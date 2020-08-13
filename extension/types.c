#include "poqaio.h"
#include "protocol.h"
#include "types.h"


static PyObject *
convert_long_result(BaseProt *self, char *data, int32_t size) {

    PyObject *ret;
    char *end;
    char term;

    term = data[size];
    data[size] = '\0';
    ret = PyLong_FromString(data, &end, 10);
    data[size] = term;

    if (end != data + size && ret != NULL) {
        Py_DECREF(ret);
        PyErr_SetString(
            PoqaioProtocolError, "Remaining data when parsing integer value");
        return NULL;
    }
    return ret;
}


static PyObject *
convert_float_result(BaseProt *self, char *data, int32_t size) {
    double val;
    char bval[size + 1];
    char *pend;

    memcpy(bval, data, size);
    bval[size] = '\0';

    val = PyOS_string_to_double(bval, &pend, PoqaioProtocolError);
    if (val == -1.0 && PyErr_Occurred())
        return NULL;
    if (pend != bval + size) {
        PyErr_SetString(PoqaioProtocolError, "Invalid floating point value");
        return NULL;
    }
    return PyFloat_FromDouble(val);
}


PyObject *
convert_text_result(BaseProt *self, char *data, int32_t size) {
    return PyUnicode_FromStringAndSize(data, size);
}

PyObject *
convert_bool_result(BaseProt *self, char *data, int32_t size) {
    if (size != 1) {
        PyErr_SetString(PoqaioProtocolError, "Invalid length for bool value");
        return NULL;
    }
    if (*data == 't')
        Py_RETURN_TRUE;
    if (*data == 'f')
        Py_RETURN_FALSE;
    PyErr_SetString(PoqaioProtocolError, "Invalid value for bool value");
    return NULL;
}

converter
get_converter(uint32_t oid)
{
    switch(oid) {
        case INT2OID:
        case INT4OID:
        case INT8OID:
        case OIDOID:
        case XIDOID:
        case CIDOID:
            return convert_long_result;
        case FLOAT4OID:
        case FLOAT8OID:
            return convert_float_result;
        case BOOLOID:
            return convert_bool_result;
        default:
            return convert_text_result;
    }
}


int write_int4(Param *param, char *dest) {
    int32_t val;

    val = htobe32(param->ctx.val32);
    memcpy(dest, &val, sizeof(int32_t));
    return 0;
}


int
fill_int4_param(Param *param, int32_t val)
{
    param->oid = INT4OID;
    param->format = 1;
    param->ctx.val32 = val;
    param->size = sizeof(int32_t);
    param->write = write_int4;
    return 0;
}


int write_int8(Param *param, char *dest) {
    int64_t val;

    val = htobe64(param->ctx.val64);
    memcpy(dest, &val, sizeof(int64_t));
    return 0;
}


int
fill_int8_param(Param *param, int64_t val)
{
    param->oid = INT8OID;
    param->format = 1;
    param->ctx.val64 = val;
    param->size = sizeof(int64_t);
    return 0;
}

void
free_obj_param(Param *param) {
    Py_DECREF(param->py_val);
}


int
write_txt(Param *param, char *dest) {
    memcpy(dest, param->ctx.valchr, param->size);
    return 0;
}


int
fill_txt_param(Param *param, PyObject *val)
{
    PyObject *py_str_val;
    char *char_val;

    py_str_val = PyObject_Str(val);
    if (py_str_val == NULL) {
        return -1;
    }
    char_val = (char *)PyUnicode_AsUTF8AndSize(py_str_val, &param->size);
    if (char_val == NULL) {
        Py_DECREF(py_str_val);
        return -1;
    }
    param->ctx.valchr = char_val;
    param->py_val = py_str_val;
    param->oid = TEXTOID;
    param->write = write_txt;
    param->free = free_obj_param;
    return 0;
}


int
fill_int_param(Param *param, PyObject *py_param)
{
    int overflow;
    long long val;

    val = PyLong_AsLongLongAndOverflow(py_param, &overflow);
    if (val == -1 && PyErr_Occurred()) {
        return -1;
    }
    if (overflow || param->oid == TEXTOID) {
        return fill_txt_param(param, py_param);
    }
    if (val < INT32_MIN || val > INT32_MAX || param->oid == INT8OID) {
        return fill_int8_param(param, val);
    }
    return fill_int4_param(param, (int32_t)val);
}


int write_bool(Param *param, char *dest)
{
    dest[0] = (param->py_val == Py_True) ? 't': 'f';
    return 0;
}


int fill_bool_param(Param *param, PyObject *py_param)
{
    param->oid = BOOLOID;
    param->size = 1;
    param->py_val = py_param;
    param->write = write_bool;
    return 0;
}


int write_float(Param *param, char *dest)
{
    if (_PyFloat_Pack8(param->ctx.dval, (unsigned char *)dest, 0) < 0) {
        return -1;
    }
    return 0;
}

int fill_float_param(Param *param, PyObject *py_param) {

    param->oid = FLOAT8OID;
    param->format = 1;
    param->size = 8;
    param->ctx.dval = PyFloat_AS_DOUBLE(py_param);
    param->write = write_float;
    return 0;
}


int fill_str_param(Param *param, PyObject *py_param) {

     param->ctx.valchr = (char *)PyUnicode_AsUTF8AndSize(py_param, &param->size);
     if (param->ctx.valchr == NULL) {
         return -1;
     }
     param->oid = TEXTOID;
     param->write = write_txt;
     return 0;
}


int
fill_param(Param *param, PyObject *py_param)
{
    PyTypeObject *typ;

    if (py_param == Py_None) {
        param->size = -1;
        if (!param->oid)
            param->oid = TEXTOID;
        return 0;
    }
    typ = py_param->ob_type;

    if (typ == &PyLong_Type) {
        return fill_int_param(param, py_param);
    }
    else if (typ == &PyUnicode_Type) {
        return fill_str_param(param, py_param);
    }
    else if (typ == &PyBool_Type) {
        return fill_bool_param(param, py_param);
    }
    else if (typ == &PyFloat_Type) {
        return fill_float_param(param, py_param);
    }
    return fill_txt_param(param, py_param);
}
