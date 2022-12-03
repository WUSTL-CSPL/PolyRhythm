#include "PolyRhythm.h"

#include <pthread.h>
#include <stdbool.h>

#include "Attacks.h"
#include "Utils.h"

/*
 *  Default parameter for different attack primitives
 *  The parameters are overwritten by those defined in
 *  the parameter file passed with the -P command-line argument.
 */
static attack_channel_info_t attack_channels[] = {
    {CLASS_CACHE, "cache", 0, cache_attack, {1, 6244, 1, 0, 1}},
    {CLASS_TLB, "tlb", 0, tlb_attack, {1, 1, 1, 0, 1}},
    {CLASS_FILESYSTEM, "filesystem", 0, filesys_attack, {1, 1, 1, 0, 1}},
    {CLASS_INTERRUPT, "interrupt", 0, cache_attack, {1, 1, 1, 0, 1}},
    {CLASS_DISK_IO, "disk_io", 0, advise_disk_io_attack, {1, 1, 1, 0, 1}},
    {CLASS_MEMORY,
     "row_buffer",
     0,
     memory_row_buffer_attack,
     {1, 100, 1, 0, 1}},
    {CLASS_NETWORK, "network", 0, stress_udp_flood, {1, 1, 1, 0, 1}},
    {CLASS_OS, "memory", 0, memory_contention_attack, {1, 1, 1, 0, 1}},
    {CLASS_SCHEDULER, "scheduler", 0, cache_attack, {1, 1, 1, 0, 1}},
    {CLASS_SPAWN, "spawn", 0, spawn_attack, {1, 1, 1, 0, 1}},
    {CLASS_PTR_CHASING, "ptr_chasing", 0, pointer_chasing, {1, 1, 1, 0, 1}},
};

/*
 * These statistics below are feedbacks for Reinforcement learning
 * The basic ideas is from profiling in eviction set estimation
 * Cache contention count
 * Row buffer contention count
 * Network contention count
 * Disk contention count
 *
 */

unsigned long int cache_contention_count = 0;
unsigned long int row_buffer_contention_count = 0;
unsigned long int network_contention_count = 0;
unsigned long int diskio_contention_count = 0;
unsigned long int tlb_contention_count = 0;

/* Flag to trigger online profiling */
static int flag_online_profiling = 0;

/* We define a global attack list here */
static int attack_primitive_index = 0;

// store the address of primitive funcs
static int (*attack_primitives_func_list[20])();

// primitive name list
char *attack_primitives_name_list[20];

/*
 * # of thread need to be launch for each resource channel
 */
int total_num_threads = 0;
int cache_num_threads = 0;
int network_num_threads = 0;
int row_buffer_num_threads = 0;
int tlb_num_threads = 0;
int spawn_num_threads = 0;
int mem_ops_num_threads = 0;
int advise_disk_num_threads = 0;

/* flag to trigger the primitive tasks */
/* To stop an attack thread without killing it, we use flag to stop */
int cache_flag = 1;
int memory_flag = 1;
int row_buffer_flag = 1;
int tlb_flag = 1;
int udp_flag = 1;
int disk_flag = 1;
int context_switch_flag = 1;
int memory_ops_flag = 1;

void disable_all_flags() {
    cache_flag = 0;
    cache_attack_reset_if_necessary();
    memory_flag = 0;
    row_buffer_flag = 0;
    row_buffer_attack_reset_if_necessary();
    tlb_flag = 0;
    udp_flag = 0;
}

