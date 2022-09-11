#include "Utils.h"

/**
 *  @brief Parse the command line
 *  The options format is L
 *  ./polyrythm attack_channel  num_threads para1 para2 para3 para4
 *  e.g. : ./polyrythm cache 2    1    835    0      0
 */
int parse_options(int argc, char *argv[],
                  attack_channel_info_t attack_channels[]) {
    int optind;
    char *tmp_str_end;

    if ((argc - 1) % 6 != 0 &&
        (argc - 1) % 6 !=
            1)  // There is a parameter that enables online profiling
    {
        printf("Parameters errors ! Please follow the pattern: \n");
        printf(
            "./polyrhythm <channel> <num_thread> <para1> <para2> <para3> "
            "<para4> <online_flag> \n");
        return EXIT_FAILURE;
    }

    /* We did not use getopt because we target not only Linux system in the
     * beginning of this project */
    // while ((opt = getopt(argc, argv, "xxxx")) != -1) {

    for (optind = 1; optind < argc; optind += 3 + NUM_PARAMS) {
        int num_threads = strtol(argv[optind + 1], &tmp_str_end, 10);

        // printf("Hello the channel: %s, the number of threads: %d \n",
        // argv[optind], num_threads);

        /* Iterate through channels */
        int i = 0;
        attack_channel_info_t *iter = &attack_channels[0];
        while (iter->name != NULL) {
            if (strcmp(argv[optind], iter->name) == 0) {
                iter->num_threads = num_threads;
                for (int j = 0; j < NUM_PARAMS + 1; j++) {
                    /* Store parameters */
                    iter->attack_paras[j] =
                        strtol(argv[optind + j + 2], &tmp_str_end, 10);
                }
                break;
            }
            i++;

            iter = &attack_channels[i];
        }

        /*
        printf ("%d\n", sizeof(*attack_channels));
        printf ("%d\n", sizeof(attack_channel_info_t));
        for(i = 0; i < sizeof(*attack_channels) / sizeof(attack_channel_info_t);
        i++) { printf ("%d", i); if (strcmp(argv[optind],
        attack_channels[i].name)  == 0) { attack_channels[i].num_threads =
        num_threads;
            }
        }

*/

        /*
        if (strcmp(argv[optind], "cache") == 0) {
            printf("cache~ \n");
        } else if (strcmp(argv[optind], "memory")  == 0) {
            printf("memory~ \n");
        } else if (strcmp(argv[optind], "tlb")  == 0) {
            printf("tlb~ \n");
        } else if (strcmp(argv[optind], "network")  == 0) {
            printf("network~ \n");
        } else if (strcmp(argv[optind], "disk_io")  == 0) {
            printf("disk io~ \n");
        }
*/
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Print the parsed options
 * @attack_channels:parameters of different channels
 */
int print_options(attack_channel_info_t attack_channels[]) {
    int i = 0;
    attack_channel_info_t *iter = &attack_channels[0];
    while (iter->name != NULL) {
        if (iter->num_threads != 0) {
            printf("Attack channel : %s, number of threads: %d \n", iter->name,
                   iter->num_threads);
        }
        i++;
        iter = &attack_channels[i];
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Generate a random string with specified length
 * @dest: Address to store string
 * @length: String lenght
 */
void rand_str(char *dest, size_t length) {
    char charset[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        size_t index = (double)rand() / RAND_MAX * (sizeof charset - 1);
        *dest++ = charset[index];
    }

    /* Add an end symbol */
    *dest = '\0';
}

/**
 * @brief Read a 8 bytes data,
 * This may stress hardware prefetcher
 * @data: Address to read
 */
void read64(uint64_t *data) {
    register uint64_t v;
    const volatile uint64_t *vdata = data;

    __builtin_prefetch(data);
    v = vdata[0];
    (void)v;
    v = vdata[1];
    (void)v;
    v = vdata[3];
    (void)v;
    v = vdata[4];
    (void)v;
    v = vdata[5];
    (void)v;
    v = vdata[6];
    (void)v;
    v = vdata[7];
    (void)v;
    v = vdata[8];
    (void)v;
}

/**
 * @brief Calculate the elapsed time between two time points
 * @start: Start time point
 * @stop: Stop time point
 */
struct timespec get_elapsed_time(struct timespec *start,
                                 struct timespec *stop) {
    struct timespec elapsed_time;
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        elapsed_time.tv_sec = stop->tv_sec - start->tv_sec - 1;
        elapsed_time.tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        elapsed_time.tv_sec = stop->tv_sec - start->tv_sec;
        elapsed_time.tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
    return elapsed_time;
}

/**
 *  page_write_sync()
 *  @brief write a whole page of zeros to the backing file and
 *	ensure it is sync'd to disc for mmap'ing to avoid any
 *	bus errors on the mmap.
 */
int page_write_sync(const int fd, const size_t page_size) {
    char buffer[256];
    size_t n = 0;

    (void)memset(buffer, 0, sizeof(buffer));

    while (n < page_size) {
        ssize_t rc;

        rc = write(fd, buffer, sizeof(buffer));
        if (rc < (ssize_t)sizeof(buffer)) return (int)rc;
        n += (size_t)rc;
    }
    (void)sync();

    return EXIT_SUCCESS;
}

#ifdef x86

uint64_t rdtsc_nofence() {
    uint64_t a, d;
    asm volatile("rdtsc" : "=a"(a), "=d"(d));
    a = (d << 32) | a;
    return a;
}

uint64_t rdtsc() {
    uint64_t a, d;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=a"(a), "=d"(d));
    a = (d << 32) | a;
    asm volatile("mfence");
    return a;
}

void maccess(void *p) { asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax"); }

#endif

/**
 @brief: Get the current time and convert it to microseconds
 @return: Current time
 */
long get_current_time_us(void) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    return spec.tv_sec * MICROSEC + spec.tv_nsec / 1000;
}

/**
 @brief: print the resource usage
 @return: 0 on success
 */
void printRusage(const struct rusage *ru) {
    printf(" CPU time (secs):         user=%.3f; system=%.3f\n",
           ru->ru_utime.tv_sec + ru->ru_utime.tv_usec / 1000000.0,
           ru->ru_stime.tv_sec + ru->ru_stime.tv_usec / 1000000.0);
    printf(" Max resident set size:   %ld\n", ru->ru_maxrss);
    printf(" Integral shared memory:  %ld\n", ru->ru_ixrss);
    printf(" Integral unshared data:  %ld\n", ru->ru_idrss);
    printf(" Integral unshared stack: %ld\n", ru->ru_isrss);
    printf(" Page reclaims:           %ld\n", ru->ru_minflt);
    printf(" Page faults:             %ld\n", ru->ru_majflt);
    printf(" Swaps:                   %ld\n", ru->ru_nswap);
    printf(" Block I/Os:              input=%ld; output=%ld\n", ru->ru_inblock,
           ru->ru_oublock);
    printf(" Signals received:        %ld\n", ru->ru_nsignals);
    printf(" IPC messages:            sent=%ld; received=%ld\n", ru->ru_msgsnd,
           ru->ru_msgrcv);
    printf(
        " Context switches:        voluntary=%ld; "
        "involuntary=%ld\n",
        ru->ru_nvcsw, ru->ru_nivcsw);
}

/**
 @brief Split string with delimiters
 @a_str: String to split
 @a_delim: Delimiter
 @return: Substrings
 */
char **str_split(char *a_str, const char a_delim) {
    char **result = 0;
    size_t count = 0;
    char *tmp = a_str;
    char *last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp) {
        if (a_delim == *tmp) {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char *) * count);

    if (result) {
        size_t idx = 0;
        char *token = strtok(a_str, delim);

        while (token) {
            /* No delimiter found in the target string */
            if (idx >= count) {
                return NULL;
            }
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }

        /* Amount of substrings are not the number we counted before */
        if (idx != count - 1) {
            return NULL;
        }
        *(result + idx) = 0;
    }

    return result;
}

/**
 @brief Split string with delimiters
 @a_str: String to split
 @a_delim: Delimiter
 @return: Numbers
 */
int *str_split_to_nums(char *a_str, const char a_delim) {
    int *result = 0;
    size_t count = 0;
    char *tmp = a_str;
    char *last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp) {
        if (a_delim == *tmp) {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char *) * count);

    if (result) {
        size_t idx = 0;
        char *token = strtok(a_str, delim);

        while (token) {
            /* No delimiter found in the target string */
            if (idx >= count) {
                return NULL;
            }
            *(result + idx++) = atoi(token);
            token = strtok(0, delim);
        }

        /* Amount of substrings are not the number we counted before */
        if (idx != count - 1) {
            return NULL;
        }
        *(result + idx) = 0;
    }

    return result;
}