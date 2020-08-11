#include "poqaio.h"
#include "protocol.h"
#include "types.h"

#define BUF_SIZE 16384
#define HEADER_SIZE 5
#define MSG_BODY(prot) ((prot)->curr_msg + HEADER_SIZE)
#define MSG_END(prot) ((prot)->curr_msg + (prot)->msg_length)
#define RECEIVING_HEADER(prot) ((prot)->msg_length == HEADER_SIZE)


static PyObject *
BaseProt_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    BaseProt *self;
    char * in_buf=NULL;
    PyObject *asyncio, *get_running_loop=NULL, *loop=NULL, *params=NULL;

    self = (BaseProt *) type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    // for status parameters
    params = PyDict_New();
    if (params == NULL) {
        goto error;
    }

    // get loop
    asyncio = PyImport_ImportModule("asyncio");
    if (asyncio == NULL) {
        goto error;
    }
    get_running_loop = PyObject_GetAttrString(asyncio, "get_running_loop");
    Py_DECREF(asyncio);
    if (get_running_loop == NULL) {
        goto error;
    }
    loop = PyObject_CallFunction(get_running_loop, NULL);
    Py_DECREF(get_running_loop);
    if (loop == NULL) {
        goto error;
    }

    // set up buffer for receiving
    in_buf = PyMem_Malloc(BUF_SIZE);
    if (in_buf == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    self->status_parameters = params;
    self->in_buf = in_buf;
    self->curr_msg = in_buf;
    self->msg_length = HEADER_SIZE;
    self->loop = loop;

    return (PyObject *)self;

error:
    Py_DECREF(self);
    Py_XDECREF(params);
    Py_XDECREF(loop);
    return NULL;
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

    transport = PyTuple_GetItem(args, 0);
    if (transport == NULL) {
        return NULL;
    }
//    printf("Connection made\n");
    self->transport = transport;
    Py_RETURN_NONE;
}


static int
check_length_gte(BaseProt *self, char *pos, ssize_t length) {

    if ((MSG_END(self)) - pos < length) {
        char identifier[2] = {self->curr_msg[0], '\0'};

        PyErr_Format(
            PoqaioProtocolError,
            "Invalid length for message with identifier '%s'. Expected %zu or "
            "more, but got %zu.",
            identifier, length, MSG_END(self) - pos);
        return -1;
    }
    return 0;
}


static uint16_t
read_uint16(char **from) {
    uint16_t val;
    memcpy(&val, *from, sizeof(val));
    *from += sizeof(val);
    return be16toh(val);
}


static inline int16_t
read_int16(char **from) {
    return (int16_t)read_uint16(from);
}


static int
read_uint16_check(BaseProt *self, char **from, uint16_t *val) {
    if (check_length_gte(self, *from, sizeof(uint16_t)) == -1)
        return -1;
    *val = read_uint16(from);
    return 0;
}


static inline int
read_int16_check(BaseProt *self, char **from, int16_t *val) {
    return read_uint16_check(self, from, (uint16_t *)val);
}


static inline PyObject *
read_py_int16(char **from)
{
    return PyLong_FromLong(read_int16(from));
}


static int32_t
get_int32(char *from) {
    uint32_t val;
    memcpy(&val, from, sizeof(uint32_t));
    return (int32_t)be32toh(val);
}


static uint32_t
read_uint32(char **from) {
    uint32_t val;
    memcpy(&val, *from, sizeof(uint32_t));
    *from += sizeof(uint32_t);
    return be32toh(val);
}


static inline int32_t
read_int32(char **from) {
    return (int32_t)read_uint32(from);
}


static int
read_uint32_check(BaseProt *self, char **from, uint32_t *val) {
    if (check_length_gte(self, *from, sizeof(uint32_t)) == -1)
        return -1;
    *val = read_uint32(from);
    return 0;
}


static inline int
read_int32_check(BaseProt *self, char **from, int32_t *val) {
    return read_uint32_check(self, from, (uint32_t *)val);
}


static inline PyObject *
read_py_int32(char **from)
{
    return PyLong_FromLong(read_int32(from));
}


