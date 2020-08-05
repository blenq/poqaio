#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include "portable_endian.h"

extern PyObject *PoqaioError;
extern PyObject *PoqaioServerError;
extern PyObject *PoqaioProtocolError;

extern PyTypeObject *FieldDescription;
extern PyTypeObject *Result;
