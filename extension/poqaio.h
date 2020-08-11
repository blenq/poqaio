#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include "portable_endian.h"

extern PyObject *PoqaioError;
extern PyObject *PoqaioServerError;
extern PyObject *PoqaioProtocolError;

extern PyTypeObject *FieldDescription;
extern PyTypeObject *Result;


#define INT2OID 21
#define INT4OID 23
#define INT8OID 20
#define OIDOID 26
#define FLOAT4OID 700
#define FLOAT8OID 701
#define BOOLOID 16
#define XIDOID 28
#define CIDOID 29

#define TEXTOID 25
