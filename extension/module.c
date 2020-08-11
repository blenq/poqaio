#include "poqaio.h"
#include "protocol.h"


static struct PyModuleDef poqaio_module = {
    PyModuleDef_HEAD_INIT,
    "_poqaio",   /* name of module */
    NULL,                /* module documentation, may be NULL */
    -1,                 /* size of per-interpreter state of the module,
                       or -1 if the module keeps state in global variables. */
    NULL
};


PyObject *PoqaioError;
PyObject *PoqaioServerError;
PyObject *PoqaioProtocolError;

PyTypeObject *FieldDescription;
PyTypeObject *Result;


PyMODINIT_FUNC
PyInit__poqaio(void)
{
    PyObject *m;

    PyStructSequence_Field fd_fields[] = {
        {"field_name", PyDoc_STR("field name")},
        {"type_oid", PyDoc_STR("type oid")},
        {"field_size", PyDoc_STR("field size")},
        {"type_mod", PyDoc_STR("type modifier")},
        {"format", PyDoc_STR("field format")},
        {"table_oid", PyDoc_STR("table oid")},
        {"col_num", PyDoc_STR("column number in table")},
        {0}
    };
    PyStructSequence_Desc fd_desc = {
        "poqaio.FieldDescription",
        PyDoc_STR("Field description"),
        fd_fields,
        7
    };
    FieldDescription = PyStructSequence_NewType(&fd_desc);

    PyStructSequence_Field res_fields[] = {
        {"fields", PyDoc_STR("Fields")},
        {"data", PyDoc_STR("Data rows")},
        {"tag", PyDoc_STR("Tag")},
        {0}
    };
    PyStructSequence_Desc res_desc = {
        "poqaio.Result",
        PyDoc_STR("Database execution result"),
        res_fields,
        3
    };
    Result = PyStructSequence_NewType(&res_desc);

    if (PyType_Ready(&BaseProtType) < 0)
        return NULL;

    PoqaioError = PyErr_NewException("poqaio.Error", NULL, NULL);
    if (PoqaioError == NULL)
        return NULL;

    PoqaioServerError = PyErr_NewException(
            "poqaio.ServerError", PoqaioError, NULL);
    if (PoqaioServerError == NULL)
        return NULL;

    PoqaioProtocolError = PyErr_NewException(
            "poqaio.ProtocolError", PoqaioError, NULL);
    if (PoqaioProtocolError == NULL)
        return NULL;

    m = PyModule_Create(&poqaio_module);
    if (m == NULL) {
        Py_DECREF(PoqaioError);
        return NULL;
    }

    Py_INCREF(&BaseProtType);
    PyModule_AddObject(m, "BaseProt", (PyObject *) &BaseProtType);
    PyModule_AddObject(m, "Error", PoqaioError);
    PyModule_AddObject(m, "ServerError", PoqaioServerError);
    PyModule_AddObject(m, "ProtocolError", PoqaioProtocolError);

    return m;
}
