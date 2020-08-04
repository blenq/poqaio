import enum


class TransactionStatus(enum.Enum):
    UNKNOWN = '\0'
    IDLE = 'I'
    TRANSACTION = 'T'
    ERROR = 'E'


class Severity(enum.Enum):
    ERROR = 'ERROR'
    FATAL = 'FATAL'
    PANIC = 'PANIC'
    WARNING = 'WARNING'
    NOTICE = 'NOTICE'
    DEBUG = 'DEBUG'
    INFO = 'INFO'
    LOG = 'LOG'
    UNKNOWN = 'UNKNOWN'

# class Error(Exception):
#     __module__ = 'poqaio'

# class ProtocolError(Error):
#     __module__ = 'poqaio'

# def _get_int(val):
#     if isinstance(val, (int, type(None))):
#         return val
#     try:
#         return int(val)
#     except Exception:
#         return val

# class ServerError(Error):
#     __module__ = 'poqaio'

#     def __init__(
#             self, severity, code, message, detail=None, hint=None,
#             position=None, internal_position=None, internal_query=None,
#             where=None, schema_name=None, table_name=None, column_name=None,
#             data_type_name=None, constraint_name=None, file_name=None,
#             line_number=None, routine_name=None):
#         self.severity = severity
#         self.code = code
#         self.message = message
#         self.detail = detail
#         self.hint = hint
#         self.position = _get_int(position)
#         self.internal_position = _get_int(internal_position)
#         self.internal_query = internal_query
#         self.where = where
#         self.schema_name = schema_name
#         self.table_name = table_name
#         self.column_name = column_name
#         self.data_type_name = data_type_name
#         self.constraint_name = constraint_name
#         self.file_name = file_name
#         self.line_number = _get_int(line_number)
#         self.routine_name = routine_name
