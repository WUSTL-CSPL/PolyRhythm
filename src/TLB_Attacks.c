
#include <errno.h>
#include <time.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

/* tlb attack */
#include <sys/mman.h>

// If we do not use while loop
#define TLB_ITERATIONS 50

/* All extern trigger flags */
extern int tlb_flag;
extern struct action *shared_memory_action;

/* feedbacks for RL, defined in PolyRhythm_RL.c */

extern unsigned long int tlb_contention_count;

/*************************************
 * Parameters for TLB attack
 * ***********************************
 */

/* Global Variables */
static int num_pages = 0;
static int log_flag;

/**
 * @brief Initialize TLB attack channels,
 * this attack is effective if hyper-threading is enabled.
 * @param:
 * 0: number of pages.
 */
int init_tlb_attack(void *arguments) {
    int *args = (int *)arguments;
    num_pages = args[0];

    return EXIT_SUCCESS;
}

/**
 * @brief Main TLB attack loop.
 */
int tlb_attack() {
    const size_t mmap_size = PAGE_SIZE * num_pages;

#ifdef RL_ONLINE

#ifdef TIMER
    while (shared_memory_action->tlb) {
#else
    for (int it = 0; it < TLB_ITERATIONS; it++) {
#endif

#else
    /* For normal mode of PolyRhythm */
    while (tlb_flag) {
#endif

        uint8_t *mem, *ptr;

        /* Create large memory chunks */
        for (;;) {
            mem = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);

            if ((void *)mem == MAP_FAILED) {
                if ((errno == EAGAIN) || (errno == ENOMEM) ||
                    (errno == ENFILE)) {
                    printf("TLB: failed \n");
                } else {
                    printf("TLB attack: mmap failed, errno=%d (%s)\n", errno,
                           strerror(errno));
                }
            } else {
                // printf("TLB: Success \n");
                break;
            }
        }

        (void)memset(mem, 0, mmap_size);

        char buffer[PAGE_SIZE];  // data trampoline

        for (ptr = mem; ptr < mem + mmap_size; ptr += PAGE_SIZE) {
            /* Force tlb shoot down on page */
            (void)mprotect(ptr, PAGE_SIZE, PROT_READ);
            (void)memcpy(buffer, ptr, PAGE_SIZE);
            (void)munmap(ptr, PAGE_SIZE);
        }

        // printf("Debug \n");
        (void)munmap(mem, mmap_size);

        /* Count the TLB loop, less count means more cache contention */
        tlb_contention_count++;
    }

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action
    // sched_next_tasks();

    /*  Don't need to close the socket if we launch TLB attack later */
    //    close(fd);

    return 0;
}