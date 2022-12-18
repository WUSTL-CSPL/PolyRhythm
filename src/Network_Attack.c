#include <errno.h>
#include <time.h>

#include "Attacks.h"
#include "PolyRhythm.h"
#include "Utils.h"

/* network attack */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/* All extern trigger flags */
extern int udp_flag;
extern struct action *shared_memory_action;

/* feedbacks for RL, defined in PolyRhythm_RL.c */

extern unsigned long int network_contention_count;
extern unsigned long int network_domain_contention_count;

/*************************************
 * Parameters for UDP attack
 * ***********************************
 */

#define UDP_PORT 11311        // Magic port number
#define UDP_ADDR "127.0.0.1"  //
#define TIMES_DOMAIN_CHANGE 20
#define TIMES_PORT_CHANGE 2
#define DOMAIN_RANGE \
    65535  // Temporarily, this is max port number, we have to switch it to
           // socket domain

// If we do not use while loop
#define NET_ITERATIONS 551

/*
    Reads ports from either the /proc/net/udp or /proc/net/tcp file.
    The first line (which contains column headers) is skipped.

    Subsequent lines should begin like this:
    1: 00000000:0016 00000000:0000
    The second column is address:port where both are expressed in hexadecimal

    We parse out the port and convert it to a decimal representation
*/

int read_ports(FILE *f, int *open_ports, int *num_open_ports) {
    char line[MAX_PROC_NET_LINE];

    // The first line should be skipped
    if (!fgets(line, MAX_PROC_NET_LINE, f)) return ERR_FILE_EMPTY;

    // Parse subsequent lines
    while (fgets(line, MAX_PROC_NET_LINE, f)) {
        if (*num_open_ports >= MAX_PORTS) return ERR_PORTS_EXCEEDED;

        char *start, *end;
        start = line;

        // Skip to port
        while (*start != ':') start++;
        start++;
        while (*start != ':') start++;
        start++;

        // Write \0 to space after port to make start a C-style string.
        end = start;
        while (*end != ' ') end++;
        end = '\0';

        // Extract port
        errno = 0;
        int port = (int)strtol(start, NULL, 16);
        if (errno) return ERR_PARSING;  // Failed to extract port

#if DEBUG
        printf("Port %d: %d\n", *num_open_ports, port);
#endif

        // Skip port 0
        if (port > 0) {
            open_ports[*num_open_ports] = port;
            (*num_open_ports)++;
        }
    }

    return SUCCESS;
}

int intcmp(const void *a, const void *b) { return (*(int *)a - *(int *)b); }

/**
 * @brief Get the open ports object
 *
 * @param open_ports Store the list of port indexes
 * @param num_open_ports To pass out number of open ports
 * @return EXIT FLAG
 */
int get_open_ports(int *open_ports, int *num_open_ports) {
    *num_open_ports = 0;

    // Open /proc/net files to read ports
    FILE *tcp = fopen("/proc/net/tcp", "r");
    FILE *udp = fopen("/proc/net/udp", "r");
    if (!tcp && !udp) return ERR_OPENING;

    int ret = 0;

    // Read ports from files
    int index = 0;

    ret = read_ports(tcp, open_ports, num_open_ports);
    if (ret) return ret;

    ret = read_ports(udp, open_ports, num_open_ports);
    if (ret) return ret;

    qsort(open_ports, *num_open_ports, sizeof(int), intcmp);

    return EXIT_SUCCESS;
}

/****************End of Get Ports******************/

// Default: AF_INET  Value range: 0 - 10
static const domain_t domains[] = {
    {"ipv4", AF_INET},
    {"unix", AF_UNIX},
    {"ipv6", AF_INET6},
};

// Additional queue verified in AF_ALG (2) domain
static int socket_domain;

/* Some global variables */

static char *packet_content;
static int packet_size;

static int log_flag;
static int s;
static int s_unix;
static int s_unix_sender;
static struct sockaddr *addr;
static socklen_t addr_len;
static struct sockaddr_in to;
static struct sockaddr_un unix_to;    // address for UNIX domain
static struct sockaddr_un unix_from;  // address for UNIX domain

