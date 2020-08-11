typedef struct {
    PyObject_HEAD
    char *in_buf;            // default receive buffer
    char *in_extra_buf;      // XL jumbo buffer, dynamically allocated
    char *curr_msg;          // pointer in buffer to current message

    char *password;
    char *user;

    int32_t msg_length;      // length of current message
    int32_t received_bytes;  // number of received bytes in current buffer

    char uses_utf8;
    char transaction_status;
    char uses_iso;

    PyObject *loop;
    PyObject *transport;
    PyObject *fut;

    int16_t result_nfields;
    PyObject *results;
    PyObject *result_fields;
    uint32_t *result_oids;
    PyObject *result_data;

    PyObject *error;
    PyObject *status_parameters;
    PyObject *wr_list;
    int32_t backend_process_id;
    int32_t backend_secret_key;
} BaseProt;

extern PyTypeObject BaseProtType;
