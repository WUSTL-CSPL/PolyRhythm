#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "Attacks.h"
#include "Utils.h"

/*** Raspberry pi 3b ***/
/* The best MEM size for BwWrite (DRAM) attack is 835 KB */
/* Both BwRead as victim - 700x and the SD Benchmark disparity - 84x */

// If we do not use while loop
// Specify the number of loops last approximate 5ms
#define CACHE_ITERATIONS 30

/*
 * Parameters:
 * stride: how many cache line skip while evicting the memory
 * mem_size: how large memory should allocate. normally a bit greater than LLC
 * cache size
 */

/* Extern trigger flags
 * To stop attack primitives
 */
extern int cache_flag;
extern struct action *shared_memory_action;

/* feedbacks for RL, defined in PolyRhythm_RL.c */
extern unsigned long int cache_contention_count;
extern unsigned long int cache_exe_time;

/*
 *  Paramters for cache attack
 */

#define KB ((1) << 10)
#define CACHE_LINE 64

/*
 * Global Variables
 */

static int *cache_attack_array[DESIRED_NUM_THREAD];

static int attack_array_indicator = 0;
/* Need index to indicate the array */

static int stride;
static int mem_size;

/**
 * @brief Initialize cache attack channels
 * @param:
 * 0: stride size to jump in each access
 * 1: memory size to iterate
 */
