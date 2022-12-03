
#include "Attacks.h"

#include <errno.h>
#include <time.h>

#include "PolyRhythm.h"
#include "Utils.h"

/* network attack */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* tlb attack & online profiling */
#include <string.h>
#include <sys/mman.h>

/* spwan attack */
#include <spawn.h>

/* Memory contention */
#include <pthread.h>

/* File sytem & Disk IO contention */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
/* End of includes */

/* Two access pattern */
enum mbandwidth_pattern { pattern_read = 0, pattern_write };

/* set the timer that triggers the tasks scheduling */
struct sigaction sa;
struct itimerval timer;

/* All extern trigger flags */
extern int memory_ops_flag;
extern int context_switch_flag;

/*************************************
 * File system attack
 * Performing  file  system  activities  such  as  making/deleting
 * files/directories, moving files, etc.
 * to block the kernel shared data structures and
 * stress various notify events
 * ***********************************
 */

/*
 *
 *	Create file of length len bytes
 *
 */

#define FILE_WRITE_SIZE (4096)
static int create_file(const char *filename) {
    int fd;
    char buffer[FILE_WRITE_SIZE];

    if ((fd = open(filename, O_CREAT | O_RDWR)) < 0) {
        if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC))
            return EXIT_FAILURE;
        printf("Cannot create file %s: errno=%d (%s)\n", filename, errno,
               strerror(errno));
        return EXIT_FAILURE;
    }

    (void)memset(buffer, 'x', FILE_WRITE_SIZE);

    ssize_t ret;

    ret = write(fd, buffer, FILE_WRITE_SIZE);
    if (ret < 0) {
        printf("Error writing to file %s: errno=%d (%s)\n", filename, errno,
               strerror(errno));
        (void)close(fd);
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}

/*
 *	remove a file
 */
int remove_file(const char *pathname) { return unlink(pathname); }

/*
 *	access a file by reading
 */
static int access_file(const char *path) {
    int fd;
    char buffer[1];
    int rc = 0;

    if ((fd = open(path, O_RDONLY)) < 0) {
        printf("File open error\n");
        return EXIT_FAILURE;
    }

do_access:
    if ((read(fd, buffer, 1) < 0)) {
        if ((errno == EAGAIN) || (errno == EINTR)) goto do_access;
        printf("Cannot access the file %s\n", path);
        rc = -1;
    }
    (void)close(fd);
    return rc;
}

/*
 *	move
 */
