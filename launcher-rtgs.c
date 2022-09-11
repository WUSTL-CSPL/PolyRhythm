/*

    Launches one or more programs,
    allowing them to be pinned to cores,
    have priority set,
    and added to a real-time scheduling group

    Command-line looks like this:

    ./launcher <num_programs> <rt_group_scheduling rt_period_us>

    If rt_group_scheduling is 1, then rt_period_us must be specified.
    If 0, then rt_period_us should not be specified

    For each program, additional arguments:
        <num_instances> <priority> <num_args> <command>

    For each instance, additional arguments:
        <core> <utilization>

    If priority is -1, then default (e.g., CFS scheduling) is used
    Otherwise, SCHED_FIFO is used and priority is set to the given value

    <num_args> specifies the number of tokens (including the path to the
   executable) in the <command> arguments. This is what will be launched

    If core is -1, then no core pinning is used
    Otherwise, the specified core is pinned

    If rt_group_scheduling is 0, utilization should not be specified
    Otherwise, the instance is moved into its own real-time scheduling group,
    and assigned rt_runtime_us = rt_period_us * utilization

    For example:

    ./launcher 2 1 50000 4 80 3 ./rl -P ../params.txt 0 0.2 1 0.3 2 0.4 3 0.5 1
   90 2 python3 DDPG.py 0 1.0

    This will run 2 programs with real-time group scheduling using a period of
   50ms.

    The first program has 4 instances at priority 80.
    It is launched with 3 arguments:
    ./rl -P ../params.txt
    The first instance is on core 0 with a runtime of 10ms
    The second instance is on core 1 with a runtime of 15ms
    The third instance is on core 2 with a runtime of 20ms
    The fourth instance is on core 3 with a runtime of 25ms

    The second program has 1 instance at priority 90.
    It is launched with 2 arguments:
    python3 DDPG.py
    Its instance is on core 0.
    Since it has a utilization of 1.0, its runtime is not constrained

    The launcher forks all children, which wait on a pipe to proceed.
    It sets their priority, core affinity, and real-time scheduling group as
   needed. Once all children are launched, the launcher sets itself to the
   maximum priority of all children, +1. It then writes a number of bytes to the
   pipe equal to the number of children. The children can proceed, but the
   launcher is at the highest priority, so it can now exec a shell, which allows
   responsiveness if necessary for broken children.


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
#include <sys/types.h>
#include <unistd.h>

/* Globals */

int g_argc;        // Number of command-line arguments
int arg_idx;       // Current command-line argument index
int num_children;  // Total number of child processes launched
int pipefd[2];     // Pipe for synchronization

const char *cgroup_path = "/sys/fs/cgroup/cpu/";
const char *cgroup_period = "cpu.rt_period_us";
const char *cgroup_runtime = "cpu.rt_runtime_us";
const char *cgroup_procs = "cgroup.procs";

// Status bytes to write over pipe:
const char status_bad = 0;
const char status_good = 1;

#define arg (argv[arg_idx])  // Get current argument from global arg index

#define RTSCHED SCHED_FIFO  // Scheduling class used

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