int init_cache_attack(void *arguments) {
    int i, j;

    int *args = (int *)arguments;

    /* Parse the parameters */
    stride = args[0];  // How much cache line need to jump for each access?
    mem_size = args[1] * KB;  // How much memory need to allocated?
    // Other parameter could be the policy read or write

    // printf("Debug, stride %d, mem_size %d \n", stride, mem_size);

    if (stride < 0 || mem_size < 0) {
        printf("Cache attack: invalide arguments \n");
        return EXIT_FAILURE;
    }
    stride = stride * CACHE_LINE;

    /* Allocate memory */
    /* Each thread need a chunk of memory */
    for (i = 0; i < DESIRED_NUM_THREAD; i++) {
        cache_attack_array[i] = (int *)malloc(mem_size);
    }

    /* Initialize the array */
    for (j = 0; j < DESIRED_NUM_THREAD; j++) {
        for (int i = 0; i < mem_size / sizeof(int); i++)
            cache_attack_array[j][i] = i;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief May need to point the thread back to the allocated memory
 */
int cache_attack_reset_if_necessary() {
    if (attack_array_indicator == DESIRED_NUM_THREAD - 1) {
        attack_array_indicator = 0;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Main cache attack loop, which exhaustively evicts cache lines
 */
int cache_attack() {
    register volatile int *local_attack_array =
    /* No need to increment attack_array_indicator if it single-threaded
                                   RL is launched in single-threaded, each
       PolyRhythm instance has one thread */
#ifdef RL_ONLINE
        cache_attack_array[attack_array_indicator];
#else
        /* We increment the indicator only we launch multiple attack threads in
           one process */
        cache_attack_array[attack_array_indicator++];
#endif

    register unsigned int sum = 0;

    /* Attack loop */
#ifdef RL_ONLINE

#ifdef TIMER
    while (shared_memory_action->cache) {
#else
    for (int it = 0; it < CACHE_ITERATIONS; it++) {
#endif

#else
    /* For normal mode of PolyRhythm */
    while (cache_flag) {
#endif
        for (int i = 0; i < mem_size / sizeof(int); i += CACHE_LINE / 4) {
            /* Read or Write, choose one of them, or both? */

            /* Read attack */
            // sum += local_attack_array[i];

            /* Write attack */
            local_attack_array[i] = 0xff;
        }

        /* Count the cache loop, less count means more cache contention */
        cache_contention_count++;

    }  // End of while or for loop

    // sched_next_tasks();

    return EXIT_SUCCESS;
}

/**
 * @brief Muate cache parameter
 * Ignore this cache parameter mutation is done in genetic.py
 */

void cache_mutation(int *stride, int *mem_size) {
    // Add more mutation.
    *mem_size += 15;
}

/*******************************************************************************************
 * The functions below are for
 * Step 2 --
 * Online contention region profiling
 */

/*
 * If we enable online contention region searching
 * We further split the target memory into different slices
 */

/* Granularity of contention region */
#define NUM_SLICE 64
#define TIMES_REMAP 10  // Max online memory remap time
static int *o_cache_attack_array[NUM_SLICE];

/* Global variables */
static int *last_contention_addr;
static int last_contention_index;
static long int timings[NUM_SLICE];
static int iteration_count = 0;  // Count

/**
 * @brief Initialization if we enable online contention region search
 *        We need to divide allocated memory into different slices
 */
int init_online_profiling_cache_attack(void *arguments) {
    int i, j;

    int *args = (int *)arguments;
    /* Parse the parameters */
    stride = args[0];
    mem_size = args[1] * KB;
    // Other parameter could be the policy read or write

    if (stride <= 0 || mem_size <= 0) {
        printf("Cache attack: invalide arguments \n");
        return EXIT_FAILURE;
    }

    stride = stride * CACHE_LINE;

    /* Allocate memory for each slice */
    for (i = 0; i < NUM_SLICE; i++) {
        o_cache_attack_array[i] = (int *)malloc(mem_size / NUM_SLICE);
    }

    /* Initialize the array */
    for (i = 0; i < NUM_SLICE; i++) {
        for (j = 0; j < (mem_size / NUM_SLICE) / sizeof(int); j++)
            o_cache_attack_array[i][j] = j;
    }

    /* Initialize the iteration count */
    iteration_count = 0;
    return EXIT_SUCCESS;
}

/**
 * @brief Online remap / re-allocated a slice of memory
 * @param mem base memory address
 * @param min_index index of the least contending memory region
 */
int online_profiling_cache_remap_memory(int **mem, int min_index) {
    printf("Min index %d \n", min_index);

    last_contention_addr = mem[min_index];
    last_contention_index = min_index;
    // Use mremap() to achieve fast remapping
    // void *temp_addr = mremap(mem[min_index], (mem_size / NUM_SLICE),
    // (mem_size / NUM_SLICE), 0, MREMAP_MAYMOVE);
    mem[min_index] = (int *)malloc(mem_size / NUM_SLICE);

    // One more time of contention region remap
    iteration_count++;
    return EXIT_SUCCESS;
}

/**
 * @brief Remap back the last memory region
 * @param mem base memory address
 */
int switch_back_memory_region(int **mem) {
    // Use mremap() to achieve fast remapping
    // void *temp_addr = mremap(mem[min_index], (mem_size / NUM_SLICE),
    // (mem_size / NUM_SLICE), 0, MREMAP_MAYMOVE);
    mem[last_contention_index] = last_contention_addr;

    // One more time of contention region remap
    iteration_count++;
    return EXIT_SUCCESS;
}

/**
 * @brief Main attack loop for cache attack with online profiling
 */

int online_profiling_cache_attack() {
    int i, j;

    /* No need to increment attack_array_indicator if it single-threaded
                                   RL is launched in single-threaded, each
       PolyRhythm instance has one thread */

    /* We increment the indicator only we launch multiple attack threads in one
     * process */
    // cache_attack_array[attack_array_indicator++];

    register unsigned int sum = 0;

    /* Profiling Loop */

    // We calculate the least contended region after a fix number of loops
    int tmp_count = 0;  // This variable is used to count
    int last_timing = 0;
    bool ready_to_jump = false;
    while (cache_flag) {
        for (i = 0; i < NUM_SLICE; i++) {
            /* We measure the time for each iteration, less time meams more
             * contention */
            long start = get_current_time_us();
            // printf("Memory region index %d \n", i);
            for (j = 0; j < (mem_size / NUM_SLICE) / sizeof(int);
                 j += CACHE_LINE / 4) {
                /* Read or Write, choose one of them, or both? */
                /* Read attack */
                // sum += local_attack_array[i];
                /* Write attack */
                o_cache_attack_array[i][j] = 0xff;
            }

            long end = get_current_time_us();
            /* Store timings for each region(slice) */
            timings[i] += end - start;
        }

        /* Calculate the least contended region */
        if (tmp_count == 2000) {
            /* Calculate the least contended region */
            int i, min_index = 0;
            for (i = 0; i < NUM_SLICE; i++) {
                if (timings[i] < timings[min_index]) {
                    min_index = i;
                }
            }

            if (timings[min_index] > last_timing) {
                /* We can try to remap a new region */

                /* Replace the least contending region */
                online_profiling_cache_remap_memory(o_cache_attack_array,
                                                    min_index);
                ready_to_jump = false;
            } else {
                /* In this case, the new allocated memory is less contending
                 * than previous one */
                /* We switch back */
                switch_back_memory_region(o_cache_attack_array);
                ready_to_jump = true;
            }

            last_timing = timings[min_index];

            printf("Last timing %d \n", last_timing);
            /* Reset the timings */
            for (i = 0; i < NUM_SLICE; i++) {
                timings[i] = 0;
            }

            /* Re-calculate the least contending region */
            tmp_count = 0;
        } else {
            tmp_count++;
        }
        /* Count the cache loop, less count means more cache contention */
        cache_contention_count++;

        /* Once the remap times reach the maximum number
         * jump to the attack loop without any time recording
         */

        if (iteration_count > TIMES_REMAP && ready_to_jump) {
            /* End of profiling, go to attack loop */
            goto no_profiling;
        }

    }  // End of while

no_profiling:
    printf("Entering the attack loop \n");
    /* Attack loop */
    /* With less if else predicate, this attack loop is more effective */
    while (cache_flag) {
        for (i = 0; i < NUM_SLICE; i++) {
            for (j = 0; j < (mem_size / NUM_SLICE) / sizeof(int);
                 j += CACHE_LINE / 4) {
                /* Read or Write, choose one of them, or both? */

                /* Read attack */
                // sum += local_attack_array[i];

                /* Write attack */
                o_cache_attack_array[i][j] = 0xff;
            }
        }
        /* Count the cache loop, less count means more cache contention */
        // cache_contention_count++;

    }  // End of while

    // sched_next_tasks();

    return EXIT_SUCCESS;
}