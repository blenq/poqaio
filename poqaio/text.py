from .public_const import TEXTOID


def convert_any_param(val):
    return TEXTOID, str(val)


def convert_str_param(val):
    return TEXTOID, val
