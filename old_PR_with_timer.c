#include <pthread.h>
#include <stdbool.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

/* In PolyRhythm's third phase (reinforcement learning),
 * Attack process (AP) uses shared memory to communicate with RL Model
 * Attack process is in C
 * RL Model is in Pythons
 * There are two channels: 1. state: from AP to RL; 2. action from RL to AP
 */
/* For the shared memory communication channel */

int shmid_action;
int shmid_state;
// give your shared memory an id, anything will do
key_t key_action = 666644;  // Magic number
key_t key_state = 666688;   // Magic number
char *shared_memory_action;
char *shared_memory_state;
#define LEN_STATE_STRING 128

/*
 *  Default parameter for different attack primitives
 *  To test Reinforcement learning, you may fill here the optimal parameters
 * tuned by GA
 */
static attack_channel_info_t attack_channels[] = {
    {CLASS_CACHE, "cache", 0, cache_attack, {1, 6244, 1, 0}},
    {CLASS_TLB, "tlb", 0, tlb_attack, {500, 1, 1, 0}},
    {CLASS_FILESYSTEM, "filesystem", 0, filesys_attack, {1, 1, 1, 0}},
    {CLASS_INTERRUPT,
     "interrupt",
     0,
     cache_attack,
     {1, 1, 1, 0}},  
    {CLASS_DISK_IO, "adv_disk_io", 0, advise_disk_io_attack, {50, 56223, 1, 0}},
    {CLASS_MEMORY, "memory", 0, memory_row_buffer_attack, {1, 10, 1, 0}},
    {CLASS_NETWORK, "network", 0, stress_udp_flood, {65333, 2, 1, 0}},
    {CLASS_OS, "memory_ops", 0, memory_contention_attack, {1, 1, 1, 0}},
    {CLASS_SCHEDULER,
     "scheduler",
     0,
     cache_attack,
     {1, 1, 1, 0}}, 
    {CLASS_SPAWN, "spawn", 0, spawn_attack, {1, 1, 1, 0}},
    {CLASS_PTR_CHASING, "ptr_chasing", 0, pointer_chasing, {1, 1, 1, 0}},
};

/*
 * This program terminates all attack and print out the states
 * The states will be fed back to the RL model

 */

/*
 * These statistics below are feedbacks for Reinforcement learning
 * The basic ideas is from profiling in eviction set estimation
 * Cache contention count
 * Row buffer contention count
 * Network contention count
 * Disk contention count
 *
 * We may also use time to measure the contention.
 */

unsigned long int cache_contention_count = 0;
unsigned long int row_buffer_contention_count = 0;
unsigned long int network_contention_count = 0;
unsigned long int diskio_contention_count = 0;
unsigned long int tlb_contention_count = 0;

/* We define a global attack list here */

static int attack_primitive_index = 0;

// Store the address of primitive funcs
static int (*attack_primitives_func_list[20])();

// primitive name list
char *attack_primitives_name_list[20];
// int flag = 1;

/*
 * # of thread need to be launch for each resource channel
 */

int cache_num_threads = 0;
int network_num_threads = 0;
int row_buffer_num_threads = 0;
int tlb_num_threads = 0;
int spawn_num_threads = 0;
int mem_ops_num_threads = 0;
int advise_disk_num_threads = 0;

/* Flag to trigger the primitive tasks */
/* To stop an attack thread without killing it, we use flag to stop */
int cache_flag = 1;
int memory_flag = 1;
int row_buffer_flag = 1;
int tlb_flag = 1;
int udp_flag = 1;
int disk_flag = 1;

/* Flag to trigger online profiling */
static int flag_online_profiling = 0;

/**
 * @brief Translate action to threads
 * --------------------
 * increment the number of threads according to the action:
 * e.g. action = 2 which is  CLASS_CACHE, we increment cache_num_threads
 * meaning that next action we launch cache attack thread.
 *
 * @return: void
 *
 */

void action_to_num_threads(int action) {
    switch (action) {
        case CLASS_CACHE:  // == 0
            cache_num_threads++;
            break;
        case CLASS_NETWORK:  // == 1
            network_num_threads++;
            break;
        case CLASS_MEMORY:  // == 2
            row_buffer_num_threads++;
            break;
        case CLASS_DISK_IO:  // == 3
            advise_disk_num_threads++;
            break;
        case CLASS_TLB:  // == 5
            tlb_num_threads++;
            break;

        default:
            printf("No action selected \n");
            break;
    }
}

/**
 * @brief Write contention states into shared memory
 */

