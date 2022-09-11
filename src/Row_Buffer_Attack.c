

#include <errno.h>
#include <time.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

/******************************************
 * Parameters for memory row buffer attack
 ******************************************
 */

/* Extern trigger flags
 * To stop attack primitives
 */
extern int row_buffer_flag;
extern struct action *shared_memory_action;

/* feedbacks for RL, defined in PolyRhythm_RL.c */
extern unsigned long int row_buffer_contention_count;
static long int last_timing;

/**
 * @brief:
 * This attack is based on the paper:
 * Security' 07,
 * Memory performance attacks: Denial of memory service in multi-core systems
 */

#define MEM_SIZE (LLC_CACHE_SIZE * 100)
#define TIMES_REMAP 30
#define slices

// If we do not use while loop
#define ROW_ITERATIONS 3

static int *row_buffer_attack_array_a[DESIRED_NUM_THREAD];
static int *row_buffer_attack_array_b[DESIRED_NUM_THREAD];
static int *row_buffer_attack_array_index[DESIRED_NUM_THREAD];

static int *last_row_buffer_attack_array_a[DESIRED_NUM_THREAD];
static int *last_row_buffer_attack_array_b[DESIRED_NUM_THREAD];
static int *last_row_buffer_attack_array_index[DESIRED_NUM_THREAD];

static int on_new_memory_flag = 0;

static int row_buffer_indicator = 0;

static int random_flag = 1;  // Default access policy is random

/* Need index to indicate the array */
static int stride_scalar;  // This parameter only works if the access pattern is
                           // sequential
static int mem_size;
static int log_flag;  // Shared by several attacks

/* Global variables */
static int ram_mem_size = 0;
static int iteration_count = 0;  // Count

/**
 * @brief Initialize row buffer attack channels
 * @param:
 * 0: stride size to jump in each access
 * 1: memory size to iterate
 * 2: flag to trigger random access pattern
 */
int init_memory_row_buffer_attack(void *arguments) {
    int i, j;  // Index
    int *args = (int *)arguments;

    stride_scalar = args[0];
    ram_mem_size = args[1] * LLC_CACHE_SIZE;
    random_flag = args[2];

    // printf("Memory Attack: Allocated memory size : %d \n", ram_mem_size);

    for (i = 0; i < DESIRED_NUM_THREAD; i++) {
        /* Allocation array a  */
        row_buffer_attack_array_a[i] = (int *)malloc(ram_mem_size);

        /* Allocation array b  */
        row_buffer_attack_array_b[i] = (int *)malloc(ram_mem_size);

        /* Allocation array index  */
        row_buffer_attack_array_index[i] = (int *)malloc(ram_mem_size);

        /* Initialize the index array according to the policy */
        if (random_flag) {
            // randomly access row buffer
            for (j = 0; j < ram_mem_size / sizeof(int); j++) {
                row_buffer_attack_array_index[i][j] =
                    rand() % (ram_mem_size / sizeof(int));
            }
        } else {
            // sequential access row buffer
            int tmp_index = 0;
            int k;
            for (k = 0; k < stride_scalar; k++) {
                for (j = k; j + stride_scalar < (ram_mem_size / sizeof(int));
                     j = j + stride_scalar) {
                    row_buffer_attack_array_index[i][tmp_index++] = j;
                }
            }
        }

        // Initialize a and b
        for (j = 0; j < ram_mem_size / sizeof(int); j++) {
            row_buffer_attack_array_a[i][j] = rand();
            row_buffer_attack_array_b[i][j] = rand();
        }
    }

    if (row_buffer_attack_array_index[DESIRED_NUM_THREAD - 1] == NULL) {
        printf("Memory attack: Unable to allocate memory");
        return EXIT_FAILURE;
    }

    /* Initialize the iteration count */
    iteration_count = 0;

    last_timing = 0;

    /* Online profiling flag */
    on_new_memory_flag = 0;

    return EXIT_SUCCESS;
}

/**
 * @brief Main attack loop of row buffer
 */
