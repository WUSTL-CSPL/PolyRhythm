#pragma once

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NUM_CHANNELS 10
#define NUM_PARAMS 4  // The maximum number of params used by a channel

/* In PolyRhythm's third phase (reinforcement learning),
 * Attack process (AP) uses shared memory to communicate with RL Model
 * Attack process is in C
 * RL Model is in Pythons
 * There are two channels: 1. state: from AP to RL; 2. action from RL to AP
 */
/* For the shared memory communication channel */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

int shmid_action;
int shmid_state;
// give your shared memory an id, anything will do
#define KEY_ACTION 666644  // Magic number
#define KEY_STATE 666688   // Magic number
// char *shared_memory_action;
// char *shared_memory_state;

/* End of communication channels */

// #include "Utils.h"

#define NUM_CORES 4  //  number of logical cores
#define DESIRED_NUM_THREAD (NUM_CORES - 1)

#define DURATION_ACTION 300000  // the duration of an attack action

typedef unsigned long long int ul;

/* Attack Channels */
#define CLASS_CACHE 0   /* CPU cache */
#define CLASS_NETWORK 1 /* Network, sockets, etc */
#define CLASS_MEMORY 2  /* Memory thrashers */
#define CLASS_DISK_IO 3 /* Disk I/O read/writes etc */
// #define CLASS_IO 4            /* I/O read/writes etc */
#define CLASS_TLB 5           /* Translation Lookaside Buffer */
#define CLASS_VM 6            /* VM stress, big memory, swapping */
#define CLASS_INTERRUPT 7     /* interrupt floods */
#define CLASS_OS 8            /* generic OS tests */
#define CLASS_PIPE_IO 9       /* pipe I/O */
#define CLASS_FILESYSTEM 10   /* file system */
#define CLASS_DEV 11          /* device (null, zero, etc) */
#define CLASS_SPAWN 12        /* POSIX thread spawn */
#define CLASS_PATHOLOGICAL 13 /* can hang a machine */
#define CLASS_SCHEDULER 14    /* Context Switching */
#define CLASS_PTR_CHASING 15  /* Pointer chasing */

typedef unsigned int attack_channel_t;

/* PolyRhythm attack vectors */
typedef struct attack_channel_info {
    attack_channel_t attack_channel; /* Class type bit mask */
    char *name;                      /* Name of class */
    int num_threads;                 /* Number of threads need to be launch */
    int (*attack_func)();            /* Main attack function */

    /*
        Attack parameters
        All attacks use this array to pass arguments
        For cache attack, 0 is the stride, 1 is the mem_size, 2 and 3 are null
        NUM_PARAMS+1 is used because the last parameter is a general parameter,
        which is used to indicate if we enable online contention region
       searching
    */
    int attack_paras[NUM_PARAMS + 1];
} attack_channel_info_t;

/* This function is used to switch to different attack channels */
void *sched_next_tasks();

/* Terminate all attacks and print out the states */
void terminate_and_print_states();

/* Get the nextion attack action e.g., cache, row buffer, etc ... */
int get_next_action();

/* Disable all flags -- To stop all current attack action */
void disable_all_flags_rl(int signal);  // For Reinforcement Learning version
void disable_all_flags();               // For normal version

/* Initialize shared memory to communicate with RL */
int init_shared_memory();

/* This is main loop for RL */
void main_attack_loop();

/* Write states into shared memory */
int write_states();

/* Write states into shared memory, states are presented by counts */
int write_states_count();