int write_states() {
    char *num;
    /* Construct the states information */
    char buffer[LEN_STATE_STRING];

    if (asprintf(&num, "%ld", cache_contention_count) == -1) {
        perror("asprintf error");
    } else {
        strcat(strcpy(buffer, "cache:"), num);
    }

    if (asprintf(&num, "%ld", row_buffer_contention_count) == -1) {
        perror("asprintf error");
    } else {
        strcat(buffer, ",row:");  // TODO change to rowbuffer
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", network_contention_count) == -1) {
        perror("asprintf error");
    } else {
        strcat(buffer, ",net:");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", diskio_contention_count) == -1) {
        perror("asprintf error");
    } else {
        strcat(buffer, ",disk:");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", tlb_contention_count) == -1) {
        perror("asprintf error");
    } else {
        strcat(buffer, ",tlb:");
        strcat(buffer, num);
        strcat(buffer, ",");
    }

    /* Write states into shared memory */
    memcpy(shared_memory_state, buffer, strlen(buffer));
    // printf("States in shared memory: %s \n", shared_memory_state);
}

/**
 * @brief Reset all contention states
 */

int reset_states() {
    /* Reset all contention count */
    cache_contention_count = 0;
    row_buffer_contention_count = 0;
    network_contention_count = 0;
    diskio_contention_count = 0;
    tlb_contention_count = 0;
    /* End of reset all contention count */
    return EXIT_SUCCESS;
}

/**
 * @brief 1. Stop all attack action,
 * 2. Store the states into shared memory,
 * 3. Read actions from shared memory.
 */

/* Global Variable */
#define MAX_NUM_ACTIONS 5
static int action_selected = 0;
int action_list[MAX_NUM_ACTIONS];
int action_index;
void disable_all_flags_rl(int signal) {
    // printf("Hello! Interrupt Signal \n");

    /* Stop cache attack */
    cache_flag = 0;
    cache_attack_reset_if_necessary();
    cache_num_threads = 0;
    /* Stop memory attack */
    row_buffer_flag = 0;
    row_buffer_attack_reset_if_necessary();
    row_buffer_num_threads = 0;
    /* Stop UDP attack */
    udp_flag = 0;
    network_num_threads = 0;
    /* Stop Disk I/O attack */
    disk_flag = 0;
    advise_disk_num_threads = 0;
    /* Stop TLB attack */
    tlb_flag = 0;
    tlb_num_threads = 0;

    // printf("End of interrupt function, \n");
}

/**
 * @brief Read shared action memory
 */

int read_shared_action_memory() {
    /* If there is still action in the list */
    if (action_index == 5) {
        /* Read from shared memory */
        // State should be already in 'shared_memory_action'

        char **actions;
        int *int_actions;
        /* Parse action list */

        do {
            // actions = str_split(shared_memory_action, ',');
            int_actions = str_split_to_nums(shared_memory_action, ',');
        } while (int_actions == NULL);

        int i;

        for (i = 0; i < MAX_NUM_ACTIONS; i++) {
            // action_list[i] = atoi(actions[i]);
            action_list[i] = int_actions[i];
            // free(*(actions + i));
        }

        /* Write contention states into shared memory */
        write_states();

        /* Reset contention states */
        reset_states();
        /* Reset the index */
        action_index = 0;
    }

    int next_action = action_list[action_index];
    action_index++;

    // printf("next_action : %d \n", next_action);

    /* According action, imcrement the # of attack thread,
     * which translate to the next attack action.
     */
    action_to_num_threads(next_action);
    action_selected = 1;
}

/**
 * @brief Switch to the next action
 */
void *sched_next_tasks(int signal) {
    /* thread pool pauses the current tasks
            and then schedule the next tasks in the queue */

start:

    read_shared_action_memory();

    // while (!action_selected)
    // { // This loop waits if the
    // 	usleep(100);
    // }
    // action_selected = 0;

    /* schedule the next task */
    if (cache_num_threads > 0) {
        cache_flag = 1;
        cache_num_threads--;
        // printf("!!! entering cache attack \n");
        cache_attack();
    } else if (network_num_threads > 0) {
        udp_flag = 1;
        network_num_threads--;
        // printf("!!! entering network attack \n");
        stress_udp_flood();
    } else if (row_buffer_num_threads > 0) {
        row_buffer_flag = 1;
        row_buffer_num_threads--;
        // printf("!!! entering row buffer attack \n");
        memory_row_buffer_attack();
    } else if (tlb_num_threads > 0) {
        tlb_flag = 1;
        tlb_num_threads--;
        // printf("!!! entering tlb attack \n");
        tlb_attack();
    } else if (spawn_num_threads > 0) {
        spawn_num_threads--;
        spawn_attack();
    } else if (mem_ops_num_threads > 0) {
        memory_flag = 1;
        mem_ops_num_threads--;
        memory_contention_attack();
    } else if (advise_disk_num_threads > 0) {
        disk_flag = 1;
        advise_disk_num_threads--;
        advise_disk_io_attack();
    } else {
        printf("No threads to launch \n");
    }

    goto start;
}