void *sched_next_tasks(int signal) {
    /* schedule the next task */
    if (cache_num_threads-- > 0) {
        if (flag_online_profiling) {
            online_profiling_cache_attack();
        } else {
            cache_attack();
        }
    } else if (network_num_threads-- > 0) {
        if (flag_online_profiling) {
            online_profiling_stress_udp_flood();
        } else {
            stress_udp_flood();
        }
        udp_flag = 1;
    } else if (row_buffer_num_threads-- > 0) {
        if (flag_online_profiling) {
            online_profiling_memory_row_buffer_attack();
        } else {
            memory_row_buffer_attack();
        }
        row_buffer_flag = 1;
    } else if (tlb_num_threads-- > 0) {
        tlb_flag = 1;
        tlb_attack();
    } else if (spawn_num_threads-- > 0) {
        spawn_attack();
    } else if (mem_ops_num_threads-- > 0) {
        memory_flag = 1;
        memory_contention_attack();
    } else if (advise_disk_num_threads-- > 0) {
        disk_flag = 1;
        advise_disk_io_attack();
    }
}

bool first_flag = true;
void terminate_and_print_states() {
    if (first_flag) {
        first_flag = false;
        return;
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    int ret;

    ret = parse_options(argc, argv, attack_channels);

    // parsing command failed
    if (ret < 0) {
        printf("Parse option error! \n");
        return EXIT_FAILURE;
    }

    /* Iterate the options --> Launch the attacks */
    int i = 0;
    attack_channel_info_t *iter = &attack_channels[0];
    while (iter->name != NULL) {
        i++;

        if (iter->num_threads == 0) {
            iter = &attack_channels[i];
            // temporal solution
            if (i > 10) {
                break;
            }
            continue;
        }
        /* Store the number of threads to launch */

        /* The parameter 5 is a general parameter,
         * which enables online profiling
         */
        flag_online_profiling = iter->attack_paras[NUM_PARAMS];

        if (strcmp(iter->name, "cache") == 0) {
            cache_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            if (flag_online_profiling) {
                printf("Init online profiling \n");
                init_online_profiling_cache_attack(&iter->attack_paras);
            } else {
                init_cache_attack(&iter->attack_paras);
            }
            
        } else if (strcmp(iter->name, "network") == 0) {
            init_udp_attack(&iter->attack_paras);
            network_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            
        } else if (strcmp(iter->name, "row_buffer") == 0) {
            init_memory_row_buffer_attack(&iter->attack_paras);
            row_buffer_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            
        } else if (strcmp(iter->name, "tlb") == 0) {
            init_tlb_attack(&iter->attack_paras);
            tlb_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            if (tlb_num_threads > 1) {
                printf(
                    "Current TLB attack only support one attack thread per "
                    "instance.\n");
                tlb_num_threads = 1;
            }
            
        } else if (strcmp(iter->name, "spawn") == 0) {
            spawn_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            init_spawn_attack(&iter->attack_paras);
        } else if (strcmp(iter->name, "memory") == 0) {
            init_memory_contention_attack(&iter->attack_paras);
            mem_ops_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
            
        } else if (strcmp(iter->name, "disk_io") == 0) {
            // printf("Init disk IO attack. \n");
            init_advise_disk_io_attack(&iter->attack_paras);
            advise_disk_num_threads = iter->num_threads;
            total_num_threads += iter->num_threads;
        }

        // Attack channels are all set
        if (i > NUM_CHANNELS) {
            break;
        }

        /* Push the funcs into waitlists */
        attack_primitives_name_list[attack_primitive_index] = iter->name;
        attack_primitives_func_list[attack_primitive_index++] =
            iter->attack_func;
        iter = &attack_channels[i];
    }

    /* If only one attack thread, launch in main thread */
    if(total_num_threads == 0) {
        printf("Must specify at least 1 attack thread!\n");
        return EXIT_FAILURE;
    }
    
    else if(total_num_threads == 1) {
    	sched_next_tasks(0);
    }
    
    /* Otherwise, call the sched_next_task N times,
       N is the total number of attack threads */
    else {
        pthread_t thread_id[10];
        int call_times;

        /* Launch the attack threads */
        // Should set the num of threads to launch
        int dummy_signal;

        for (call_times = 0; call_times < 5; call_times++) {
            pthread_create(&thread_id[call_times], NULL, (void *)sched_next_tasks,
                           &dummy_signal);
        }

        /* Spin the main process*/
        pthread_join(thread_id[0], NULL);
    }
    
    return EXIT_SUCCESS;
}
