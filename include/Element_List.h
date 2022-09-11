#ifndef elem_list_utils_H
#define elem_list_utils_H

#include <stdlib.h>

#include "PolyRhythm.h"
// #include "cache.h"
// #include "micro.h"

typedef struct elem {
    struct elem *next;
    struct elem *prev;
    int set;
    size_t delta;
    char pad[32];  // up to 64B
} Elem;

int list_length(Elem *ptr);
Elem *list_pop(Elem **ptr);
Elem *list_shift(Elem **ptr);
void list_push(Elem **ptr, Elem *e);
void list_append(Elem **ptr, Elem *e);
void list_split(Elem *ptr, Elem **chunks, int n);
Elem *list_slice(Elem **ptr, size_t s, size_t e);
Elem *list_get(Elem **ptr, size_t n);
void list_concat(Elem **ptr, Elem *chunk);
void list_from_chunks(Elem **ptr, Elem **chunks, int avoid, int len);
void list_set_id(Elem *ptr, int id);
void print_list(Elem *ptr);

// void initialize_random_list(Elem *ptr, ul offset, ul sz, Elem *base);
void initialize_list(Elem *ptr, ul sz, ul offset);
void pick_n_random_from_list(Elem *src, ul stride, ul sz, ul offset, ul n);
void rearrange_list(Elem **ptr, ul stride, ul sz, ul offset);
void generate_conflict_set(Elem **ptr, Elem **out);

#endif /* list_utils_H */
