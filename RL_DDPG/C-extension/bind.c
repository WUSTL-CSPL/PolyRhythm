#include "libshm.h"

char init_shmfunc_docs[] = "Initialize the shared memory";
char write_shmfunc_docs[] = "Write the data into shared memory";

PyMethodDef shmextension_funcs[] = {
    {"init_shm", (PyCFunction)init_shm, METH_NOARGS, init_shmfunc_docs},
    {"write_shm", (PyCFunction)write_shm, METH_VARARGS, write_shmfunc_docs},
    {NULL}};

char shmextensionmod_docs[] = "This is a C extension for writing action.";

PyModuleDef shmextension_mod = {PyModuleDef_HEAD_INIT,
                                "shmextension",
                                shmextensionmod_docs,
                                -1,
                                shmextension_funcs,
                                NULL,
                                NULL,
                                NULL,
                                NULL};

PyMODINIT_FUNC PyInit_shmextension(void) {
    return PyModule_Create(&shmextension_mod);
}