static inline PyObject *
read_py_uint32(char **from)
{
    return PyLong_FromUnsignedLong(read_uint32(from));
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



static PyObject *
read_pystr(char **src, size_t max_len)
{
    Py_ssize_t len;
    char* str;

    str = read_str(src, &len, max_len);
    if (str == NULL) {
        return NULL;
    }
    return PyUnicode_FromStringAndSize(str, len);
}


static int
check_length(BaseProt *self, int length) {
    if (length + HEADER_SIZE != self->msg_length) {
        char identifier[2] = {self->curr_msg[0], '\0'};

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
handle_auth_req(BaseProt *self)
{
    char *pos;
    int specifier;

    pos = MSG_BODY(self);
    if (check_length_gte(self, pos, 4) == -1) {
        return -1;
    }
    specifier = read_int32(&pos);
    switch (specifier) {
        case 0:
            break;
        default:
            PyErr_Format(
                PoqaioProtocolError,
                "Unsupported authentication specifier: %d", specifier);
    }

    if (pos != MSG_END(self)) {
        PyErr_SetString(
            PoqaioProtocolError,
            "Remaining data in authentication request message.");
        return -1;
    }
    return 0;
}


static int
handle_backend_key(BaseProt *self) {

    if (check_length(self, 8) == -1) {
        return -1;
    }

    self->backend_process_id = get_int32(MSG_BODY(self));
    self->backend_secret_key = get_int32(MSG_BODY(self) + 4);
    return 0;
}


static int
handle_parameter_status(BaseProt *self) {
    char *pos, *end;
    int ret = -1, client_encoding = 0, date_style = 0;
    char *bname;
    Py_ssize_t len;
    PyObject *name=NULL, *val=NULL;

    pos = MSG_BODY(self);
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
        self->uses_utf8 = !strncmp(bname, "UTF8", 4);
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
handle_row_description(BaseProt *self) {
    char *pos;
    int16_t nfields;
    int i;
    PyObject *fields;

    pos = MSG_BODY(self);

    if (read_int16_check(self, &pos, &nfields) == -1) {
        return -1;
    }

    fields = PyTuple_New(nfields);
    if (fields == NULL) {
        return -1;
    }

    self->result_oids = PyMem_Malloc(sizeof(uint32_t) * nfields);
    if (self->result_oids == NULL) {
        return -1;
    }

    for (i = 0; i < nfields; i++) {
        PyObject *field_desc, *field_val;
        uint32_t oid;

        // Make a new field description and add to fields tuple
        field_desc = PyStructSequence_New(FieldDescription);
        if (field_desc == NULL) {
            goto error;
        }
        PyTuple_SET_ITEM(fields, i, field_desc);

        // get field name
        field_val = read_pystr(&pos, MSG_END(self) - pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 0, field_val);

        // can we get the rest?
        if (MSG_END(self) - pos < 18) {
            PyErr_SetString(PoqaioProtocolError, "Invalid field description.");
            goto error;
        }

        // table oid
        field_val = read_py_uint32(&pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 5, field_val);

        // column number (in originating table)
        field_val = read_py_int16(&pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 6, field_val);

        // data type oid
        oid = read_uint32(&pos);
        field_val = PyLong_FromUnsignedLong(oid);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 1, field_val);
        self->result_oids[i] = oid;

        // data type size
        field_val = read_py_int16(&pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 2, field_val);

        // data type modifier
        field_val = read_py_int32(&pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 3, field_val);

        // field format
        field_val = read_py_int16(&pos);
        if (field_val == NULL) {
            goto error;
        }
        PyStructSequence_SET_ITEM(field_desc, 4, field_val);
    }

    if (pos != MSG_END(self)) {
        PyErr_SetString(
            PoqaioProtocolError, "Invalid field description, data remaining.");
        goto error;
    }
    self->result_data = PyList_New(0);
    if (self->result_data == NULL) {
        goto error;
    }
    self->result_nfields = nfields;
    self->result_fields = fields;
    return 0;

error:
    Py_DECREF(fields);
    return -1;
}


static int
handle_data_row(BaseProt *self) {
    int i, ret=-1;
    char *pos;
    int16_t nfields;
    PyObject *row;

    if (self->error) {
        // Error already set, ignore message
        return 0;
    }

    if (!self->uses_utf8) {
        PyErr_SetString(
            PoqaioProtocolError, "Client encoding is not set to UTF-8");
    }

    if (self->result_data == NULL) {
        PyErr_SetString(
            PoqaioProtocolError,
            "Invalid state for data row."
            );
    }

    pos = MSG_BODY(self);

    // Get number of fields
    if (read_int16_check(self, &pos, &nfields) == -1)
        return -1;

    if (nfields != self->result_nfields) {
        PyErr_SetString(
            PoqaioProtocolError,
            "Invalid data row, number of values differs from row description."
            );
    }
    row = PyTuple_New(nfields);
    if (row == NULL) {
        return -1;
    }
    for (i = 0; i < nfields; i++) {
        PyObject *val;
        int32_t val_size;

        // Get value size
        if (read_int32_check(self, &pos, &val_size) == -1)
            return -1;

        if (val_size == -1) {
            val = Py_None;
            Py_INCREF(val);
        }
        else {
            if (check_length_gte(self, pos, val_size) == -1) {
                goto error;
            }

            val = convert_result_val(self, pos, val_size, self->result_oids[i]);
            if (val == NULL) {
                goto error;
            }
            pos += val_size;
        }

        PyTuple_SET_ITEM(row, i, val);
    }
    if (pos != MSG_END(self)) {
        PyErr_SetString(
            PoqaioProtocolError, "Invalid field description, data remaining.");
        goto error;
    }

    if (PyList_Append(self->result_data, row) == -1)
        goto error;

    ret = 0;

error:
    Py_DECREF(row);
    return ret;
}


static int
handle_command_complete(BaseProt *self) {
    char *pos;
    PyObject *py_val, *result, *results;

    if (self->result_oids) {
        PyMem_Free(self->result_oids);
        self->result_oids = NULL;
    }
    result = PyStructSequence_New(Result);
    if (result == NULL) {
        return -1;
    }

    pos = MSG_BODY(self);

    // fields
    py_val = self->result_fields;
    if (py_val == NULL) {
        py_val = Py_None;
        Py_INCREF(Py_None);
    }
    else {
        self->result_fields = NULL;
    }
    PyStructSequence_SET_ITEM(result, 0, py_val);

    // data
    py_val = self->result_data;
    if (py_val == NULL) {
        py_val = Py_None;
        Py_INCREF(Py_None);
    }
    else {
        // no decref, sequence will steal
        self->result_data = NULL;
    }
    PyStructSequence_SET_ITEM(result, 1, py_val);

    // tag
    py_val = read_pystr(&pos, MSG_END(self) - pos);
    if (py_val == NULL) {
        goto error;
    }
    PyStructSequence_SET_ITEM(result, 2, py_val);

    // add result to result list
    results = self->results;
    if (results == NULL) {
        results = PyList_New(1);
        if (results == NULL) {
            goto error;
        }
        self->results = results;
        PyList_SET_ITEM(results, 0, result);
    }
    else {
        if (PyList_Append(results, result) == -1) {
            goto error;
        }
        Py_DECREF(result);
    }
    return 0;

error:
    Py_DECREF(result);
    return -1;
}


static int
handle_ready(BaseProt *self) {
    char status;

    if (check_length(self, 1) == -1) {
        return -1;
    }
    status = MSG_BODY(self)[0];
    switch (status) {
        case 'I':
        case 'E':
        case 'T':
            self->transaction_status = status;
            break;
        default: {
            char status_string[2] = {status, 0};
            PyErr_Format(
                PoqaioProtocolError,
                "Invalid transaction code in ReadyResponse, got '%s'",
                status_string);
            return -1;
        }
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
        Py_CLEAR(self->results);
    }
    else {
        if (!done) {
            PyObject *result;

            if (self->results == NULL) {
                result = Py_None;
                Py_INCREF(result);
            }
            else {
                result = self->results;
                self->results = NULL;
            }
            res = PyObject_CallMethod(
                self->fut, "set_result", "O", result);
            Py_DECREF(result);

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
handle_error(BaseProt *self) {
    PyErr_SetString(PoqaioProtocolError, "ERROR!!!!!");
    return 0;
}


static int
handle_message(BaseProt *self) {
    int res;

//    printf("Message received: %c\n", self->curr_msg[0]);
    switch (self->curr_msg[0]) {
        case 'R':
            res = handle_auth_req(self);
            break;
        case 'Z':
            res = handle_ready(self);
            break;
        case 'K':
            res = handle_backend_key(self);
            break;
        case 'S':
            res = handle_parameter_status(self);
            break;
        case 'T':
            res = handle_row_description(self);
            break;
        case 'C':
            res = handle_command_complete(self);
            break;
        case 'D':
            res = handle_data_row(self);
            break;
        case 'n':  // NoData
        case '1':  // ParseComplete
        case '2':  // BindComplete
            res = check_length(self, 0);
            break;
        case 'N':  // TODO: Handle notice
            res = 0;
            break;
        case 'E':
            res = handle_error(self);
            break;
        default: {
            char identifier[2] = {self->curr_msg[0], 0};
            PyErr_Format(
                PoqaioProtocolError,
                "Unknown identifier '%s'",
                identifier);
            return -1;
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

    // write protocol version and user
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

//    printf("Startup sent\n");

    // create a future to report on later
    self->fut = PyObject_CallMethod(self->loop, "create_future", NULL);
    return self->fut;
}


PyObject *
BaseProt_execute(BaseProt *self, PyObject *args) {
    char *query, *buf, *pos;
    Py_ssize_t query_len;
    PyObject *py_params, *ret, *py_buf;
    Py_ssize_t msg_size, num_params=0, i;
    int32_t msize;

    if (!PyArg_ParseTuple(args, "s#O", &query, &query_len, &py_params)) {
        return NULL;
    }

    if (py_params != Py_None) {
        py_params = PySequence_Fast(
                py_params, "Parameters must be a sequence or None");
        if (py_params == NULL) {
            return NULL;
        }
        num_params = PySequence_Fast_GET_SIZE(py_params);
        if (num_params == 0) {
            Py_DECREF(py_params);
        }
    }

    if (num_params == 0) {
        // simple query

        msg_size = query_len + 6;  // query length + term zero + length + identifier
        buf = PyMem_Malloc(msg_size);  // including identifier
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        // write identifier
        buf[0] = 'Q';
        pos = buf + 1;

        // write msg length
        msize = (int32_t)htobe32((uint32_t)(msg_size - 1));
        outbuf_write(&pos, &msize, sizeof(msize));

        // write query
        outbuf_write(&pos, query, query_len);
        pos[0] = '\0';
    }
    else {
        Param *params;
        int32_t value_size = 0;
        int16_t val16;
        uint32_t uval32;
        static char fin[] = (
                "D\0\0\0\x06P\0"         // describe
                "E\0\0\0\x09\0\0\0\0\0"  // execute
                "H\0\0\0\x04"            // flush
                "S\0\0\0\x04"            // sync
                );

//        printf("Complex query\n");

        if (num_params > INT16_MAX) {
            PyErr_SetString(
                PyExc_ValueError,
                "Too many parameters provided. Maximum number is 32767");
            return NULL;
        }

        params = PyMem_Calloc(num_params, sizeof(Param));
        if (params == NULL) {
            return PyErr_NoMemory();
        }
//        printf("Complex query 2\n");

        for (i = 0; i < num_params; i++) {
            PyObject *py_param =  PySequence_Fast_GET_ITEM(py_params, i);
            Param *param = params + i;
            if (fill_param(param, py_param) == -1) {
                return NULL;
            }
            if (param->size > 0) {
                value_size += param->size;
            }
        }

//        printf("Complex query 3\n");

        // Parse Message: 9 + querylen + 4 * num_params
        //     'P'(1) + size(4) + empty name (1) + query string (len) +
        //     term zero(1) + num_params (2) + param_oids (4 * num_params)
        //
        // Bind Message: 15 + 6 * num_params + param_value_length
        //     'B'(1) + size(4) + empty portal name (1) +
        //     empty statement name (1) + num_params (2) +
        //     parameter formats (2 * num_params) + num_params (2) +
        //     [param_length + param_value]* (4 * num_params + param_value_length)
        //     num_format_codes (2) + format_code (2)

        msg_size = 51 + 10 * num_params + query_len + value_size;
        buf = PyMem_Malloc(msg_size);  // including identifier
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        // 0: Parse identifier
        buf[0] = 'P';
        pos = buf + 1;
        // 1: Parse message size
        msize = (int32_t)htobe32((uint32_t)(8 + query_len + 4 * num_params));
        outbuf_write(&pos, &msize, sizeof(msize));
        // 5: Empty name
        pos[0] = '\0';
        pos++;
        // 6: Query
        outbuf_write(&pos, query, query_len);
        // 6 + querylen: Query terminator
        pos[0] = '\0';
        pos++;
        // 7 + querylen: Number of parameter oids
        val16 = (int16_t)htobe16((uint16_t)num_params);
        outbuf_write(&pos, &val16, sizeof(val16));
        // 9 + querylen: Parameter oids
        for (i = 0; i < num_params; i++) {
            uval32 = htobe32(params[i].oid);
            outbuf_write(&pos, &uval32, sizeof(uval32));
        }

        // 9 + querylen + 4 * num_params: Bind identifier
        pos[0] = 'B';
        pos++;
        // 10 + querylen + 4 * num_params: Bind message size
        msize = (int32_t)htobe32((uint32_t)(14 + 6 * num_params + value_size));
        outbuf_write(&pos, &msize, sizeof(msize));
        // 14 + querylen + 4 * num_params: Empty portal name
        pos[0] = '\0';
        pos++;
        // 15 + querylen + 4 * num_params: Empty statement name
        pos[0] = '\0';
        pos++;
        // 16 + querylen + 4 * num_params: Number of parameter formats
        val16 = (int16_t)htobe16((uint16_t)num_params);
        outbuf_write(&pos, &val16, sizeof(val16));
        // 18 + querylen + 4 * num_params: Parameter formats
        for (i = 0; i < num_params; i++) {
            val16 = (int16_t)htobe16((uint16_t)params[i].format);
            outbuf_write(&pos, &val16, sizeof(val16));
        }
        // 18 + querylen + 6 * num_params: Number of parameter values
        val16 = (int16_t)htobe16((uint16_t)num_params);
        outbuf_write(&pos, &val16, sizeof(val16));
        // 20 + querylen + 6 * num_params: Parameter values
        for (i = 0; i < num_params; i++) {
            Param *param = params + i;
            int32_t size = (int32_t)param->size;

            msize = (int32_t)htobe32((uint32_t)size);
            outbuf_write(&pos, &msize, sizeof(msize));
            if (size > 0) {
                param->write(param, pos);
                pos += size;
            }
        }
        // 20 + querylen + 10 * num_params + param_values:
        //      Number of result format codes
        val16 = (int16_t)htobe16(1);
        outbuf_write(&pos, &val16, sizeof(val16));
        // 22 + querylen + 10 * num_params + param_values: Result format codes
        val16 = (int16_t)htobe16(0);
        outbuf_write(&pos, &val16, sizeof(val16));

        // 24 + querylen + 10 * num_params + param_values: Final messages
        outbuf_write(&pos, fin, 27);
        // 51 + querylen + 10 * num_params + param_values: end

        for (i = 0; i < num_params; i++) {
            Param *param = params + i;
            if (param->free) {
                param->free(param);
            }
        }
    }

    // really send it
    ret = PyObject_CallMethod(
        self->transport, "write", "y#", buf, msg_size);
    PyMem_Free(buf);
    if (ret == NULL) {
        return NULL;
    }
    Py_DECREF(ret);

    // create a future to report on later
    self->fut = PyObject_CallMethod(self->loop, "create_future", NULL);
    return self->fut;

}


static PyObject *
BaseProt_get_buffer(BaseProt *self, PyObject *args)
{

    // A message must always fit entirely in a buffer. Easy for parsing.
    // There are two buffers, a fixed size one called 'in_buf' and an
    // dynamic one 'in_extra_buf'for large messages.

    // Return a view on the (remaining) buffer
    char *buf;
    int buf_size;

//    printf("Getting buffer\n");

    if (self->in_extra_buf) {
        buf = self->in_extra_buf;
        buf_size = self->msg_length;
    }
    else {
        buf = self->in_buf;
        buf_size = BUF_SIZE;
    }

    return PyMemoryView_FromMemory(
        buf + self->received_bytes,
        buf_size - self->received_bytes,
        PyBUF_WRITE);
}


static int
_BaseProt_buffer_updated(BaseProt *self)
{
    int32_t length;
    int ret;

    if (RECEIVING_HEADER(self)) {
//        printf("Identifier: %c\n", self->curr_msg[0]);
        length = get_int32(self->curr_msg + 1);
        // TODO: check for negative msg_length;
//        if (length < 4) {
//                raise
//        }

        self->msg_length = length + 1;  // body including length plus identifier
        if (self->msg_length > BUF_SIZE) {
            // big buffer is needed
            char *buf;

            buf = PyMem_Malloc(self->msg_length);
            if (buf == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            self->in_extra_buf = buf;

            // copy already received data (header) into big buffer
            memcpy(buf, self->curr_msg, self->received_bytes);
            self->curr_msg = buf;
            return 0;
        }

        if (self->msg_length > self->received_bytes) {
            // incomplete message
            return 0;
        }
    }
//    self->message = PyMemoryView_FromMemory(
//        self->buf_in_use + HEADER_SIZE, self->msg_length - HEADER_SIZE,
//        PyBUF_WRITE);

    ret = handle_message(self);

    // Done with message. Clean up
//    Py_CLEAR(self->message);
    if (self->in_extra_buf) {
        // clear large buffer
        PyMem_Free(self->in_extra_buf);
        self->in_extra_buf = NULL;
        self->received_bytes = 0;
        self->curr_msg = self->in_buf;
    }
    else {
        self->received_bytes -= self->msg_length;
        self->curr_msg = self->curr_msg + self->msg_length;
    }
    self->msg_length = HEADER_SIZE;
    return ret;
}


static PyObject *
BaseProt_buffer_updated(BaseProt *self, PyObject *args)
{
    // This is called from the transport when data is available. Raising
    // errors makes no sense, because those do not end up in user space.
    // Make sure to gather exceptions and store those for later to set them
    // on the future.
    long nbytes;
    PyObject *py_nbytes;

//    printf("Data received\n");

    py_nbytes = PyTuple_GET_ITEM(args, 0);
    if (py_nbytes == NULL) {
        return NULL;
    }
    nbytes = PyLong_AsLong(py_nbytes);
    if (nbytes == -1 && PyErr_Occurred()) {
        return NULL;
    }

    if (nbytes < 0) {
        PyErr_SetString(
            PoqaioProtocolError, "invalid value for received bytes");
        // TODO: abort
        return NULL;
    }

    self->received_bytes += nbytes;
    while (self->received_bytes >= self->msg_length) {
        if (_BaseProt_buffer_updated(self) == -1) {
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
                    if (!_is_closing) {
                        if (PyObject_CallMethod(
                                self->transport, "close", NULL) == NULL) {
                            return NULL;
                        }
                    }
                }
            }
        }
    }

    if (!self->in_extra_buf && self->curr_msg != self->in_buf) {
        // Positioned after last handled message
        if (self->received_bytes) {
//            printf("partial=======================\n");
            // Partial message, move to beginning
            memmove(self->in_buf, self->curr_msg, self->received_bytes);
        }
        // Position at start
        self->curr_msg = self->in_buf;
    }
    Py_RETURN_NONE;
}


static PyMemberDef BaseProt_members[] = {
    {"error", T_OBJECT, offsetof(BaseProt, error), 0, ""}, // remove
    {"fut", T_OBJECT, offsetof(BaseProt, fut), 0, ""}, // remove
    {"transaction_status", T_CHAR, offsetof(BaseProt, transaction_status),
     READONLY, "transaction status"
    },
    {"status_parameters", T_OBJECT, offsetof(BaseProt, status_parameters),
     READONLY, "status parameters"
    },
    {"password", T_STRING, offsetof(BaseProt, password), READONLY, "password"},
    {"user", T_STRING, offsetof(BaseProt, user), READONLY, "user"},
    {"transport", T_OBJECT, offsetof(BaseProt, transport), 0, ""},
    {NULL}
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
    {"execute", (PyCFunction) BaseProt_execute, METH_VARARGS,
     "execute"},
    {NULL}
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
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    BaseProt_new                                /* tp_new */
};