int main(int argc, char *argv[]) {
    int num_programs, rt, rt_period, max_priority;
    num_children = 0;
    arg_idx = 0;
    max_priority = -1;
    g_argc = argc;

    // String for cgroup path
    char cgroup_filename[64];
    // String for cgroup values
    char cgroup_value[64];

    // Array of pointers to strings to establish child arguments
    char **child_argv = (char **)malloc(sizeof(char *) * argc);
    if (!child_argv) {
        printf("Failed to malloc array for child command-line arguments!");
        exit(EXIT_FAILURE);
    }

    // Get scheduling information
    int p_max = sched_get_priority_max(RTSCHED);
    int p_min = sched_get_priority_min(RTSCHED);

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

    // Establish RT group scheduling
    arg_inc();
    rt = atoi(arg);
    if (rt) {
        arg_inc();
        rt_period = atoi(arg);
        if (rt_period < 1000) {
            parent_exit("Real-time period %d too low! Must be at least 1ms\n",
                        rt_period);
        }

        // Enables setting the period globally

        // Set runtime first to 0.95 * period
        int rt_total_runtime = 0.95 * rt_period;
        sprintf(cgroup_filename, "%s%s", cgroup_path, cgroup_runtime);
        int fd_runtime = open(cgroup_filename, O_WRONLY);
        if (fd_runtime == -1) {
            parent_exit(
                "Could not open cgroup interface %s. Make sure real-time group "
                "scheduling is enabled and that you have permissions!\n");
        }
        memset(cgroup_value, 0, sizeof(cgroup_value));
        sprintf(cgroup_value, "%d", rt_total_runtime);
        if (write(fd_runtime, &cgroup_value, sizeof(cgroup_value)) !=
            sizeof(cgroup_value)) {
            parent_exit("Could not write %s to %s!\n", cgroup_value,
                        cgroup_filename);
        }
        close(fd_runtime);

        // Then set period
        sprintf(cgroup_filename, "%s%s", cgroup_path, cgroup_period);
        int fd_period = open(cgroup_filename, O_WRONLY);
        if (fd_period == -1) {
            parent_exit(
                "Could not open cgroup interface %s. Make sure real-time group "
                "scheduling is enabled and that you have permissions!\n");
        }
        memset(cgroup_value, 0, sizeof(cgroup_value));
        sprintf(cgroup_value, "%d", rt_period);
        if (write(fd_period, &cgroup_value, sizeof(cgroup_value)) !=
            sizeof(cgroup_value)) {
            parent_exit("Could not write %s to %s!\n", cgroup_value,
                        cgroup_filename);
        }
        close(fd_period);

        printf("Real-time group scheduling will use a period of %dus\n",
               rt_period);

    } else {
        printf("Real-time group scheduling will not be used\n");
    }

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

        // Get priority
        arg_inc();
        int priority = atoi(arg);
        struct sched_param sp;
        if (priority < 0) {
            printf("Using default scheduling class\n");
        } else if (priority >= p_max || priority < p_min) {
            parent_exit("Priority %d must be in the range %d-%d\n", priority,
                        p_min, p_max - 1);
        } else {
            if (max_priority < priority) max_priority = priority;
            sp.sched_priority = priority;
            printf("Setting priority to %d\n", priority);
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
            int rt_runtime;
            if (rt) {
                arg_inc();
                rt_runtime = atof(arg) * rt_period;
                if (rt_runtime > rt_period) {
                    parent_exit("Runtime %d must not exceed period %d!\n",
                                rt_runtime, rt_period);
                }
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

                // Set core affinity
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(core, &mask);
                if (sched_setaffinity(pid, sizeof(mask), &mask)) {
                    parent_exit("Failed to set CPU affinity to core %d\n",
                                core);
                }
                printf("Pinned to core %d\n", core);

                // Set priority
                if (priority >= 0) {
                    if (sched_setscheduler(pid, RTSCHED, &sp)) {
                        parent_exit(
                            "Failed to set real-time scheduling policy!\n");
                    }
                }

                // Set real-time scheduling group
                if (rt && rt_runtime < rt_period) {
                    // Create directory for group

                    // Write directory name into buffer
                    sprintf(cgroup_filename, "%sp%d-i%d", cgroup_path, prog,
                            instance);

                    // Create directory. On failure, proceed if directory
                    // already exists
                    if (mkdir(cgroup_filename, 0) && errno != EEXIST) {
                        parent_exit("Failed to create directory %s\n",
                                    cgroup_filename);
                    }
                    printf("Setting runtime to %dus in cgroup %s\n", rt_runtime,
                           cgroup_filename);

                    // Done creating directory for group

                    // Write period to group

                    // Write path to period interface into buffer
                    sprintf(cgroup_filename, "%sp%d-i%d/%s", cgroup_path, prog,
                            instance, cgroup_period);

                    // Open file for writing
                    int fd_period = open(cgroup_filename, O_WRONLY);
                    if (fd_period == -1) {
                        parent_exit("Could not open cgroup interface %s.\n");
                    }

                    // Write period into buffer
                    memset(cgroup_value, 0, sizeof(cgroup_value));
                    sprintf(cgroup_value, "%d", rt_period);

                    // Write buffer to file
                    if (write(fd_period, cgroup_value, sizeof(cgroup_value)) !=
                        sizeof(cgroup_value)) {
                        parent_exit("Could not write %s to %s!\n", cgroup_value,
                                    cgroup_filename);
                    }

                    // Close file
                    close(fd_period);

                    // Done creating period for group

                    // Write runtime to group

                    // Write path to runtime interface into buffer
                    sprintf(cgroup_filename, "%sp%d-i%d/%s", cgroup_path, prog,
                            instance, cgroup_runtime);

                    // Open file for writing
                    int fd_runtime = open(cgroup_filename, O_WRONLY);
                    if (fd_runtime == -1) {
                        parent_exit("Could not open cgroup interface %s.\n");
                    }

                    // Write runtime into buffer
                    memset(cgroup_value, 0, sizeof(cgroup_value));
                    sprintf(cgroup_value, "%d", rt_runtime);

                    // Write buffer to file
                    if (write(fd_runtime, cgroup_value, sizeof(cgroup_value)) !=
                        sizeof(cgroup_value)) {
                        parent_exit("Could not write %s to %s!\n", cgroup_value,
                                    cgroup_filename);
                    }

                    // Close file
                    close(fd_runtime);

                    // Done creating runtime for group

                    // Write PID into group

                    // Write path to cgrou procs interface into buffer
                    sprintf(cgroup_filename, "%sp%d-i%d/%s", cgroup_path, prog,
                            instance, cgroup_procs);

                    // Open file for writing
                    int fd_procs = open(cgroup_filename, O_WRONLY);
                    if (fd_procs == -1) {
                        parent_exit("Could not open cgroup interface %s.\n");
                    }

                    // Write PID into buffer
                    memset(cgroup_value, 0, sizeof(cgroup_value));
                    sprintf(cgroup_value, "%d", pid);

                    // Write buffer to file
                    if (write(fd_procs, cgroup_value, sizeof(cgroup_value)) !=
                        sizeof(cgroup_value)) {
                        parent_exit("Could not write %s to %s!\n", cgroup_value,
                                    cgroup_filename);
                    }

                    // Close file
                    close(fd_procs);

                    // Done writing PID into group
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

    // Set own priority
    if (max_priority > -1) {
        max_priority += 1;
        struct sched_param sp;
        sp.sched_priority = max_priority;
        printf("Parent setting priority to %d\n", max_priority);
        if (sched_setscheduler(0, RTSCHED, &sp)) {
            parent_exit("Failed to set real-time scheduling policy!\n");
        }
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
