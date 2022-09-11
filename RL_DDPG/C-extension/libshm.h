#ifndef __LIBMYPY_H__
#define __LIBMYPY_H__

#include <Python.h>

/* Attack action buffer */
struct action {
    int cache;
    int network;
    int row_buffer;
    int disk;
    int tlb;
};

PyObject *init_shm(PyObject *);
PyObject *write_shm(PyObject *, PyObject *);

#endif
