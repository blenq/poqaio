#include "poqaio.h"

#define BUF_SIZE 8192


typedef struct {
    PyObject_HEAD
    char *in_buf;
    char *in_extra_buf;
    char *buf_in_use;
    char msg_header[5];
    char receiving_header;
    char transaction_status[2];
    int32_t msg_length;
    int received_bytes;
    PyObject *message;    // remove when not used anymore
    PyObject *header;
    PyObject *wr_list;
} BaseProt;


static PyObject *
BaseProt_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    BaseProt *self;
    char * in_buf;
    PyObject *header;

    self = (BaseProt *) type->tp_alloc(type, 0);
    if (self != NULL) {
        in_buf = PyMem_Malloc(BUF_SIZE);
        if (in_buf == NULL) {
            Py_DECREF(self);
            return PyErr_NoMemory();
        }
        header = PyMemoryView_FromMemory(self->msg_header, 5, PyBUF_WRITE);
        if (header == NULL) {
            PyMem_Free(in_buf);
            Py_DECREF(self);
            return PyErr_NoMemory();
        }
        self->in_buf = in_buf;
        self->header = header;
        self->receiving_header = 1;
        self->msg_length = 5;
    }
    return (PyObject *) self;
}


static void
BaseProt_dealloc(BaseProt *self)
{
    if (self->wr_list != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);
    PyMem_Free(self->in_buf);
    PyMem_Free(self->in_extra_buf);
    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyObject *
BaseProt_get_buffer(BaseProt *self, PyObject *args)
{
    char *buf;

    if (self->receiving_header) {
        Py_XDECREF(self->message);
        self->message = NULL;
        PyMem_Free(self->in_extra_buf);
        self->in_extra_buf = NULL;
        Py_INCREF(self->header);
        return self->header;
    }
    if (self->msg_length > BUF_SIZE) {
        buf = PyMem_Malloc(self->msg_length);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        self->in_extra_buf = buf;
    }
    else {
        buf = self->in_buf;
        Py_INCREF(self->in_buf);
    }
    self->buf_in_use = buf;
    self->message = PyMemoryView_FromMemory(buf, self->msg_length, PyBUF_WRITE);
    return self->message;
}


static int
check_length(BaseProt *self, int length) {
    if (length != self->msg_length) {
        char identifier[2];

        identifier[0] = self->msg_header[0];
        identifier[1] = 0;

        PyErr_Format(
            PoqaioProtocolError,
            "Invalid length for message with identifier '%s'. Expected %d, but "
            "got %d.",
            identifier, length, self->msg_length);
        return -1;
    }
    return 0;
}

static int
handle_ready(BaseProt *self) {
    char status;

    if (check_length(self, 1) == -1) {
        return -1;
    }
    status = self->buf_in_use[0];
    status = 0;
    switch (status) {
        case 'I':
        case 'E':
        case 'T':
            self->transaction_status[0] = status;
            return 0;
        default
            char status_string[2];
            status_string[0] = status;
            status_string[1] = '\0';
            PyErr_Format(
                PoqaioProtocolError,
                "Invalid transaction code in ReadyResponse, got '%s'",
                status_string);
            return -1;
    }

}


static int
handle_message(BaseProt *self) {
    PyObject *ret;
    int res;

    switch (self->msg_header[0]) {
    case 'Z':
        res = handle_ready(self);
    default:
        ret = PyObject_CallMethod((PyObject *)self, "handle_message", NULL);
        if (ret == NULL) {
            res = -1;
        }
        else {
            res = 0;
        }
    }
    if (res == -1) {

    }
}


static PyObject *
BaseProt_buffer_updated(BaseProt *self, PyObject *args)
{
    // This is called from the transport when data is available. Raising
    // errors makes no sense, because those do not end up in user space.
    // Make sure to gather exceptions and store those for later to set them
    // on the future.
    int nbytes, ret;
    int32_t length;

    if (!PyArg_ParseTuple(args, "i", &nbytes)) {
        return NULL;
    }
    self->received_bytes += nbytes;
    if (self->received_bytes < self->msg_length) {
        Py_RETURN_NONE;
    }
    if (self->receiving_header) {
        memcpy(&length, self->msg_header + 1, 4);
        length = be32toh(length);
        // TODO: check for negative msg_length;
//        if (length < 4) {
//                raise
//        }
        self->msg_length = length - 4;  // length includes length itself
        if (self->msg_length > 0) {
            self->receiving_header = 0;
            Py_RETURN_NONE;
        }
    }
    handle_message(self);

    self->msg_length = 5;
    self->receiving_header = 1;
    self->received_bytes = 0;
    Py_RETURN_NONE;
}


static PyObject *
BaseProt_getidentifier(BaseProt *self, void *closure)
{
    return PyLong_FromLong(self->msg_header[0]);
}


static PyGetSetDef BaseProt_getsetters[] = {
    {"identifier", (getter) BaseProt_getidentifier, NULL,
     "identifier", NULL},
    {NULL}  /* Sentinel */
};


static PyMemberDef BaseProt_members[] = {
    {"receiving_header", T_BOOL, offsetof(BaseProt, receiving_header), 0,
     "indicates if protocol is receiving header"},
    {"message", T_OBJECT, offsetof(BaseProt, message), READONLY, ""}, // remove
    {"msg_length", T_INT, offsetof(BaseProt, msg_length), 0, ""}, // remove
    {NULL}  /* Sentinel */
};


static PyMethodDef BaseProt_methods[] = {
    {"get_buffer", (PyCFunction) BaseProt_get_buffer, METH_VARARGS,
     "Return the name, combining the first and last name"
    },
    {"buffer_updated", (PyCFunction) BaseProt_buffer_updated, METH_VARARGS,
     "buffer updated"
    },
    {NULL}  /* Sentinel */
};


PyTypeObject BaseProtType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "BaseProt",                                 /* tp_name */
    sizeof(BaseProt),                           /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)BaseProt_dealloc,               /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash  */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    PyDoc_STR("poqaio BaseProt object"),        /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    offsetof(BaseProt, wr_list),                /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    BaseProt_methods,                           /* tp_methods */
    BaseProt_members,                           /* tp_members */
    BaseProt_getsetters,                        /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    BaseProt_new                                /* tp_new */
};
