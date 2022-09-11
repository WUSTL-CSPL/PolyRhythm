#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include "PolyRhythm.h"

#define MICROSEC 1000000
#define NANOSEC 1000000000L

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

int parse_options(int argc, char *argv[],
                  attack_channel_info_t attack_channels[]);

int print_options(attack_channel_info_t attack_channels[]);

void rand_str(char *, size_t);

void read64(uint64_t *data);

struct timespec get_elapsed_time(struct timespec *start, struct timespec *stop);

int page_write_sync(const int fd, const size_t page_size);

uint64_t rdtsc_nofence();

uint64_t rdtsc();

void maccess(void *p);

long get_current_time_us(void);

void printRusage(const struct rusage *ru);

char **str_split(char *a_str, const char a_delim);

int *str_split_to_nums(char *a_str, const char a_delim);