int memory_row_buffer_attack() {
    int i, j;

    double scalar = 1.7;  // this is a magic number

    register volatile int *a_array, *b_array, *index_array;

    register unsigned int sum = 0;

    long size = MEM_SIZE;

    double elapsedTime;

    // printf("Enter row buffer attack. \n");

    // printf("row buffer : %d \n", row_buffer_indicator);
    a_array = row_buffer_attack_array_a[row_buffer_indicator];
    b_array = row_buffer_attack_array_b[row_buffer_indicator];
#ifdef RL_ONLINE
    index_array = row_buffer_attack_array_index[row_buffer_indicator];
#else
    index_array = row_buffer_attack_array_index
        [row_buffer_indicator++];  // Here increment the indicator for the next
                                   // thread
#endif

    /* Attack loop */
#ifdef RL_ONLINE

#ifdef TIMER
    while (shared_memory_action->row_buffer) {
#else
    for (int it = 0; it < ROW_ITERATIONS; it++) {
#endif

#else
    /* For normal mode of PolyRhythm */
    while (row_buffer_flag) {
#endif

        int offset = 80;
        int jump =
            rand() % offset + offset;  // Generate random number from 80 to 160

        for (i = 0; i < (ram_mem_size / sizeof(int)) - jump;
             i += jump)  // accelerate the loop
        {
            a_array[index_array[i]] = b_array[index_array[i]];

            // b_array[index_array[i]] = a_array[index_array[i]];
        }

        for (j = 0; j < ram_mem_size / sizeof(int); j++) {
            b_array[index_array[j]] = a_array[index_array[j]];
            // b_array[index_array[j]] = 0xff; // Different access pattern
        }

        /* count the cache loop, less count means more cache contention */
        row_buffer_contention_count++;
    }

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action
    // sched_next_tasks();

    /*  Don't need to release if we launch memory attack later

    free((void *)a_array);
    free((void *)b_array);
    free((void *)index_array);
*/
    return EXIT_SUCCESS;
}

/**
 * @brief May need to point the thread back to the allocated memory
 */
int row_buffer_attack_reset_if_necessary() {
    if (row_buffer_indicator == DESIRED_NUM_THREAD - 1) {
        row_buffer_indicator = 0;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Online remap / re-allocated a slice of memory
 */
int online_profiling_row_buffer_remap_memory() {
    printf("Memory re-allocate. \n");

    /* Record last memories*/
    last_row_buffer_attack_array_a[row_buffer_indicator] =
        row_buffer_attack_array_a[row_buffer_indicator];
    last_row_buffer_attack_array_b[row_buffer_indicator] =
        row_buffer_attack_array_b[row_buffer_indicator];
    last_row_buffer_attack_array_index[row_buffer_indicator] =
        row_buffer_attack_array_index[row_buffer_indicator];

    /* Allocation new memory */
    row_buffer_attack_array_a[row_buffer_indicator] =
        (int *)malloc(ram_mem_size);
    row_buffer_attack_array_b[row_buffer_indicator] =
        (int *)malloc(ram_mem_size);
    row_buffer_attack_array_index[row_buffer_indicator] =
        (int *)malloc(ram_mem_size);

    /* One more time of contention region remap */
    iteration_count++;

    /* Switch to new allocated memory */
    on_new_memory_flag = 1;
}

/**
 * @brief Switch back to the previously allocated memory
 */
int online_profiling_row_buffer_memory_switch_back() {
    row_buffer_attack_array_a[row_buffer_indicator] =
        last_row_buffer_attack_array_a[row_buffer_indicator];
    row_buffer_attack_array_b[row_buffer_indicator] =
        last_row_buffer_attack_array_b[row_buffer_indicator];
    row_buffer_attack_array_index[row_buffer_indicator] =
        last_row_buffer_attack_array_index[row_buffer_indicator];

    /* May need to free the unused memory */

}

/**
 * @brief Main loop of row buffer attack with online profiling
 */
int online_profiling_memory_row_buffer_attack() {
    int i, j;

    double scalar = 1.7;  // this is a magic number

    register volatile int *a_array, *b_array, *index_array;

    register unsigned int sum = 0;

    long size = MEM_SIZE;

    a_array = row_buffer_attack_array_a[row_buffer_indicator];
    b_array = row_buffer_attack_array_b[row_buffer_indicator];
    index_array = row_buffer_attack_array_index[row_buffer_indicator];

    /* Profiling Loop */

    // We calculate the least contended region after a fix number of loops
    int tmp_count = 0;
    long int timing = 0;
    while (row_buffer_flag) {
        long start = get_current_time_us();

        int offset = 80;
        int jump =
            rand() % offset + offset;  // Generate random number from 80 to 160

        for (i = 0; i < (ram_mem_size / sizeof(int)) - jump;
             i += jump)  // accelerate the loop
        {
            a_array[index_array[i]] = b_array[index_array[i]];
        }

        for (j = 0; j < ram_mem_size / sizeof(int); j++) {
            b_array[index_array[j]] = a_array[index_array[j]];
        }

        long end = get_current_time_us();

        timing += (end - start);

        if (tmp_count == 500) {
            /* Calculate the least contended region */
            if (last_timing == 0) {
                last_timing = timing;
                continue;
            }

            /* Check if the current if better than last */
            if (on_new_memory_flag) {
                if (timing < last_timing) {
                    online_profiling_row_buffer_memory_switch_back();
                } else {
                    /* We can re-allocate memory now */
                    on_new_memory_flag = 0;
                }
            } else { /* Re-allocate new memory */
                online_profiling_row_buffer_remap_memory();
            }

            last_timing = timing;
            timing = 0;
            tmp_count = 0;
        } else {
            tmp_count++;
        }

        /* count the row buffer loop, less count means more contention */
        row_buffer_contention_count++;

        /* Once the remap times reach the maximum number
         * jump to the attack loop without any time recording
         */

        /* End of profiling, go to attack loop */
        if (iteration_count > TIMES_REMAP) {
            goto no_profiling;
        }
    }

no_profiling:

    printf("Entering the attack loop \n");
    /* Attack loop */
    /* With less if else predicate, this attack loop is more effective */
    while (row_buffer_flag) {
        int offset = 80;
        int jump =
            rand() % offset + offset;  // Generate random number from 80 to 160

        for (i = 0; i < (ram_mem_size / sizeof(int)) - jump;
             i += jump)  // accelerate the loop
        {
            a_array[index_array[i]] = b_array[index_array[i]];
        }

        for (j = 0; j < ram_mem_size / sizeof(int); j++) {
            b_array[index_array[j]] = a_array[index_array[j]];
        }

        /* Count the cache loop, less count means more cache contention */
        row_buffer_contention_count++;
    }

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action
    // sched_next_tasks();

    /*  Don't need to release if we launch memory attack later

    free((void *)a_array);
    free((void *)b_array);
    free((void *)index_array);
*/
    return EXIT_SUCCESS;
}