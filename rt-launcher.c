/*

    Launches one or more programs,
    allowing them to be pinned to cores,
    and additionally have a scheduling policy assigned according to one of:
        default CFS
        SCHED_RR (with a priority)
        SCHED_DEADLINE (with a period, deadline, and runtime)

    Command-line looks like this:

    ./launcher <num_programs>

    For each program, additional arguments:
        <num_instances> <scheduler attrs ...> <num_args> <command>

    The scheduler attributes depend on the value of scheduler
        c: Use CFS. No additional attributes.
        f: Use SCHED_RR. A priority is specified
        d: Use SCHED_DEADLINE. A period and deadline are specified (in ms)

    For each instance, additional arguments:
        <core> <utilization?>

    Utilization is only specified for SCHED_DEADLINE.
    In this case, runtime is set to period * utilization

    <num_args> specifies the number of tokens (including the path to the
   executable) in the <command> arguments. This is what will be launched

    If core is -1, then no core pinning is used
    Otherwise, the specified core is pinned

    For example:

    ./launcher 3 \
        1 f 10 1 slam -1 \
        4 d 50 50 3 ./rl -P ../params.txt 0 0.2 1 0.3 2 0.4 3 0.5 \
        1 d 5 1 2 python3 DDPG.py 0 1.0

    This will run 3 programs:

    The first program has 1 instance using SCHED_RR at priority 10.
    It is not pinned to a core.

    The second program has 4 instances using SCHED_DEADLINE with 50ms
   period/deadline It is launched with 3 arguments:
    ./rl -P ../params.txt
    The first instance is on core 0 with a runtime of 10ms
    The second instance is on core 1 with a runtime of 15ms
    The third instance is on core 2 with a runtime of 20ms
    The fourth instance is on core 3 with a runtime of 25ms

    The third program has 1 instance using SCHED_DEADLINE with a 5ms period and
   1ms deadline It is launched with 2 arguments: python3 DDPG.py Its instance is
   on core 0.

    The launcher forks all children, which wait on a pipe to proceed.
    It sets their core affinity and scheduling attributes as needed.
    Once all children are launched,
    the launcher sets itself to the highest priority:
        If all children are CFS, it does nothing.
        If some children are SCHED_RR, it uses the maximum priority + 1
        If some children are SCHED_DEADLINE, it instead uses half the shortest
   period and deadline It then writes a number of bytes to the pipe equal to the
   number of children. The children can proceed, but the launcher is at the
   highest priority, so it can now exec a shell, which allows responsiveness if
   necessary for broken children.

*/

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// Globals
int g_argc;                // Number of command-line arguments
int arg_idx;               // Current command-line argument index
int num_children;          // Total number of child processes launched
int pipefd[2];             // Pipe for synchronization
char cgroup_filename[64];  // String for cgroup path
char cgroup_value[64];     // String for cgroup values

// CPUSET Cgroup paths for EDF
const char *cgroup_path = "/sys/fs/cgroup/cpuset/";
const char *cgroup_cpus = "cpuset.cpus";
const char *cgroup_mems = "cpuset.mems";
const char *cgroup_exclusive = "cpuset.cpu_exclusive";
const char *cgroup_procs = "cgroup.procs";

// Status bytes to write over pipe:
const char status_bad = 0;
const char status_good = 1;

#define MS_TO_NS 1E6

#define arg (argv[arg_idx])  // Get current argument from global arg index

// Schedulers, by priority
enum schedulers { cfs, rr, edf };

// sched_attr struct
struct sched_attr {
    unsigned size;                  /* Size of this structure */
    unsigned sched_policy;          /* Policy (SCHED_*) */
    unsigned long long sched_flags; /* Flags */
    int sched_nice;                 /* Nice value (SCHED_OTHER,
                                         SCHED_BATCH) */
    unsigned sched_priority;        /* Static priority (SCHED_FIFO,
                                    SCHED_RR) */
    /* Remaining fields are for SCHED_DEADLINE */
    unsigned long long sched_runtime;
    unsigned long long sched_deadline;
    unsigned long long sched_period;
};

// Initialize a sched_attr struct
#define SCHED_ATTR(sa)          \
    struct sched_attr sa;       \
    memset(&sa, 0, sizeof(sa)); \
    sa.size = sizeof(sa);

static void parent_exit(const char *fmt, ...) {
    // Print message
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    // Broadcast exit to children
    for (int i = 0; i < num_children; i++) {
        write(pipefd[1], &status_bad, 1);
    }

    // Exit
    exit(EXIT_FAILURE);
}

/* Increment command-line argument index, fail if past argc */
static inline void arg_inc(void) {
    arg_idx++;
    if (arg_idx >= g_argc) parent_exit("Missing command-line arguments!\n");
}

