#include "poqaio.h"
#include "protocol.h"


static struct PyModuleDef poqaio_module = {
    PyModuleDef_HEAD_INIT,
    "_poqaio",   /* name of module */
    NULL,			    /* module documentation, may be NULL */
    -1,                 /* size of per-interpreter state of the module,
                       or -1 if the module keeps state in global variables. */
    NULL
};


PyObject *PoqaioError;
PyObject *PoqaioServerError;
PyObject *PoqaioProtocolError;


PyMODINIT_FUNC
PyInit__poqaio(void)
{
	PyObject *m;

	PyObject *common = PyImport_ImportModule("poqaio.common");


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