int main(int argc, char *argv[]) {
    /***********  Parse arguments ***********/
    int ret;

    ret = parse_options(argc, argv, attack_channels);

    // parsing command failed
    if (ret < 0) {
        printf("Parse option error! \n");
        return EXIT_FAILURE;
    }

    // print_options(attack_channels);
    /*********** End of Parse arguments ***********/

    /*********** Set up shared memory ***********/
    // Setup shared memory, 64 is the size
    if ((shmid_state = shmget(KEY_STATE, 32, IPC_CREAT | 0666)) < 0) {
        printf("State SHMemory: Error getting shared memory id\n");
        return EXIT_FAILURE;
    }
    // Attached shared memory
    if ((shared_memory_state = shmat(shmid_state, NULL, 0)) == (char *)-1) {
        printf("(State) Error attaching shared memory id\n");
        return EXIT_FAILURE;
    }

    write_states();
    printf("Initialized states \n");

    /* Again, for action shared memory */
    if ((shmid_action = shmget(KEY_ACTION, 32, IPC_CREAT | 0666)) < 0) {
        printf("Action SHMemory: Error getting shared memory id\n");
        return EXIT_FAILURE;
    }
    if ((shared_memory_action = shmat(shmid_action, NULL, 0)) == (char *)-1) {
        printf("(Action) Error attaching shared memory id\n");
        return EXIT_FAILURE;
    }

    /** End of Set up shared memory **/

    /* Iterate the options --> Launch the attacks */
    int i = 0;
    attack_channel_info_t *iter = &attack_channels[0];
    while (iter->name != NULL) {
        i++;

        /* Store the number of threads to launch */

        // printf("Channel name: %s\n", iter->name);

        if (strcmp(iter->name, "cache") == 0) {
            flag_online_profiling = iter->attack_paras[NUM_PARAMS];

            if (flag_online_profiling) {
                init_online_profiling_cache_attack(&iter->attack_paras);
            } else {
                init_cache_attack(&iter->attack_paras);
            }
            cache_num_threads = iter->num_threads;
        } else if (strcmp(iter->name, "network") == 0) {
            init_udp_attack(&iter->attack_paras);
            network_num_threads = iter->num_threads;
            // printf("Init network attacks\n");
        } else if (strcmp(iter->name, "memory") == 0) {
            init_memory_row_buffer_attack(&iter->attack_paras);
            row_buffer_num_threads = iter->num_threads;
            // printf("Init row buffer attacks\n");
        } else if (strcmp(iter->name, "tlb") == 0) {
            init_tlb_attack(&iter->attack_paras);
            tlb_num_threads = iter->num_threads;
        } else if (strcmp(iter->name, "spawn") == 0) {
            spawn_num_threads = iter->num_threads;
        } else if (strcmp(iter->name, "memory_ops") == 0) {
            init_memory_contention_attack(&iter->attack_paras);
            mem_ops_num_threads = iter->num_threads;
        } else if (strcmp(iter->name, "adv_disk_io") == 0) {
            init_advise_disk_io_attack(&iter->attack_paras);
            advise_disk_num_threads = iter->num_threads;
        }

        // Attack channels are all set
        if (i > NUM_CHANNELS) {
            break;
        }

        /* push the funcs into waitlists */
        // while (iter->num_threads > 0) {
        attack_primitives_name_list[attack_primitive_index] = iter->name;
        attack_primitives_func_list[attack_primitive_index++] =
            iter->attack_func;
        iter = &attack_channels[i];
    }

    /** Set a timer to terminate all attacks and feedback to RL **/
    /* set the timer that triggers the tasks scheduling */
    struct sigaction sa;
    struct itimerval timer;
    // Set the callback function
    sa.sa_handler = (void *)disable_all_flags_rl;
    // sigaction (signal to process, handler, original handler)
    sigaction(SIGALRM, &sa, NULL);

    // Set timer intial value
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    // Set the time interval as 100 ms
    // Each attack primitive lasts for 100 ms
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 300 * 1000;

    // Launch the timer
    setitimer(ITIMER_REAL, &timer, NULL);

    /* Launch the attack threads */
    int dummy_signal;

    sched_next_tasks(
        dummy_signal);  // Here we need a signal for some historical reasons

    return EXIT_SUCCESS;
}

/* save calling environment for longjmo */
// int val = setjmp(schd_buffer);

// longjmp(schd_buffer, "OoO");
