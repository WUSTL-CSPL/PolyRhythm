

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

// If we do not use while loop
#define DISK_ITERATIONS 70

/* Extern trigger flags
 * To stop attack primitives
 */
extern int disk_flag;
extern struct action *shared_memory_action;

/**************************************************************
 * Parameters for fadvise disk io attack (only for Linux) *****
 * Attack against disk bandwidth
 * This is useful if the victim uses SD card and logs a lot, e.g. drone.
 * *********************************************************************
 */

/* Two access pattern */
enum io_pattern { pattern_sequential = 0, pattern_random };

extern unsigned long int diskio_contention_count;

/* Global variables */

static int num_pages;
static int disk_content_size;
static int stride;
static enum io_pattern adivse;

static char *filename;
static char *content;
static int filesize;
static int fd;  // File descriptor

/**
 * @brief Initialize disk I/O attack channels
 * @param:
 * 0: number of pages
 * 1: content size for each write operation
 * 2: stride to jump for each write operation
 */
int init_advise_disk_io_attack(void *arguments) {
    int *args = (int *)arguments;
    int posix_advise_pattern = 1;
    num_pages = args[0];  // pages
    disk_content_size = args[1];
    stride = args[2];
    adivse = args[3];

    // Set file size
    filesize = num_pages * PAGE_SIZE;

    /* Construct file name */
    filename = malloc(sizeof(char) * 20);  // 20 is fixed length of file name
    rand_str(filename, 19);

    /* Construct the content to be wrote */
    content = malloc(sizeof(char) * disk_content_size);
    /* Fill the packet with data */
    rand_str(content, disk_content_size);

    // Convert to posix pattern
    posix_advise_pattern = adivse + 1;
    // Create and open file
    fd = -1;
    while (fd < 0) {
        fd = open(filename, O_RDWR | O_CREAT);
        if (fd < 0)
            printf("Disk IO Attack failed to open file, errno=%d (%s)\n", errno,
                   strerror(errno));
        return EXIT_FAILURE;
    }

    // Set page cache advice, POSIX_FADV_RANDOM is probably best

    if (posix_fadvise(
            fd, 0, 0,
            posix_advise_pattern)) {  // Currently set to POSIX_FADV_RANDOM
        printf("Disk IO Attack failed to set fadvise, errno=%d (%s)\n", errno,
               strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Main disk I/O attack loop.
 */
int advise_disk_io_attack() {
    // Track file offset
    int offset = 0;

    /* Execute attack loop */

#ifdef RL_ONLINE

#ifdef TIMER
    while (shared_memory_action->disk) {
#else
    for (int it = 0; it < DISK_ITERATIONS; it++) {
#endif

#else
    /* For normal mode of PolyRhythm */
    while (disk_flag) {
#endif

        // Write to file
        if (write(fd, content, disk_content_size) < 0) {
            printf("Disk IO Attack failed to write to file, errno=%d (%s)\n",
                   errno, strerror(errno));
            return EXIT_FAILURE;
        }

        // Flush writeback buffer
        if (fsync(fd)) {
            printf(
                "Disk IO Attack failed to flush writeback buffer, errno=%d "
                "(%s)\n",
                errno, strerror(errno));
            return EXIT_FAILURE;
        }

        // Seek in file, use filesize-size to keep from writing past end
        if (adivse == pattern_sequential) {
            offset = (offset + stride) % (filesize - disk_content_size);
        } else if (adivse == pattern_random) {
            offset = rand() % (filesize - disk_content_size);
        }

        if (lseek(fd, offset, SEEK_SET) < 0) {
            printf(
                "Disk IO Attack failed to flush writeback buffer, errno=%d "
                "(%s)\n",
                errno, strerror(errno));
            return EXIT_FAILURE;
        }

        /* Count the disk I/O loop, less count means more cache contention */
        diskio_contention_count++;

    }  // End of attack loop

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action
    // sched_next_tasks();

    /*  Don't need to close the socket if we launch disk I/O attack later */
    //    close(fd);

    return 0;
}