/* Write a value to the given cgroup interface file */
void write_to_cgroup(const int value, const int cpu, const char *filename) {
    // Write path to interface into buffer
    sprintf(cgroup_filename, "%sc%d/%s", cgroup_path, cpu, filename);

    // Open file for writing
    int fd = open(cgroup_filename, O_WRONLY);
    if (fd == -1) {
        parent_exit("Could not open cgroup interface %s.\n");
    }

    // Write value into buffer
    memset(cgroup_value, 0, sizeof(cgroup_value));
    sprintf(cgroup_value, "%d", value);

    // Write buffer to file. On failure, proceed if cgroup already in use
    // Save errno state
    int olderrno = errno;
    errno = 0;
    if (write(fd, cgroup_value, sizeof(cgroup_value)) != sizeof(cgroup_value) &&
        errno != EBUSY) {
        parent_exit("Could not write %s to %s! errno: %d\n", cgroup_value,
                    cgroup_filename, errno);
    }
    // Restore errno state
    if (!errno) errno = olderrno;

    // Close file
    close(fd);
}

int main(int argc, char *argv[]) {
    int num_programs, max_priority, max_scheduler;
    float min_period, min_deadline;
    num_children = 0;
    arg_idx = 0;
    max_priority = -1;
    min_period = 1000;
    min_deadline = 1000;
    max_scheduler = cfs;
    g_argc = argc;

    // Array of pointers to strings to establish child arguments
    char **child_argv = (char **)malloc(sizeof(char *) * argc);
    if (!child_argv) {
        printf("Failed to malloc array for child command-line arguments!");
        exit(EXIT_FAILURE);
    }

    // Get scheduling information
    int p_max = sched_get_priority_max(SCHED_RR);
    int p_min = sched_get_priority_min(SCHED_RR);

    // Open pipe to synchronize with children
    if (pipe(pipefd)) {
        printf("Failed to create pipe!\n");
        exit(EXIT_FAILURE);
    }

    // Get the number of programs
    arg_inc();
    num_programs = atoi(arg);
    if (num_programs <= 0) {
        parent_exit("First argument must specify number of programs!\n");
    }
    printf("Number of programs: %d\n", num_programs);

    // Iterate through each program to set it up
    for (int prog = 0; prog < num_programs; prog++) {
        // Num of instances
        arg_inc();
        int num_instances = atoi(arg);
        if (num_instances < 1) {
            parent_exit("Number of instances %d must be at least 1!\n");
        }
        num_children += num_instances;
        printf("----------------------------\nProgram %d:\n%d instances\n",
               prog, num_instances);

        // Get scheduler
        arg_inc();
        char scheduler = arg[0];
        SCHED_ATTR(sa)

        switch (scheduler) {
            // SCHED_RR
            case 'f':
                if (max_scheduler < rr) max_scheduler = rr;

                sa.sched_policy = SCHED_RR;

                // Get priority
                arg_inc();
                int priority = atoi(arg);
                if (priority >= p_max || priority < p_min) {
                    parent_exit("Priority %d must be in the range %d-%d\n",
                                priority, p_min, p_max - 1);
                }
                if (max_priority < priority) max_priority = priority;
                sa.sched_priority = priority;
                printf("Setting to SCHED_RR with priority %d\n", priority);
                break;

            // SCHED_DEADLINE
            case 'd':
                if (max_scheduler < edf) max_scheduler = edf;

                sa.sched_policy = SCHED_DEADLINE;

                // Get period
                arg_inc();
                int period = atoi(arg);
                if (min_period > period) min_period = period;
                sa.sched_period = period * MS_TO_NS;

                // Get deadline
                arg_inc();
                int deadline = atoi(arg);
                if (deadline > period) {
                    parent_exit("Deadline %d must not exceed period %d!\n",
                                deadline, period);
                }
                if (min_deadline > deadline) min_deadline = deadline;
                sa.sched_deadline = deadline * MS_TO_NS;
                printf("Setting deadline to %d and period to %d milliseconds\n",
                       deadline, period);

            // Default CFS, do nothing
            case 'c':
                break;

            // Invalid option supplied
            default:
                parent_exit("Specified scheduler %s is invalid!\n", arg);
        }

        // Get number of args
        arg_inc();
        int num_args = atoi(arg);
        if (num_args < 1) {
            parent_exit("Number of arguments %d must be at least 1!\n",
                        num_args);
        }
        for (int child_arg = 0; child_arg < num_args; child_arg++) {
            arg_inc();
            child_argv[child_arg] = argv[arg_idx];
        }
        child_argv[num_args] = NULL;

        // Iterate through each instance to launch it
        for (int instance = 0; instance < num_instances; instance++) {
            printf("\nInstance %d:\n", instance);

            // Get core
            arg_inc();
            int core = atoi(arg);

            // Get runtime
            if (scheduler == 'd') {
                arg_inc();
                float utilization = atof(arg);
                if (utilization > 1) {
                    parent_exit("Utilization %.2f must not exceed 1!\n",
                                utilization);
                }
                sa.sched_runtime = sa.sched_deadline * utilization;
                printf("Runtime: %.2f ms\n",
                       (float)sa.sched_runtime / (MS_TO_NS));
            }

            // Fork child process
            int pid = fork();

            // Fork failed
            if (pid < 0) {
                parent_exit("Fork failed!\n");
            }

            // Parent
            if (pid) {
                printf("Forked with PID %d\n", pid);

                // Set scheduling attribute if not CFS
                if (scheduler != 'c') {
                    if (syscall(SYS_sched_setattr, pid, &sa, 0)) {
                        parent_exit(
                            "Failed to set real-time scheduling attributes!\n"
                            "sched_policy: %u \n"
                            "sched_runtime: %llu \n"
                            "sched_deadline: %llu \n"
                            "sched_period: %llu \n"
                            "errno: %d\n",
                            sa.sched_policy, sa.sched_runtime,
                            sa.sched_deadline, sa.sched_period, errno);
                    }
                }

                // Pin to core
                if (core >= 0) {
                    printf("Pinning to core %d\n", core);

                    // EDF requires a cpuset cgroup
                    if (scheduler == 'd') {
                        // Create directory for group

                        // Write directory name into buffer
                        sprintf(cgroup_filename, "%sc%d", cgroup_path, core);

                        // Create directory. On failure, proceed if directory
                        // already exists Save errno state
                        int olderrno = errno;
                        errno = 0;
                        if (mkdir(cgroup_filename, 0) && errno != EEXIST) {
                            parent_exit("Failed to create directory %s\n",
                                        cgroup_filename);
                        }
                        // Restore errno state
                        if (!errno) errno = olderrno;

                        // Done creating directory for group

                        // Write core to CPUSET
                        write_to_cgroup(core, core, cgroup_cpus);

                        // Write memory node to CPUSET
                        // TODO: This only supports single-node (node = 0)
                        // systems! We need to add support for multiple nodes
                        write_to_cgroup(0, core, cgroup_mems);

                        // Make cgroup CPU exclusive
                        write_to_cgroup(1, core, cgroup_exclusive);

                        // Write PID into group
                        write_to_cgroup(pid, core, cgroup_procs);
                    }

                    // Otherwise, just use sched_setaffinity
                    else {
                        cpu_set_t mask;
                        CPU_ZERO(&mask);
                        CPU_SET(core, &mask);
                        if (sched_setaffinity(pid, sizeof(mask), &mask)) {
                            parent_exit(
                                "Failed to set CPU affinity to core %d\n",
                                core);
                        }
                    }
                }

            }

            // Child
            else {
                // Wait on pipe
                char status;
                close(pipefd[1]);  // Close write end of pipe
                read(pipefd[0], &status, 1);

                // Parent failure
                if (status == status_bad) exit(EXIT_FAILURE);

                // Print PGID
                if (prog == 0 && instance == 0) {
                    printf("Process Group ID: %d\n", getpgid(0));
                }

                // Parent success, exec command
                execvp(child_argv[0], child_argv);
            }
        }
        // Done iterating through instances
    }
    // Done iterating through programs

    printf("----------------------------\n");

    // Set own scheduling attributes, if necessary
    SCHED_ATTR(sa)
    switch (max_scheduler) {
        case cfs:
            break;

        case rr:
            max_priority++;
            sa.sched_policy = SCHED_RR;
            sa.sched_priority = max_priority;
            printf("Parent setting priority to %d\n", max_priority);
            if (syscall(SYS_sched_setattr, 0, &sa, 0)) {
                parent_exit(
                    "Failed to set real-time scheduling attributes! errno: "
                    "%d\n",
                    errno);
            }
            break;

        case edf:
            min_period /= 2;
            min_deadline /= 2;
            sa.sched_policy = SCHED_DEADLINE;
            sa.sched_period = min_period * MS_TO_NS;
            sa.sched_deadline = min_deadline * MS_TO_NS;
            sa.sched_runtime = sa.sched_deadline;
            printf(
                "Parent setting deadline to %.2f and period to %.2f "
                "milliseconds\n",
                min_deadline, min_period);
            if (syscall(SYS_sched_setattr, 0, &sa, 0)) {
                parent_exit(
                    "Failed to set real-time scheduling attributes!\n"
                    "sched_policy: %u \n"
                    "sched_runtime: %llu \n"
                    "sched_deadline: %llu \n"
                    "sched_period: %llu \n"
                    "errno: %d\n",
                    sa.sched_policy, sa.sched_runtime, sa.sched_deadline,
                    sa.sched_period, errno);
            }
            break;
    }

    // Allow children to proceed
    printf("Children executing specified commands ...\n");
    close(pipefd[0]);  // Close read end of pipe
    for (int i = 0; i < num_children; i++) {
        write(pipefd[1], &status_good, 1);
    }

    // Exec shell
    printf(
        "Executing basic shell. You can launch another shell, e.g. "
        "/bin/bash\n");
    execlp("sh", "sh", (char *)NULL);
}
