#pragma once

#include "PolyRhythm.h"

/*
 *  Parameters for cache
 */
#define KB ((1) << 10)
#define CACHE_LINE 64
#define LLC_CACHE_SIZE 512 * KB  // 512 for pi3, 6144 for AMD

/*
 *  Parameters for TLB and Disk IO attacks
 */

#define PAGE_SIZE (4 * KB)
#define MMAP_PAGES (512)

/* Attack action buffer */
struct action {
    int cache;
    int network;
    int row_buffer;
    int disk;
    int tlb;
};

/* Architecture-level contention channels */

/* Cache attack */

/* This function is currently deprecated,
 * we mutate cache parameter in Genetic Algorithm's python script
 */
void cache_mutation(int *stride, int *mem_size);

int init_cache_attack(void *arguments);

int cache_attack_reset_if_necessary();

int cache_attack();

int init_online_profiling_cache_attack(void *arguments);

int online_profiling_cache_remap_memory(
    int **mem, int min_index);  // For online contention region profiling

int switch_back_memory_region(int **mem);

int online_profiling_cache_attack();

/* TLB attack */
int init_tlb_attack(void *arguments);

int tlb_attack();

/* Memory row buffer attack */
int init_memory_row_buffer_attack(void *arguments);

int row_buffer_attack_reset_if_necessary();

int memory_row_buffer_attack();

/* Allocate new memory */
int online_profiling_row_buffer_remap_memory();

/* Switch back to the last memory */
int online_profiling_row_buffer_memory_switch_back();

int online_profiling_memory_row_buffer_attack();

int disk_io_attack();

/** OS-level Contention Channels **/

/* Network attack */

#define MAX_PORTS 256
#define MAX_PROC_NET_LINE 256  // Lines should not exceed 256 characters
#define DEBUG 0

/* Errors when profiling the open ports */
enum GET_PORTS_ERROR {
    SUCCESS,
    ERR_OPENING,
    ERR_FILE_EMPTY,
    ERR_PORTS_EXCEEDED,
    ERR_PARSING
};

/* Read ports from linux file sytems*/
int read_ports(FILE *f, int *open_ports, int *num_open_ports);

int intcmp(const void *a, const void *b);

/* Profile open ports */
int get_open_ports(int *open_ports, int *num_open_ports);

typedef struct {
    const char *name;
    const int domain;
} domain_t;

/* we only evaluate udp for network IO attack */
int init_udp_attack(void *arguments);

int stress_udp_flood();

int init_online_udp_attack(void *arguments);

int online_profiling_udp_remap_domain(
    int domain);  // For online contention region profiling

int online_profiling_stress_udp_flood();

/* Memory bus attack */

int init_memory_contention_attack(void *arguments);

int memory_contention_attack();

int online_profiling_memory_ops_switch_back();

int online_profiling_memory_ops_remap_memory();

/* File system attack */

int init_filesys_attack(void *arguments);

int filesys_attack();

/* Thread spawning attack */

int init_spawn_attack();

int spawn_attack();

/* Currently, this function is not implemented,
 * we use UDP attack to contend for network I/O
 */
int network_attack();

int pointer_chasing();

/* Disk I/O Attack */

int init_advise_disk_io_attack(void *arguments);

int advise_disk_io_attack();
