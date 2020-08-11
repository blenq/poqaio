PyObject *convert_result_val(BaseProt *, char *, int32_t, uint32_t);


typedef struct _Param Param;

typedef void (*free_param)(Param *);
typedef int (*write_param)(Param *, char *);


typedef struct {
    PyObject *str_val;
} StrContext;


typedef struct _Param {
    uint32_t oid;
    int format;
    union {
        int32_t val32;
        int64_t val64;
        char *valchr;
        double dval;
        StrContext str_ctx;
    } ctx;
    PyObject *py_val;
    Py_ssize_t size;
    write_param write;
    free_param free;
} Param;

int fill_param(Param *, PyObject *);
