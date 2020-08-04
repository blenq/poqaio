#include "poqaio.h"

#define BUF_SIZE 8192
#define HEADER_SIZE 5
#define MSG_START(prot) ((prot)->buf_in_use + HEADER_SIZE)
#define MSG_END(prot) ((prot)->buf_in_use + (prot)->msg_length)

typedef struct {
    PyObject_HEAD
    char *in_buf;
    char *in_extra_buf;
    char *buf_in_use;
    char *password;
    char *user;
    char receiving_header;
    char uses_utf8;
    char transaction_status;
    int uses_iso;
    int32_t msg_length;
    int received_bytes;

    PyObject *loop;
    PyObject *transport;
    PyObject *fut;

    PyObject *message;    // remove when not used anymore
    PyObject *header;
    PyObject *results;
    PyObject *error;
    PyObject *status_parameters;
    PyObject *wr_list;
    int32_t backend_process_id;
    int32_t backend_secret_key;
} BaseProt;


static PyObject *
BaseProt_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    BaseProt *self;
    char * in_buf;

    self = (BaseProt *) type->tp_alloc(type, 0);
    if (self != NULL) {
        PyObject *asyncio, *get_running_loop;

        asyncio = PyImport_ImportModule("asyncio");
        if (asyncio == NULL) {
            Py_DECREF(self);
            return NULL;
        }
        get_running_loop = PyObject_GetAttrString(asyncio, "get_running_loop");
        if (get_running_loop == NULL) {
            Py_DECREF(asyncio);
            Py_DECREF(self);
            return NULL;
        }
        self->loop = PyObject_CallFunction(get_running_loop, NULL);
        Py_DECREF(asyncio);
        if (self->loop == NULL) {
            Py_DECREF(self);
            return NULL;
        }

        in_buf = PyMem_Malloc(BUF_SIZE);
        if (in_buf == NULL) {
            Py_DECREF(self);
            return PyErr_NoMemory();
        }
        self->status_parameters = PyDict_New();
        self->in_buf = in_buf;
        self->buf_in_use = in_buf;
        self->receiving_header = 1;
        self->msg_length = HEADER_SIZE;  // size of header
        Py_INCREF(Py_None);
        self->results = Py_None;
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
BaseProt_connection_made(BaseProt *self, PyObject *args) {
    PyObject *transport;

    if (!PyArg_ParseTuple(args, "O", &transport)) {
        return NULL;
    }
    Py_INCREF(transport);
    self->transport = transport;
    Py_RETURN_NONE;
}


static int32_t
read_int32(char *from) {
    uint32_t val;
    memcpy(&val, from, sizeof(uint32_t));
    return (int32_t)be32toh(val);
}


static char *
read_str(char **src, Py_ssize_t *len, size_t max_len)
{
    char *term, *ret;

    term = memchr(*src, '\0', max_len);
    if (term == NULL) {
        PyErr_SetString(PoqaioProtocolError, "Terminating zero not found");
        return NULL;
    }
    ret = *src;
    *src = term + 1;  // position past terminator
    *len = term - ret;
    return ret;
}


//static int
//read_pystr(char **src, PyObject **str, size_t max_len)
//{
//    char *term;
//
//    term = memchr(*src, '\0', max_len);
//    if (term == NULL) {
//        PyErr_SetString(PoqaioProtocolError, "Terminating zero not found");
//        return -1;
//    }
//    *str = PyUnicode_FromStringAndSize(*src, term - *src);
//    if (*str == NULL) {
//        return -1;
//    }
//    *src = term + 1;  // position past terminator
//    return 0;
//}


static PyObject *
BaseProt_get_buffer(BaseProt *self, PyObject *args)
{

    // A message must always fit entirely in a buffer. Easy for parsing.
    // There are two buffers, a fixed size one called 'in_buf' and an
    // dynamic one 'in_extra_buf'for large messages.

    if (self->msg_length > BUF_SIZE && self->buf_in_use != self->in_extra_buf) {
        // The large buffer is needed and not in use yet, allocate it. After
        // the message is handled, the extra large buffer will be cleared
        char *buf;

        buf = PyMem_Malloc(self->msg_length);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        self->in_extra_buf = buf;

        // copy already received data (header) into big buffer
        memcpy(buf, self->in_buf, self->received_bytes);
        self->buf_in_use = buf;
    }

    // Return a view on the (remaining) missing message data
    return PyMemoryView_FromMemory(
        self->buf_in_use + self->received_bytes,
        self->msg_length - self->received_bytes,
        PyBUF_WRITE);
}


static int
check_length(BaseProt *self, int length) {
    if (length + HEADER_SIZE != self->msg_length) {
        char identifier[2];

        identifier[0] = self->buf_in_use[0];
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
handle_backend_key(BaseProt *self) {

    if (check_length(self, 8) == -1) {
        return -1;
    }

    self->backend_process_id = read_int32(MSG_START(self));
    self->backend_secret_key = read_int32(MSG_START(self) + 4);
    return 0;
}


static int
handle_parameter_status(BaseProt *self) {
    char *pos, *end;
    int ret = -1, client_encoding = 0, date_style = 0;
    char *bname;
    Py_ssize_t len;
    PyObject *name=NULL, *val=NULL;

    pos = MSG_START(self);
    end = MSG_END(self);
    if (end <= pos) {
        PyErr_SetString(
            PoqaioProtocolError, "Empty parameter status message.");
        goto end;
    }
    bname = read_str(&pos, &len, end - pos);
    if (bname == NULL) {
        goto end;
    }
    if (strcmp(bname, "client_encoding") == 0) {
        client_encoding = 1;
    }
    else if (strcmp(bname, "DateStyle") == 0) {
        date_style = 1;
    }
    name = PyUnicode_FromStringAndSize(bname, len);
    if (name == NULL) {
        goto end;
    }

    if (pos == end) {
        PyErr_SetString(
            PoqaioProtocolError, "Missing value in parameter status message.");
        return -1;
    }

    bname = read_str(&pos, &len, end - pos);
    if (bname == NULL) {
        goto end;
    }
    if (client_encoding) {
        self->uses_utf8 = !strcmp(bname, "UTF8");
    }
    else if (date_style) {
        self->uses_iso = !strncmp(bname, "ISO", 3);
    }

    val = PyUnicode_FromStringAndSize(bname, len);
    if (name == NULL) {
        goto end;
    }

    if (pos != end) {
        Py_DECREF(name);
        PyErr_SetString(
            PoqaioProtocolError,
            "Remaining data after parameter status message.");
        goto end;
    }
    ret = PyDict_SetItem(self->status_parameters, name, val);

end:
    Py_XDECREF(name);
    Py_XDECREF(val);
    return ret;
}


static int
handle_ready(BaseProt *self) {
    char status;

    if (check_length(self, 1) == -1) {
        return -1;
    }
    status = MSG_START(self)[0];
    switch (status) {
    case 'I':
    case 'E':
    case 'T':
        self->transaction_status = status;
        break;
    default:
        char status_string[2] = {status, 0};
        PyErr_Format(
            PoqaioProtocolError,
            "Invalid transaction code in ReadyResponse, got '%s'",
            status_string);
        return -1;
    }

    PyObject *py_done;
    int done;

    py_done = PyObject_CallMethod(self->fut, "done", NULL) ;
    if (py_done == NULL) {
        return -1;
    }
    done = PyObject_IsTrue(py_done);
    Py_DECREF(py_done);
    PyObject *res;

    int ret = 0;

    if (self->error) {
        if (!done) {

            res = PyObject_CallMethod(
                    self->fut, "set_exception", "O", self->error);
            if (res == NULL) {
                ret = -1;
            }
            else {
                Py_DECREF(res);
            }
        }
        Py_CLEAR(self->error);
    }
    else {
        if (!done) {
            res = PyObject_CallMethod(
                self->fut, "set_result", "O", self->results);
            if (res == NULL) {
                ret = -1;
            }
            else {
                Py_DECREF(res);
            }
        }
    }
    return ret;
}


static int
handle_message(BaseProt *self) {
    PyObject *ret;
    int res;
    switch (self->buf_in_use[0]) {
    case 'Z':
        res = handle_ready(self);
        break;
    case 'K':
        res = handle_backend_key(self);
        break;
    case 'S':
        res = handle_parameter_status(self);
        break;
    case 'n':  // NoData
    case '1':  // ParseComplete
    case '2':  // BindComplete
        res = check_length(self, 0);
        break;
    default:
        ret = PyObject_CallMethod((PyObject *)self, "handle_message", NULL);
        if (ret == NULL) {
            res = -1;
        }
        else {
            Py_DECREF(ret);
            res = 0;
        }
    }
    return res;
}


static void
outbuf_write(char **buf, void *src, Py_ssize_t n) {
    memcpy(*buf, src, n);
    *buf += n;
}


static PyObject *
BaseProt_startup(BaseProt *self,  PyObject *args) {
    char *user, *db, *app, *pwd, *startup_message, *pos;
    Py_ssize_t user_len, db_len=0, app_len, pwd_len, size;
    int32_t msize;
    PyObject *ret;

    if (!PyArg_ParseTuple(
            args,"s#z#z#z#", &user, &user_len, &db,
            &db_len, &app, &app_len, &pwd, &pwd_len)) {
        return NULL;
    }
    user_len += 1;
    self->user = PyMem_Malloc(user_len);
    if (self->user == NULL) {
        return PyErr_NoMemory();
    }
    memcpy(self->user, user, user_len);
    if (pwd) {
        pwd_len += 1;
        self->password = PyMem_Malloc(pwd_len);
        if (self->password == NULL) {
            return PyErr_NoMemory();
        }
        memcpy(self->password, pwd, pwd_len);
    }

    // size = 8 (header) + 'user' (5) + 'DateStyle' (10) + 'ISO' (4) +
    //     'client_encoding' (16) + 'UTF8' (5) + terminating zero (1) = 49
    size = 49 + user_len;
    if (db_len) {
        // 'database' (9) + value
        db_len += 1;
        size += 9 + db_len;
    }
    if (app_len) {
        // 'application_name' (17) + value
        app_len += 1;
        size += 17 + app_len;
    }
    startup_message = PyMem_Malloc(size);
    if (startup_message == NULL) {
        return PyErr_NoMemory();
    }

    pos = startup_message;

    // size of buffer (including size)
    msize = (int32_t)htobe32((uint32_t)size);
    outbuf_write(&pos, &msize, sizeof(msize));

    // write user
    outbuf_write(&pos, "\0\x03\0\0user", 9);
    outbuf_write(&pos, user, user_len);

    if (db_len) {
        // write database name
        outbuf_write(&pos, "database", 9);
        outbuf_write(&pos, db, db_len);
    }
    if (app_len) {
        // write application name
        outbuf_write(&pos, "application_name", 17);
        outbuf_write(&pos, app, app_len);
    }

    // write standard parameters
    memcpy(pos, "DateStyle\0ISO\0client_encoding\0UTF8\0", 36);

    // really send it
    ret = PyObject_CallMethod(
        self->transport, "write", "y#", startup_message, size);
    if (ret == NULL) {
        return NULL;
    }
    Py_DECREF(ret);

    // create a future to report on later
    self->fut = PyObject_CallMethod(self->loop, "create_future", NULL);
    return self->fut;
}


static int
_BaseProt_buffer_updated(BaseProt *self, int nbytes) {
    int32_t length;
    int ret;

    if (self->receiving_header) {
        length = read_int32(self->buf_in_use + 1);
        // TODO: check for negative msg_length;
//        if (length < 4) {
//                raise
//        }
        self->msg_length = length + 1;  // length plus identifier
        if (self->msg_length > self->received_bytes) {
            // non-empty message, set up for receiving message body
            self->receiving_header = 0;
            return 0;
        }
    }
    self->message = PyMemoryView_FromMemory(
        self->buf_in_use + HEADER_SIZE, self->msg_length - HEADER_SIZE,
        PyBUF_WRITE);

    ret = handle_message(self);

    // Done with message. Clean up
    Py_CLEAR(self->message);
    if (self->in_extra_buf) {
        // clear large buffer
        PyMem_Free(self->in_extra_buf);
        self->in_extra_buf = NULL;
        self->buf_in_use = self->in_buf;
    }

    self->msg_length = HEADER_SIZE;
    self->receiving_header = 1;
    self->received_bytes = 0;
    return ret;
}


static PyObject *
BaseProt_buffer_updated(BaseProt *self, PyObject *args)
{
    // This is called from the transport when data is available. Raising
    // errors makes no sense, because those do not end up in user space.
    // Make sure to gather exceptions and store those for later to set them
    // on the future.
    int nbytes;

    if (!PyArg_ParseTuple(args, "i", &nbytes)) {
        return NULL;
    }

    self->received_bytes += nbytes;
    if (self->received_bytes < self->msg_length) {
        // not enough data yet
        Py_RETURN_NONE;
    }
    if (_BaseProt_buffer_updated(self, nbytes) == -1) {
        // Something went wrong
        int is_protocol_error;

        is_protocol_error = PyErr_ExceptionMatches(PoqaioProtocolError);

        if (self->error && (
                PyErr_GivenExceptionMatches(
                    self->error, PoqaioProtocolError) || !is_protocol_error)
                ) {
            // Already recorded at least equally important exception
            PyErr_Clear();
        }
        else {
            // First get the exception instance
            PyObject *ex_type, *ex_val, *ex_tb;
            PyErr_Fetch(&ex_type, &ex_val, &ex_tb);
            PyErr_NormalizeException(&ex_type, &ex_val, &ex_tb);
            if (ex_tb != NULL) {
                PyException_SetTraceback(ex_val, ex_tb);
                Py_DECREF(ex_tb);
            }
            Py_DECREF(ex_type);

            // record error
            self->error = ex_val;

            // close connection in case of Protocol error
            if (is_protocol_error) {
                PyObject *is_closing;
                int _is_closing;
                is_closing = PyObject_CallMethod(
                    self->transport, "is_closing", NULL);
                if (is_closing == NULL) {
                    return NULL;
                }
                _is_closing = PyObject_IsTrue(is_closing);
                Py_DECREF(is_closing);
                if (!is_closing) {
                    if (PyObject_CallMethod(
                            self->transport, "close", NULL) == NULL) {
                        return NULL;
                    }
                }
            }
        }
    }
    Py_RETURN_NONE;
}


static PyObject *
BaseProt_getidentifier(BaseProt *self, void *closure)
{
    return PyLong_FromLong(self->buf_in_use[0]);
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
    {"error", T_OBJECT, offsetof(BaseProt, error), 0, ""}, // remove
    {"fut", T_OBJECT, offsetof(BaseProt, fut), 0, ""}, // remove
    {"results", T_OBJECT, offsetof(BaseProt, results), 0, ""}, // remove
    {"msg_length", T_INT, offsetof(BaseProt, msg_length), 0, ""}, // remove
    {"transaction_status", T_CHAR, offsetof(BaseProt, transaction_status),
     READONLY, "transaction status"
    },
    {"status_parameters", T_OBJECT, offsetof(BaseProt, status_parameters),
     READONLY, "status parameters"
    },
    {"password", T_STRING, offsetof(BaseProt, password), READONLY, "password"},
    {"user", T_STRING, offsetof(BaseProt, user), READONLY, "user"},
    {"transport", T_OBJECT, offsetof(BaseProt, transport), 0, ""},
    {NULL}  /* Sentinel */
};


static PyMethodDef BaseProt_methods[] = {
    {"get_buffer", (PyCFunction) BaseProt_get_buffer, METH_VARARGS,
     "Return the name, combining the first and last name"
    },
    {"buffer_updated", (PyCFunction) BaseProt_buffer_updated, METH_VARARGS,
     "buffer updated"
    },
    {"connection_made", (PyCFunction) BaseProt_connection_made, METH_VARARGS,
     "connection made"
    },
    {"startup", (PyCFunction) BaseProt_startup, METH_VARARGS,
     "startup"},
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