static int move_file(const char *oldpath, const char *newpath) {
    if (rename(oldpath, newpath) < 0) {
        printf("Cannot rename %s to %s: errno=%d (%s)\n", oldpath, newpath,
               errno, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* Parameters for file system attack */

/* Four actions: access, create, delete, move */

typedef enum { CREATE = 0, MOVE, DELETE, ACCESS } file_op;

static file_op mode;

int init_filesys_attack(void *arguments) {
    int *args = (int *)arguments;
    mode = args[0];  // What type of operation
}

int filesys_attack() {
    int i;
    char tmp_create_filename[] = {[41] = '\1'};
    char *access_filenames[100];

    switch (mode) {
        case CREATE:
            /* */
            while (1) {
                rand_str(tmp_create_filename, sizeof(tmp_create_filename) - 1);
                create_file(tmp_create_filename);
            }
            break;
        case ACCESS:
            /* */
            for (i = 0; i < 100; i++) {
                access_filenames[i][41] = '\1';
                rand_str(access_filenames[i], sizeof(access_filenames[i]) - 1);
                create_file(access_filenames[i]);
            }
            while (1) {
                i = rand() % 100;  // Randomly access these files
                access_file(access_filenames[i]);
            }
            break;
        default:
            break;
    }

    /* operation on inodes */
    return EXIT_SUCCESS;
}

/*************************************
 * Parameters for spawn attack //TO FIX
 * ***********************************
 */

#define MAX_THREAD 10000  // TO FIX
#define PATH_MAX 4096     /* # chars in a path name including nul */

extern char **environ;  // A dummy parameters for posix_spawn()
char *argv[] = {"spawn", "2", "1",
                "1",     "1", "1"};  // parameters for posix_spawn()

int spawn_attack() {
    printf("spwan attack begin \n");
    int thread_count = 0;
    char path[PATH_MAX];  // why + 1, 4096 is including nul
    unsigned int len;

    /* Determine our own self as the spawnutable */
    len = readlink("/proc/self/exe", path, sizeof(path));

    pid_t tmp_pid;
    while (1) {
        int ret;
        pid_t pid;
        ret = posix_spawn(&tmp_pid, path, NULL, NULL, argv, environ);
        if (ret < 0) {
            printf("Spawn: posix_spawn failed, errno=%d (%s)\n", errno,
                   strerror(errno));
            return EXIT_FAILURE;
        }
        thread_count++;
        printf("spwan succeded \n");

        if (thread_count > MAX_THREAD) {
            // break;
            while (1) {
            }
        }
    }

    printf("spwan end \n");
    return EXIT_SUCCESS;
}

/*************************************
 * Parameters for disk io attack *****
 * Attack against disk bandwidth
 * This is useful if the victim uses SD card and logs a lot, e.g. drone.
 * ***********************************
 */

int disk_io_attack() {
    double cpu_start;
    double cpu_end;

    char filename[] = {[41] = '\1'};
    char content[] = {[128] = '\1'};  //  TO CHANGE
    rand_str(filename, sizeof(filename) - 1);
    rand_str(content, sizeof(content) - 1);

    FILE *out = fopen(filename, "w");

    while (1) {
        //
        fputs(content, out);
        fflush(out);
    }

    return EXIT_SUCCESS;
}

/**********************************************
 * Parameters for memory bus contention *****
 * ********************************************
 */

static int max_num_threads = 0;
static int mem_ops_size = 0;
static int paccess = 0;  // Read
static int log_flag = 0;

void *data[2];  // create two virtual memory concurrently access the same
                // physical memory
void *last_data[2];

FILE *mfile;
int fd, rc;

/* For online profiling */
static long int last_timing;
static int on_new_memory_flag = 0;
static int online_flag = 0;

int online_profiling_memory_ops_switch_back() {
    data[0] = last_data[0];
    data[1] = last_data[1];

    on_new_memory_flag = 0;
    return EXIT_SUCCESS;
}

int online_profiling_memory_ops_remap_memory() {
    char filename[] = {[41] = '\1'};
    char content[] = {[128] = '\1'};  //  TO CHANGE
    rand_str(filename, sizeof(filename) - 1);
    rand_str(content, sizeof(content) - 1);

    mfile = fopen(filename, "w+");
    // printf("file name %s \n", filename);

    fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    rc = page_write_sync(fd, 4 * KB);

    /*
     *  Get two different mappings of the same physical page
     *  just to make things more interesting
     */

    last_data[0] = data[0];
    last_data[1] = data[1];

    data[0] =
        mmap(NULL, mem_ops_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    data[1] =
        mmap(NULL, mem_ops_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    /* Online profiling flag */
    on_new_memory_flag = 1;

    return EXIT_SUCCESS;
}

/* This attack contains two functions*/

void stress_memory_bus_contention(void *data) {
    uint64_t **mappings = data;
    volatile uint64_t *vdata0 = mappings[0];
    volatile uint64_t *vdata1 = mappings[1];
    uint64_t *data0 = mappings[0];
    uint64_t *data1 = mappings[1];
    register int i;

    // We calculate the least contended region after a fix number of loops
    int tmp_count = 0;
    long int timing = 0;

    while (memory_ops_flag) {
        long start = get_current_time_us();

        for (i = 0; i < 1024; i++) {
            if (paccess == pattern_write) {
                vdata0[0] = (uint64_t)i;
                vdata1[0] = (uint64_t)i;
                vdata0[1] = (uint64_t)i;
                vdata1[1] = (uint64_t)i;
                vdata0[2] = (uint64_t)i;
                vdata1[2] = (uint64_t)i;
                vdata0[3] = (uint64_t)i;
                vdata1[3] = (uint64_t)i;
                vdata0[4] = (uint64_t)i;
                vdata1[4] = (uint64_t)i;
                vdata0[5] = (uint64_t)i;
                vdata1[5] = (uint64_t)i;
                vdata0[6] = (uint64_t)i;
                vdata1[6] = (uint64_t)i;
                vdata0[7] = (uint64_t)i;
                vdata1[7] = (uint64_t)i;
            } else if (paccess == pattern_read) {
                read64(data0);
                read64(data1);
            } else {
                printf("[Memory Bandwidth] Unknown Access Pattern\n");
            }
        }
#ifdef x86

        for (i = 0; i < 1024; i++) {
            if (paccess == pattern_write) {
                vdata0[0] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                asm volatile("" : : : "memory");
                vdata1[0] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[1] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[1] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[2] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[2] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[3] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[3] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[4] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[4] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[5] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[5] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[6] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[6] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata0[7] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
                vdata1[7] = (uint64_t)i;
                asm volatile("mfence" : : : "memory");
            } else if (paccess == pattern_read) {
                read64(data0);
                read64(data1);
            } else {
                printf("[Memory Bandwidth] Unknown Access Pattern\n");
            }
        }

        for (i = 0; i < 1024; i++) {
            if (paccess == pattern_write) {
                vdata0[0] = (uint64_t)i;
                vdata1[0] = (uint64_t)i;
                vdata0[1] = (uint64_t)i;
                vdata1[1] = (uint64_t)i;
                vdata0[2] = (uint64_t)i;
                vdata1[2] = (uint64_t)i;
                vdata0[3] = (uint64_t)i;
                vdata1[3] = (uint64_t)i;
                vdata0[4] = (uint64_t)i;
                vdata1[4] = (uint64_t)i;
                vdata0[5] = (uint64_t)i;
                vdata1[5] = (uint64_t)i;
                vdata0[6] = (uint64_t)i;
                vdata1[6] = (uint64_t)i;
                vdata0[7] = (uint64_t)i;
                vdata1[7] = (uint64_t)i;
                asm volatile("clflush (%0)" ::"r"(data0) : "memory");
                asm volatile("clflush (%0)" ::"r"(data1) : "memory");
                else if (paccess == pattern_read) {
                    read64(data0);
                    read64(data1);
                }
                else {
                    printf("[Memory Bandwidth] Unknown Access Pattern\n");
                }
            }
#endif
            for (i = 0; i < 1024; i++) {
                if (paccess == pattern_write) {
                    vdata0[0] = (uint64_t)i;
                    asm volatile(""
                                 :
                                 :
                                 : "memory");  // Add memory barrier to compiler
                    vdata1[0] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[1] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[1] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[2] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[2] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[3] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[3] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[4] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[4] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[5] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[5] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[6] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[6] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata0[7] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                    vdata1[7] = (uint64_t)i;
                    asm volatile("" : : : "memory");
                } else if (paccess == pattern_read) {
                    read64(data0);
                    read64(data1);
                } else {
                    printf("[Memory Bandwidth] Unknown Access Pattern\n");
                }
            }

            if (online_flag) {
                long end = get_current_time_us();

                timing += (end - start);

                if (tmp_count == 8000) {
                    /* Calculate the least contended region */
                    if (last_timing == 0) {
                        last_timing = timing;
                        continue;
                    }

                    /* Check if the current if better than last */
                    if (on_new_memory_flag) {
                        if (timing < last_timing) {
                            online_profiling_memory_ops_switch_back();
                        } else {
                            /* We can re-allocate memory now */
                            on_new_memory_flag = 0;
                        }
                    } else { /* Re-allocate new memory */
                        online_profiling_memory_ops_remap_memory();
                    }

                    last_timing = timing;
                    timing = 0;
                    tmp_count = 0;
                } else {
                    tmp_count++;
                }
            }

        }  // End of while loop
    }

    int init_memory_contention_attack(void *arguments) {
        int *args = (int *)arguments;
        /* Parse the parameters */
        max_num_threads = args[0];
        mem_ops_size = args[1] * LLC_CACHE_SIZE;
        paccess = args[2];
        // printf("Memory to allocate : %d MB \n", mem_ops_size / (1024 *
        // 1024)); the 4th parameter is the log flag by default

        online_flag = args[NUM_PARAMS];

        // printf("online flag : %d \n", online_flag);
        return EXIT_SUCCESS;
    }

    int memory_contention_attack() {
        int i;
        pthread_t pthreads[max_num_threads];
        int ret[max_num_threads];

        char filename[] = {[41] = '\1'};
        char content[] = {[128] = '\1'};  //  TO CHANGE
        rand_str(filename, sizeof(filename) - 1);
        rand_str(content, sizeof(content) - 1);

        mfile = fopen(filename, "w+");
        // printf("file name %s \n", filename);

        fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        rc = page_write_sync(fd, 4 * KB);

        /*
         *  Get two different mappings of the same physical page
         *  just to make things more interesting
         */

        // printf("Start mmap \n");
        data[0] = mmap(NULL, mem_ops_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                       fd, 0);
        if (data[0] == MAP_FAILED) {
            printf("Memory contend [1]: mmap failed: errno=%d (%s)\n", errno,
                   strerror(errno));
            (void)close(fd);
            return EXIT_FAILURE;
        }

        data[1] = mmap(NULL, mem_ops_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                       fd, 0);
        if (data[1] == MAP_FAILED) {
            printf("Memory contend [2]: mmap failed: errno=%d (%s)\n", errno,
                   strerror(errno));
            (void)munmap(data[0], mem_ops_size);
            (void)close(fd);
            return EXIT_FAILURE;
        }
        (void)close(fd);

        for (i = 0; i < max_num_threads; i++) {
            ret[i] =
                pthread_create(&pthreads[i], NULL,
                               (void *)stress_memory_bus_contention, &data);
        }

        pthread_join(pthreads[0], NULL);
        return EXIT_SUCCESS;
    }

    /********************************************
     * Parameters for context switch attack *****
     *
     * Implements a context switching attack
     * If run by a high-priority attacker with utilization constrained,
     * it should induce a large number of context switches in the victim
     *
     * Sets a timer for length DURATION,
     * which suspends the attacking thread for that amount of time
     *
     * This forces rapid context switching between attacker and victim
     * without using much of the attacker's given bandwidth/utilization
     *
     * ******************************************
     */

    static int context_switch_duration;

    int context_switch_attack() {
        while (context_switch_flag) {
            if (usleep(context_switch_duration)) return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }