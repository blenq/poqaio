#ifndef POQAIO_PROTOCOL_H
#define POQAIO_PROTOCOL_H

typedef struct _BaseProt BaseProt;

typedef PyObject *(*converter)(BaseProt *, char *, int32_t);

converter get_converter(uint32_t);

typedef struct _BaseProt {
    PyObject_HEAD
    char *in_buf;            // default receive buffer
    char *in_extra_buf;      // XL jumbo buffer, dynamically allocated
    char *curr_msg;          // pointer in buffer to current message
    PyObject *default_buf;

    char *password;
    char *user;

    int32_t msg_length;      // length of current message
    int32_t received_bytes;  // number of received bytes in current buffer

    int uses_utf8;
    char transaction_status;
    char uses_iso;

    PyObject *loop;
    PyObject *create_future;
    PyObject *transport;
    PyObject *fut;

    int16_t result_nfields;
    PyObject *results;
    PyObject *result_fields;
    PyObject *result_data;
    converter *converters;

    PyObject *transport_write;
    PyObject *error;
    PyObject *status_parameters;
    PyObject *wr_list;
    int32_t backend_process_id;
    int32_t backend_secret_key;
} BaseProt;

extern PyTypeObject BaseProtType;

#endif
