#include "libshm.h"

#include <Python.h>

/* For the shared memory communication channel */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

#define KEY_ACTION 666644
struct action *shared_memory_action;

PyObject *init_shm(PyObject *self) {
    /* Shared memory */
    int shmid_action;
    if ((shmid_action = shmget(KEY_ACTION, 20, IPC_CREAT | 0666)) < 0) {
        printf("Action SHMemory: Error getting shared memory id\n");
        return PyUnicode_FromFormat("Error");  // TODO return value
    }

    if ((shared_memory_action = (struct action *)shmat(
             shmid_action, NULL, 0)) == (struct action *)-1) {
        printf("(Action) Error attaching shared memory id\n");
        return PyUnicode_FromFormat("Error");
        ;
    }

    return PyUnicode_FromFormat("Hello C extension!");
}

PyObject *write_shm(PyObject *self, PyObject *args) {
    int num;
    char *name;

    /* Initialize the action */
    struct action tmp_action;
    tmp_action.cache = 0;
    tmp_action.row_buffer = 0;
    tmp_action.network = 0;
    tmp_action.disk = 0;
    tmp_action.tlb = 0;

    if (!PyArg_ParseTuple(args, "is", &num, &name)) {
        return NULL;
    }

    switch (num) {
        case 0:
            tmp_action.cache = 1;
            break;
        case 1:
            tmp_action.network = 1;
            break;
        case 2:
            tmp_action.row_buffer = 1;
            break;
        case 3:
            tmp_action.disk = 1;
            break;
        case 4:
            tmp_action.tlb = 1;
            break;
    }

    memcpy(shared_memory_action, &tmp_action, sizeof(struct action));

    return PyUnicode_FromFormat("Hay %s!  You gave me %d.", name, num);
}
