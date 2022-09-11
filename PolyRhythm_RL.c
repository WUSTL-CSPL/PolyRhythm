#define _GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

/* In PolyRhythm's third phase (reinforcement learning),
 * Attack process (AP) uses shared memory to communicate with RL Model
 * Attack process is in C
 * RL Model is in Pythons
 * There are two communication channels: 1. state: from AP to RL; 2. action from
 * RL to AP
 */

/* For the shared memory communication channel */

int shmid_action;
int shmid_state;
// give your shared memory an id, anything will do
key_t key_action = 666644;  // Magic number
key_t key_state = 666688;   // Magic number
char *shared_memory_action_string;
struct action *shared_memory_action;
char *shared_memory_state;
#define LEN_STATE_STRING 128
#define LEN_PARAM_STRING \
    64  // Parameter file line should not exceed 64 characters

/*
 *  Default parameter for different attack primitives
 *  The parameters are overwritten by those defined in
 *  the parameter file passed with the -P command-line argument.
 */
static attack_channel_info_t attack_channels[] = {
    {CLASS_CACHE, "cache", 0, cache_attack, {1, 6244, 1, 0, 0}},
    {CLASS_TLB, "tlb", 0, tlb_attack, {500, 1, 1, 0, 0}},
    {CLASS_FILESYSTEM, "filesystem", 0, filesys_attack, {1, 1, 1, 0, 0}},
    {CLASS_INTERRUPT, "interrupt", 0, cache_attack, {1, 1, 1, 0, 0}},
    {CLASS_DISK_IO,
     "adv_disk_io",
     0,
     advise_disk_io_attack,
     {50, 56223, 1, 0, 0}},
    {CLASS_MEMORY, "memory", 0, memory_row_buffer_attack, {1, 10, 1, 0, 0}},
    {CLASS_NETWORK, "network", 0, stress_udp_flood, {65333, 2, 1, 0, 0}},
    {CLASS_OS, "memory_ops", 0, memory_contention_attack, {1, 1, 1, 0, 0}},
    {CLASS_SCHEDULER, "scheduler", 0, cache_attack, {1, 1, 1, 0, 0}},
    {CLASS_SPAWN, "spawn", 0, spawn_attack, {1, 1, 1, 0, 0}},
    {CLASS_PTR_CHASING, "ptr_chasing", 0, pointer_chasing, {1, 1, 1, 0, 0}},
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
unsigned long int cache_exe_time = 0;
unsigned long int row_buffer_contention_count = 0;
unsigned long int network_contention_count = 0;
unsigned long int network_exe_time = 0;
unsigned long int diskio_contention_count = 0;
unsigned long int tlb_contention_count = 0;

/* Some global variable for action */
#define MAX_NUM_ACTIONS 5
static int action_selected = 0;
int action_list[MAX_NUM_ACTIONS];
double exe_delays[MAX_NUM_ACTIONS];
int action_index;
long begin_time = 0;
long end_time = 0;

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
int context_switch_flag = 1;
int memory_ops_flag = 1;

/* Flag to trigger online profiling */
static int flag_online_profiling = 0;

/* Flag to trigger writing states */
static int flag_write_states = 0;

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
 * @brief Write contention states into shared memory (Execution Time)
 */

int write_states() {
    int i;
    char *num;
    /* Construct the states information */
    char buffer[LEN_STATE_STRING];
    buffer[0] = '\0';
    // buffer[1] = ':';
    for (i = 0; i < MAX_NUM_ACTIONS; i++) {
        if (asprintf(&num, "%.2f", exe_delays[i]) == -1) {
            perror("asprintf error");
        } else {
            strcat(buffer, ",");
            strcat(buffer, num);
        }
    }
    /* Add a comma at the end for better spliting */
    strcat(buffer, ",");

    /* Write states into shared memory */
    memcpy(shared_memory_state, buffer, strlen(buffer));
    // printf("States in shared memory: %s \n", shared_memory_state);
}

/**
 * @brief Write contention states into shared memory (Count)
 */

int write_states_count() {
    int i;
    char *num;
    /* Construct the states information */
    char buffer[LEN_STATE_STRING];
    buffer[0] = '\0';
    // buffer[1] = ':';

    if (asprintf(&num, "%ld", cache_contention_count / 51313) == -1) {
        printf("asprintf error\n");

    } else {
        strcat(buffer, ",");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", network_contention_count / 587) == -1) {
        printf("asprintf error\n");
    } else {
        strcat(buffer, ",");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", row_buffer_contention_count / 641) == -1) {
        printf("asprintf error\n");
    } else {
        strcat(buffer, ",");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", diskio_contention_count / 23154) == -1) {
        printf("asprintf error\n");
    } else {
        strcat(buffer, ",");
        strcat(buffer, num);
    }

    if (asprintf(&num, "%ld", tlb_contention_count / 47) == -1) {
        printf("asprintf error\n");
    } else {
        strcat(buffer, ",");
        strcat(buffer, num);
    }

    /* Add a comma at the end for better spliting */
    strcat(buffer, ",");

    /* Write states into shared memory */
    memcpy(shared_memory_state, buffer, strlen(buffer));
    // printf("States in shared memory: %s \n", shared_memory_state);

    return EXIT_SUCCESS;
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
 * @brief Get the next attack channel
 */
int get_next_action() {
    /* Calculate the last action elaplsed time */
    end_time = get_current_time_us();
    if (begin_time != 0) {
        /* Store the delay for each action */
        double tmp = (end_time - begin_time) / 5000.0;
        exe_delays[(action_index + 4) % 5] = MIN(5, tmp);
        // printf("Delay : %f \n", exe_delays[(action_index + 4) % 5]);
        // MIN
    }

    /* If there is still action in the list */
    if (action_index == 5) {
        /* Read from shared memory */
        // State should be already in 'shared_memory_action'

        /* The action list is already read */
        if (strlen(shared_memory_action_string) == 1) {
            goto skip_read_action;
        }

        char **actions;
        int *int_actions;
        /* Parse action list */

        do {
            actions = str_split(shared_memory_action_string, ',');

            int_actions = str_split_to_nums(shared_memory_action_string, ',');
        } while (int_actions == NULL);

        int i;

        // printf("Shared action memory after : %s \n",
        // shared_memory_action_string);

        for (i = 0; i < MAX_NUM_ACTIONS; i++) {
            action_list[i] = atoi(actions[i]);
            printf("action [%d] = %d \n", i, int_actions[i]);

            action_list[i] = int_actions[i];

            free(*(actions + i));
        }

    skip_read_action:

        /* Write contention states into shared memory */

        if (flag_write_states) {
            write_states();
        }

        /* Reset contention states */
        reset_states();

        /* Reset the index */
        action_index = 0;
    }

    int next_action = action_list[action_index];
    action_index++;

    begin_time = end_time;
    return next_action;
}

/**
 * @brief Switch to the next action
 */
void *sched_next_tasks(int signal) {
    int next_attack = 0;
start:

    next_attack = get_next_action();

    /* According action, imcrement the # of attack thread,
     * which translate to the next attack action.
     */
    action_to_num_threads(next_attack);

    // while (!action_selected)
    // { // This loop waits if the
    // 	usleep(100);
    // }
    // action_selected = 0;

    /* schedule the next task */
    if (cache_num_threads > 0) {
        cache_flag = 1;
        cache_num_threads--;
        // printf("Entering cache attack. \n");
        cache_attack();
    } else if (network_num_threads > 0) {
        udp_flag = 1;
        network_num_threads--;
        // printf("Entering network attack. \n");
        stress_udp_flood();
    } else if (row_buffer_num_threads > 0) {
        row_buffer_flag = 1;
        row_buffer_num_threads--;
        // printf("Entering row buffer attack. \n");
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
        printf("Idle because no threads to launch. \n");
    }

    goto start;
}

/**
 * @brief Initialize states
 */

int int_states() {
    int i;
    for (i = 0; i < MAX_NUM_ACTIONS; i++) {
        exe_delays[i] = 1.0;
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Initialize shared memory
 */
int init_shared_memory() {
    /* Initial states which are inputs for RL */
    int_states();

    // Setup shared memory, 32 is the size
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
    if ((shmid_action = shmget(KEY_ACTION, 20, IPC_CREAT | 0666)) < 0) {
        printf("Action SHMemory: Error getting shared memory id\n");
        return EXIT_FAILURE;
    }
    if ((shared_memory_action = (struct action *)shmat(
             shmid_action, NULL, 0)) == (struct action *)-1) {
        printf("(Action) Error attaching shared memory id\n");
        return EXIT_FAILURE;
    }

    /* Write an initial action into the shared memory */
    struct action init_action;
    init_action.cache = 1;
    init_action.network = 0;
    init_action.row_buffer = 0;
    init_action.disk = 0;
    init_action.tlb = 0;
    memcpy(shared_memory_action, &init_action, sizeof(struct action));

    return EXIT_SUCCESS;
}

/**
 * @brief Initialize attack channels
 */
int init_all_attack_channels() {
    int i = 0;

    attack_channel_info_t *iter = &attack_channels[0];

    while (iter->name != NULL) {
        i++;

        /* Store the number of threads to launch */
        if (strcmp(iter->name, "cache") == 0) {
            cache_num_threads = iter->num_threads;

            if (flag_online_profiling) {
                init_online_profiling_cache_attack(&iter->attack_paras);
            } else {
                init_cache_attack(&iter->attack_paras);
            }
        } else if (strcmp(iter->name, "network") == 0) {
            init_udp_attack(&iter->attack_paras);
            network_num_threads = iter->num_threads;
            // printf("network attacks\n");
        } else if (strcmp(iter->name, "memory") == 0) {
            init_memory_row_buffer_attack(&iter->attack_paras);
            row_buffer_num_threads = iter->num_threads;
            // printf("init row buffer attacks\n");
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

        attack_primitives_name_list[attack_primitive_index] = iter->name;
        attack_primitives_func_list[attack_primitive_index++] =
            iter->attack_func;
        iter = &attack_channels[i];
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Print attack channel info for debugging
 */
void print_channels() {
    // Number of attack channels
    const int num_attack_channels =
        sizeof(attack_channels) / sizeof(attack_channel_info_t);

    for (int i = 0; i < num_attack_channels; i++) {
        attack_channel_info_t *a = &attack_channels[i];
        printf("%d (%s) -- num_threads: %d -- params:", a->attack_channel,
               a->name, a->num_threads);
        for (int j = 0; j < NUM_PARAMS + 1; j++) {
            printf(" %d", a->attack_paras[j]);
        }
        printf("\n");
    }
}

/**
 * @brief Read parameters for each attack channel
 */
int read_channel_params(FILE *params) {
    // For debugging:
    // print_channels();

    // Number of attack channels
    const int num_attack_channels =
        sizeof(attack_channels) / sizeof(attack_channel_info_t);

    // For parsing parameter file lines
    char line[LEN_PARAM_STRING];
    char *token;
    int null_token = 0;
    const char *sep = ",";

    // Read line-by-line from parameter file
    while (fgets(line, LEN_PARAM_STRING, params)) {
        int i;

        if (null_token) break;

        token = strtok(line, sep);

        // First token is the channel

        // Search for the channel
        for (i = 0; i < num_attack_channels; i++) {
            if (!strcmp(attack_channels[i].name, token)) break;
        }

        // Channel not found
        if (i == num_attack_channels) {
            printf("Channel %s invalid!\n", token);
            return EXIT_FAILURE;
        }

        // Assign subsequent parameter values
        attack_channel_info_t *a = &attack_channels[i];

        for (int j = 0; j < NUM_PARAMS + 1; j++) {
            token = strtok(NULL, sep);
            if (!token) {
                null_token = 1;
                break;
            }
            a->attack_paras[j] = atoi(token);
        }
    }

    if (null_token) {
        printf("The following line does not define 3 parameters!\n%s\n", line);
        return EXIT_FAILURE;
    }

    // For debugging:
    // print_channels();

    return EXIT_SUCCESS;
}

/**
 * @brief Switch to the next action
 */
void main_attack_loop() {
    while (1) {
        cache_attack();
        stress_udp_flood();
        memory_row_buffer_attack();
        advise_disk_io_attack();
        tlb_attack();
        advise_disk_io_attack();

        /* Write the states in to shared memory */
        if (flag_write_states) {
            write_states_count();
            reset_states();
        }
    }
}

int main(int argc, char *argv[]) {
    // while(1) {
    // 	printf("Cache: %d \n", shared_memory_action->cache);
    // 	printf("Network: %d \n", shared_memory_action->network);
    // 	// shared_memory_action->network = 1;
    // 	// shared_memory_action->cache = 1;
    // 	char* s = (char *)shared_memory_action;
    // 	for (int i=0; i<20; i++){
    // 		printf("%c|",s[i]);
    // 	}
    // 	printf("\n");
    // 	// printf("The whole structure: %s\n", s);
    // 	usleep(500000);
    // }

    int ret;

    /***********  Parse arguments ***********/
    int opt;
    FILE *params = NULL;

    while ((opt = getopt(argc, argv, "owP:")) != -1) {
        switch (opt) {
            case 'o':
                flag_online_profiling = 1;
                break;
            case 'w':
                flag_write_states = 1;
                break;
            case 'P':
                params = fopen(optarg, "r");
                if (!params) {
                    printf("Could not open specified parameter file %s\n",
                           optarg);
                    return EXIT_FAILURE;
                }
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s -P file/path [-o] [-w]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    // print_options(attack_channels);
    /*********** End of Parse arguments ***********/

    /*********** Read attack channel parameters ***********/
    ret = read_channel_params(params);
    if (ret != EXIT_SUCCESS) exit(ret);

    /*********** Set up shared memory ***********/
    ret = init_shared_memory();
    if (ret != EXIT_SUCCESS) exit(ret);

    /*********** Initialize attack channels ***********/
    ret = init_all_attack_channels();
    if (ret != EXIT_SUCCESS) exit(ret);

    // Attack channels are all set

    /* Launch the attack threads */
    // int dummy_signal;
    // sched_next_tasks(dummy_signal); // Here we need a signal for some
    // historical reasons
    main_attack_loop();

    return EXIT_SUCCESS;
}

/* save calling environment for longjmp */
// int val = setjmp(schd_buffer);

// longjmp(schd_buffer, "OoO");
