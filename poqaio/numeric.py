from .public_const import INT4OID, INT8OID, TEXTOID, FLOAT8OID, BOOLOID


def convert_int_param(val):
    if -0x80000000 <= val <= 0x7FFFFFFF:
        oid = INT4OID
    elif -0x8000000000000000 <= val <= 0x7FFFFFFFFFFFFFFF:
        oid = INT8OID
    else:
        oid = TEXTOID
    val = str(val)
    return oid, val


def convert_int_result(val):
    return int(val)


def convert_bool_param(val):
    val = b'1' if val else b'0'
    return BOOLOID, val


def convert_bool_result(val):
    return val == b't'


def convert_float_param(val):
    return FLOAT8OID, str(val)


def convert_float_result(val):
    return float(val)