static int iteration_count = 0;  // Count the online adjustment times
static unsigned long int contention_counts_domains[DOMAIN_RANGE];
static int current_domain_index = 0;  // The default is AF_INET
static unsigned long int
    *contention_counts_ports;  // Size of this array is not fixed

static int current_port_index = 0;

static int open_ports[MAX_PORTS];
static int num_open_ports;

/* Two endpoint for UNIX domain communication */
#define UNIX_TO_PATH "/tmp/unix_sock"

/**
 * @brief Initialize UNIX domain
 */
int init_unix_domain() {
    /* create a unix domain socket */
    // unlink, if already exists
    struct stat statbuf;
    if (stat(UNIX_TO_PATH, &statbuf) == 0) {
        if (unlink(UNIX_TO_PATH) == -1) printf("err unlink \n");
    }

    /* creat the socket */
    if ((s_unix = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1)
        printf("err socket \n");

    struct sockaddr_un socket_address;

    memset(&socket_address, 0, sizeof(struct sockaddr_un));
    socket_address.sun_family = AF_UNIX;
    strncpy(socket_address.sun_path, UNIX_TO_PATH,
            sizeof(socket_address.sun_path) - 1);

    if (bind(s_unix, (const struct sockaddr *)&socket_address,
             sizeof(struct sockaddr_un)) == -1)
        printf("err bind \n");

    // Mark socket for accepting incoming connections using accept
    if (listen(s_unix, 1000) == -1) printf("err listen \n");

    /* End of server */

    /* Client */

    if ((s_unix_sender = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1)
        printf("err client socket \n");

    struct sockaddr_un cli_socket_address;

    memset(&cli_socket_address, 0, sizeof(struct sockaddr_un));
    cli_socket_address.sun_family = AF_UNIX;
    strncpy(cli_socket_address.sun_path, UNIX_TO_PATH,
            sizeof(cli_socket_address.sun_path) - 1);

    if (connect(s_unix_sender, (const struct sockaddr *)&cli_socket_address,
                sizeof(struct sockaddr_un)) == -1)
        printf("err connect \n");

    /* End of client */

    return EXIT_SUCCESS;
}

/* End of int UDP domain */

/**
 * @brief Initialize network I/O (UDP) attack channels
 * @param:
 * 0: packet size
 * 1: socket domain UNIX/AF_INET
 */
int init_udp_attack(void *arguments) {
    int i, j;
    int *args = (int *)arguments;

    /* Parse the parameters */
    if (args[0] > 0) {
        packet_size = args[0];
    }

    if (args[1] > 0) {
        socket_domain = domains[args[1]].domain;
    } else {
        socket_domain = AF_INET;
    }

    /* Construct the content to be sent */
    packet_content = malloc(sizeof(char) * packet_size);
    /* Fill the packet with data */
    rand_str(packet_content, packet_size - 1);

    /***** Initialize UDP attack ******/
    /* Create the socket */
    s = socket(socket_domain, SOCK_DGRAM, 0);
    /* If failed to creat socket */
    if (s < 0) {
        printf("Init socket failed \n");
        return EXIT_FAILURE;
    }

    memset(&to, 0, sizeof(to));
    to.sin_family = socket_domain;
    inet_aton(UDP_ADDR, &to.sin_addr);
    // to.sin_addr   = inet_addr(UDP_ADDR);

    /* Generate a port number that is between 1024 to 65535 will be selected */
    int num_port = (rand() % (65535 - 1024 + 1)) + 1024;

    /* Set the port */
    to.sin_port = htons(num_port);

    /* Initialize the iteration count */
    iteration_count = 0;

    /* Initialize the contention count for different domains */

    for (i = 0; i < DOMAIN_RANGE; i++) {
        contention_counts_domains[i] = 0;
    }

    /* Profile the opened ports */
    int ret = get_open_ports(open_ports, &num_open_ports);
    // int lastport = 0;

    /* Initialize contention counts */
    contention_counts_ports =
        malloc(sizeof(unsigned long int) * num_open_ports);

    for (j = 0; j < num_open_ports; j++) {
        contention_counts_ports[j] = 0;
    }

    // Set current port index and current domain index;
    current_port_index = 0;
    if (args[1] > 0) {
        current_domain_index = args[1];
    } else {
        current_domain_index = 0;
    }

    /* Init unix socket */
    init_unix_domain();

    return EXIT_SUCCESS;
}

int stress_udp_flood() {
    /* UDP attack loop */

#ifdef RL_ONLINE

#ifdef TIMER
    while (shared_memory_action->network) {
#else
    for (int it = 0; it < NET_ITERATIONS; it++) {
#endif

#else
    /* For normal mode of PolyRhythm */
    while (udp_flag) {
#endif
        // printf("UDP loop \n");
        // (void)memset(buf, data[j++ & 63], sz);
        // printf("packet content : %s size : %d  Parameter :%d \n",
        // packet_content, sizeof(packet_content), packet_size);

        if (sendto(s, packet_content, packet_size + 1, 0,
                   (struct sockaddr *)&to, sizeof(to)) < 0) {
            printf("UDP attack sendto error. \n");
            return EXIT_FAILURE;
        }

        /* Count the network loop, less count means more cache contention */
        network_contention_count++;
    }

    /*  Don't need to close the socket if we launch network attack later */
    //    close(s);

    return EXIT_SUCCESS;
}

/*******************************************************************************************
 * The functions below are for
 * Step 2 --
 * Online contention region profiling
 */

// int init_online_udp_attack(void *arguments);  // This function is the same as
// init_udp_attack();

int online_profiling_udp_remap_domain(
    int domain_index)  // For online contention region profiling
{
    /* Close socket and then re-open it */
    close(s);

    /***** Re-Initialize UDP attack ******/

    // Re-Generate a socket domain
    socket_domain = domains[domain_index].domain;
    // Fix this
    socket_domain = AF_INET;

    sleep(1);

    // Re-Create the socket
    s = socket(socket_domain, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("Re-create socket failed \n");
        return EXIT_FAILURE;
    }

    // Reset 'to' socket
    memset(&to, 0, sizeof(to));
    to.sin_family = socket_domain;
    inet_aton(UDP_ADDR, &to.sin_addr);

    // Re-Generate a port number that is between 1024 to 65535 will be selected
    int num_port = (rand() % (65535 - 1024 + 1)) + 1024;
    to.sin_port = htons(num_port);

    // One more time of contention region remap
    iteration_count++;

    return num_port;
}

int online_profiling_udp_remap_port(
    int port_index)  // For online contention region profiling
{
    /* Close socket and then re-open it */
    close(s);

    /***** Re-Initialize UDP attack ******/

    // Re-Generate a socket domain
    socket_domain = domains[current_domain_index].domain;
    // Fix this
    // socket_domain = AF_INET;

    sleep(1);

    // Re-Create the socket
    s = socket(socket_domain, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("Re-create socket failed \n");
        return EXIT_FAILURE;
    }

    // Reset 'to' socket
    memset(&to, 0, sizeof(to));
    to.sin_family = socket_domain;
    inet_aton(UDP_ADDR, &to.sin_addr);

    // Re-Generate a port number that is between 1024 to 65535 will be selected
    int num_port = open_ports[port_index];
    to.sin_port = htons(num_port);

    contention_counts_ports[port_index] = network_contention_count;
    // One more time of contention region remap
    iteration_count++;

    return num_port;
}

/**
 * @brief Main attack loop for udp attack with online profiling
 */
int online_profiling_stress_udp_flood() {
    /* UDP Profiling Loop */
    int tmp_port_count = 0;
    int tmp_domain_count = 0;
    int size_of_domains = sizeof(domains) / sizeof(domain_t);

    printf("Online loop \n");
    printf("Number of opened ports: %d \n", num_open_ports);

    // udp_flag enables/disables the udp attack loop
    while (udp_flag) {
        // We can either use time or count to measure the slowdown
        // long start = get_current_time_us();
        if (current_domain_index == 0) {
            if (sendto(s, packet_content, packet_size + 1, 0,
                       (struct sockaddr *)&to, sizeof(to)) < 0) {
                printf("UDP attack sendto error. \n");
                return EXIT_FAILURE;
            }
        } else if (current_domain_index == 1) {
            if (write(s_unix_sender, packet_content, packet_size) == -1) {
                printf("Unix UDP attack sendto error %i. \n", errno);
                return EXIT_FAILURE;
            }
            printf("Unix Domain: data sent \n");
        }

        // long end = get_current_time_us();

        /* Count the network loop, less count means more cache contention */
        network_contention_count++;

        // We first profile ports. Once all ports are done, we swtich to domains
        if (iteration_count < (num_open_ports - 1)) {
            /* Calculate the least contended port */
            if (tmp_port_count >=
                TIMES_PORT_CHANGE)  // This number may be changed
            {
                printf("Port loop \n");

                contention_counts_ports[current_port_index] =
                    network_contention_count;

                online_profiling_udp_remap_port(++current_port_index);

                network_contention_count = 0;
                tmp_port_count = 0;
            } else {
                tmp_port_count++;
            }
        }

        else if (iteration_count < num_open_ports + size_of_domains) {
            // All domains should be iterated
            /* Calculate the least contended domain */
            if (tmp_domain_count >=
                TIMES_DOMAIN_CHANGE)  // This number may be changed
            {
                /* For domain contention */
                contention_counts_domains[current_domain_index] =
                    network_contention_count;

                printf("Switching domains \n");

                if (current_domain_index == 0) {
                    // Switch to UNIX domain
                    // init_unix_domain();
                    current_domain_index++;

                    if (current_domain_index == 1) {  // Now this is unix domain
                        iteration_count++;
                        continue;
                    }
                    online_profiling_udp_remap_domain(current_domain_index);

                } else {
                    current_domain_index = 0;  // reset to AF_INET
                }

                network_contention_count = 0;
                tmp_domain_count = 0;

            } else {
                tmp_domain_count++;
            }

        } else {
            printf("Profiling done \n");
            /* Once the remap times reach the maximum number
             * jump to the attack loop without any time recording
             */
            /* Find the most effective domain */
            int k;
            unsigned long int min_count = contention_counts_ports[0];
            int min_index = 0;
            for (k = 1; k < num_open_ports; k++) {
                if (contention_counts_domains[k] != 0 &&
                    min_count > contention_counts_ports[k]) {
                    min_count = contention_counts_ports[k];
                }
                min_index = k;
            }

            /* Switch to the most contending domain */
            online_profiling_udp_remap_port(min_index);

            if (current_domain_index == 0) {
                /* Attack on AF_INET domain */
                goto af_inet;
            } else if (current_domain_index == 1) {
                /* Attack on Unix domain */
                goto af_unix;
            } else {
                goto af_inet;
            }
        }
    }

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action

    /*  Don't need to close the socket if we launch network attack later */
    //    close(s);

af_inet:

    // Reset the state count
    network_contention_count = 0;
    printf("Entering AF_INET Attack Loop.");

    /* Attack Loop */
    while (udp_flag) {
        if (sendto(s, packet_content, packet_size + 1, 0,
                   (struct sockaddr *)&to, sizeof(to)) < 0) {
            printf("UDP attack sendto error. \n");
            return EXIT_FAILURE;
        }

        /* Count the network loop, less count means more cache contention */
        network_contention_count++;
    }

af_unix:

    printf("Entering AF_UNIX Attack Loop.\n");

    while (udp_flag) {
        if (write(s_unix_sender, packet_content, packet_size) == -1) {
            printf("Unix UDP attack sendto error %i. \n", errno);
            return EXIT_FAILURE;
        }

        /* Count the network loop, less count means more cache contention */
        network_contention_count++;
    }

    // In normal mode, PolyRhythm will not reach here
    // This is for RL, we back to sched_next_tasks() to execute the next action
    // sched_next_tasks();

    /*  Don't need to close the socket if we launch network attack later */
    //    close(s);

    return EXIT_SUCCESS;
}