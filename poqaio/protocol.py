import asyncio
from codecs import decode
import hashlib
import struct

from ._poqaio import BaseProt, ServerError, ProtocolError
from .common import Severity
from . import numeric
from .text import convert_any_param, convert_str_param
from . import public_const

BUFFER_SIZE = 8192

#
# _error_fields = {
#     "C": "code",
#     "M": "message",
#     "D": "detail",
#     "H": "hint",
#     "P": "position",
#     "p": "internal_position",
#     "q": "internal_query",
#     "w": "where",
#     "s": "schema_name",
#     "t": "table_name",
#     "c": "column_name",
#     "d": "data_type_name",
#     "n": "constraint_name",
#     "F": "file_name",
#     "L": "line_number",
#     "R": "routine_name",
# }

_error_fields = {
    "C": 1,
    "M": 2,
    "D": 3,
    "H": 4,
    "P": 5,
    "p": 6,
    "q": 7,
    "w": 8,
    "s": 9,
    "t": 10,
    "c": 11,
    "d": 12,
    "n": 13,
    "F": 14,
    "L": 15,
    "R": 16,
}

param_converters = {
    int: numeric.convert_int_param,
    str: convert_str_param,
    float: numeric.convert_float_param,
    bool: numeric.convert_bool_param,
}

result_converters = {
    public_const.INT2OID: numeric.convert_int_result,
    public_const.INT4OID: numeric.convert_int_result,
    public_const.INT8OID: numeric.convert_int_result,
    public_const.OIDOID: numeric.convert_int_result,
    public_const.FLOAT8OID: numeric.convert_float_result,
    public_const.FLOAT4OID: numeric.convert_float_result,
    public_const.BOOLOID: numeric.convert_bool_result,
}


class PGProtocol(BaseProt, asyncio.BufferedProtocol):

    int_struct = struct.Struct('!i')
    intint_struct = struct.Struct('!ii')
    short_struct = struct.Struct('!h')

    def __init__(self):
        self.loop = asyncio.get_running_loop()
#         self.received_bytes = 0

        self.out_buffer = memoryview(bytearray(BUFFER_SIZE))
#         self.results = None
#         self.result = None

    def connection_lost(self, exc):
        if self.error:
            self.fut.set_exception(self.error)
            self.error = None
        if exc:
            self.set_exception(exc)
        self.transport = None

    def set_exception(self, ex):
        if (isinstance(ex, (ProtocolError)) or self.transport.is_closing()):
            if not self.fut.done():
                self.fut.set_exception(ex)
            self.transport.close()
        else:
            self.error = ex

    def set_result(self, result=None):
        if not self.fut.done():
            self.fut.set_result(result)

    def check_length_equal(self, length):
        if len(self.message) != length:
            raise ProtocolError(
                "Invalid length for message with identifier "
                f"'{chr(self.identifier)}'. Expected {length}, but got"
                f"{len(self.message)}.")

    def handle_auth_req(self):
        # clear password from the object
        password = self.password

        specifier = self.int_struct.unpack_from(self.message)[0]
        if specifier == 0:
            self.check_length_equal(4)
            return
        if specifier == 5:
            self.check_length_equal(8)
            user = self.user
            if password is None:
                raise ProtocolError("Missing password")
            salt = struct.unpack_from("4s", self.message, 4)[0]
            if isinstance(password, str):
                password = password.encode()
            if isinstance(user, str):
                user = user.encode()
            password = (
                b'md5' + hashlib.md5(
                    hashlib.md5(password + user).hexdigest().encode() + salt
                ).hexdigest().encode())

            pw_len = len(password) + 1
            struct_fmt = f'!ci{pw_len}s'
            data = struct.pack(struct_fmt, b'p', pw_len + 4, password)
            self.transport.write(data)
            return
        raise ProtocolError(
            f"Unknown authentication specifier: {specifier}")

#     def handle_ready(self):
#         self.check_length_equal(1)
#         value = self.message[0]
#         try:
#             self.transaction_status = TransactionStatus._from_protocol(value)
#         except Exception:
#             raise ProtocolError("Invalid transaction status in ready message")
#         if self.starting_up:
#             self.starting_up = False
#         if self.error:
#             self.fut.set_exception(self.error)
#             self.error = None
#         else:
#             self.set_result(self.results)
#             self.results = None

#     def handle_parameter_status(self):
#         # format: "{param_name}\0{param_value}\0"
# 
#         param = decode(self.message)
#         param = param.split('\0')
#         if len(param) != 3 or param[2] != '':
#             self.set_exception(
#                 ProtocolError("Invalid parameter status message"))
#             return
#         name, val = param[:2]
#         if name == "client_encoding":
#             self.client_encoding = val
#         elif name == "DateStyle":
#             self.date_style = val
#         elif name == "integer_datetimes":
#             self.integer_datetimes = (val == 'on')
#         else:
#             self.parameters[name] = val

    def handle_error(self):
        # format: "({error_field_code:char}{error_field_value}\0)+\0"
        if self.message[-2:] != b'\0\0':
            raise ProtocolError("Invalid Error Response")
        messages = decode(self.message[:-2])
        messages = {msg[:1]: msg[1:] for msg in messages.split('\0')}
        ex_args = [None] * 17

        try:
            localized_severity = messages.pop('S')
        except KeyError:
            raise ProtocolError(
                "Missing localized severity 'S' in Error Response")
        try:
            severity = messages.pop('V')
        except KeyError:
            # fallback to localized version (< 9.6) and hope it is in English
            severity = localized_severity
        try:
            severity = Severity(severity)
        except ValueError:
            severity = Severity.UNKNOWN
        ex_args[0] = severity
        for k, v in messages.items():
            if k in ('p', 'P', 'L'):
                try:
                    v = int(v)
                except Exception:
                    pass
            try:
                idx = _error_fields[k]
            except KeyError:
                continue
            ex_args[idx] = v

        if ex_args[1] is None:
            raise ProtocolError("Missing code in Error Response")
        if ex_args[2] is None:
            raise ProtocolError("Missing message in Error Response")
        raise ServerError(ex_args)

    def handle_notice(self):
        pass

#     def handle_row_description(self):
#         fields = []
#         result = self.get_current_result()
#         result.update(data=[], fields=fields)
# 
#         msg_len = len(self.message)
#         num_fields = self.short_struct.unpack_from(self.message)[0]
#         pos = 2
#         buffer = bytes(self.message)
#         for _ in range(num_fields):
#             if pos >= msg_len:
#                 raise ProtocolError("Invalid row description")
#             try:
#                 zidx = buffer.index(0, pos)
#             except ValueError:
#                 raise ProtocolError("Invalid row description")
#             field_name = decode(self.message[pos:zidx])
#             pos = zidx + 1
#             if pos >= msg_len:
#                 raise ProtocolError("Invalid row description")
#             field_struct = struct.Struct(f"!IhIhih")
#             field = field_struct.unpack_from(buffer, pos)
#             fields.append({
#                 "field_name": field_name,
#                 "table_oid": field[0],
#                 "col_num": field[1],
#                 "type_oid": field[2],
#                 "typ_size": field[3],
#                 "typ_mod": field[4],
#                 "format": field[5],
#                 })
#             pos += field_struct.size
#         if pos != msg_len:
#             raise ProtocolError("Invalid row description")

    def convert_data(self, value, field):
        if field["format"] == 0:
            converter = result_converters.get(field["type_oid"])
            if converter is None:
                value = decode(value)
                return value
            return converter(value)
        else:
            return bytes(value)

#     def handle_data_row(self):
#         result = self.result
#         fields = result["fields"]
#         msg = self.message
#         num_fields = self.short_struct.unpack_from(msg)[0]
#         if num_fields != len(fields):
#             raise ProtocolError("Invalid data row 1")
#         msg_len = len(msg)
# 
#         def get_fields():
#             pos = 2
#             for field in fields:
#                 if pos >= msg_len:
#                     raise ProtocolError("Invalid data row 2")
#                 val_len = self.int_struct.unpack_from(msg, pos)[0]
#                 pos += 4
# 
#                 if val_len == -1:
#                     yield None
#                 else:
#                     yield self.convert_data(msg[pos:pos + val_len], field)
#                     pos += val_len
# 
#             if pos != msg_len:
#                 raise ProtocolError("Invalid data row 3")
# 
#         result["data"].append(tuple(get_fields()))

#     def get_current_result(self):
#         if self.result is not None:
#             return self.result
#         result = self.result = {
#             "fields": None, "data": None, "command_status": None}
#         if self.results is None:
#             self.results = []
#         self.results.append(result)
#         return result

#     def handle_command_complete(self):
#         msg = self.message
#         if msg[-1] != 0:
#             raise ProtocolError("Invalid command complete")
#         result = self.get_current_result()
#         result["command_status"] = decode(msg[:-1])
#         self.result = None

#     def handle_parse_complete(self):
#         # print("Parse complete")
#         pass
# 
#     def handle_bind_complete(self):
#         # print("Bind complete")
#         pass

#     def handle_no_data(self):
#         self.check_length_equal(0)

#     handle_message_49 = handle_parse_complete
#     handle_message_50 = handle_bind_complete
#     handle_message_67 = handle_command_complete
#     handle_message_68 = handle_data_row
    handle_message_69 = handle_error
    handle_message_78 = handle_notice
    handle_message_82 = handle_auth_req
#     handle_message_83 = handle_parameter_status
#     handle_message_84 = handle_row_description
#     handle_message_110 = handle_no_data
#     handle_message_90 = handle_ready

    def handle_message(self):
        handle_method = getattr(
            self, f"handle_message_{self.identifier}", None)
        if handle_method is None:
            self.set_exception(
                ValueError(f"Unknown identier: {chr(self.identifier)}"))
            return
        handle_method()

#     def startup(self, user, database, application_name, password):
#         parameters = []
#         struct_format = ["!ii"]
# 
#         def add_parameter(name, value):
#             if not value:
#                 return
# 
#             name = name.encode()
#             value = value.encode()
#             struct_format.extend([f"{len(name) + 1}s", f"{len(value) + 1}s"])
# 
#             parameters.extend([name, value])
# 
#         for name, value in [
#                 ("user", user),
#                 ("database", database),
#                 ("application_name", application_name),
#                 ("DateStyle", "ISO"),
#                 ("client_encoding", "UTF8\0")]:
#             if value:
#                 add_parameter(name, value)
# 
#         msg_struct = struct.Struct(''.join(struct_format))
#         message = msg_struct.pack(msg_struct.size, 196608, *parameters)
# 
#         self.user = user
#         self.password = password
# 
#         self.transport.write(message)
#         self.fut = self.loop.create_future()
#         return self.fut

    def get_wire_param(self, val):
        converter = param_converters.get(type(val), convert_any_param)
        oid, val = converter(val)
        if not isinstance(val, bytes):
            if isinstance(val, str):
                val = val.encode()
            else:
                raise ValueError("Converter returned invalid type")
        return oid, len(val), val, 0

    def execute(self, query, parameters):
#         self.results = None
        query = query.encode()
        query_len = len(query) + 1  # includes zero terminator

        if parameters:
            # extended query protocol
            wire_params = []
            for param in parameters:
                if param is None:
                    wire_params.append((0, -1, b'', 0))
                else:
                    wire_params.append(self.get_wire_param(param))
            num_params = len(wire_params)

            # Parse Message: 'P'(1) + size(4) + empty name (1) +
            #    query string (len) + num_params (2) +
            #    param_oids (4 * num_params)
            parse_length = 7 + num_params * 4 + query_len  # without 'P'
            struct_args = [b'P', parse_length, 0, query, num_params]
            struct_args += [p[0] for p in wire_params]

            struct_fmt = [f'!ciB{query_len}sh', 'I' * num_params]

            # Bind Message: 'B'(1) + size(4) + empty portal name (1) +
            #     empty statement name (1) + num_params (2) +
            # parameter formats (2 * num_params) +
            # num_params (2) +
            # [param_length + param_value]* (4 * num_params + total_param_length)
            # num_format_codes (2) + format_code (2)
            total_param_length = sum(p[1] for p in wire_params if p[1] != -1)
            bind_length = 14 + num_params * 6 + total_param_length
            struct_args += [b'B', bind_length, b'\0\0', num_params]
            struct_args += [p[3] for p in wire_params]
            struct_args.append(num_params)
            for p in wire_params:
                struct_args.extend([p[1], p[2]])
            struct_args += [1, 0]

            struct_fmt += ['ci2sh', 'h' * num_params, 'h']
            for p in wire_params:
                if p[1] == -1:
                    struct_fmt.append('i0s')
                else:
                    struct_fmt.append(f'i{p[1]}s')
            struct_fmt.append('hh')

            # Describe Message: 'D'(1) + size(4) + 'P'(1) +
            #     empty portal name (1)
            # Execute Message: 'E'(1) + size(4) + empty portal name (1) +
            #     number of rows (4)
            # Flush Message: 'H'(1) + size(4)
            # Sync command\: 'S'(1) + size(4)
            struct_args += [
                b'D\0\0\0\x06P\0'  # describe
                b'E\0\0\0\x09\0\0\0\0\0'  # execute
                b'H\0\0\0\x04'  # flush
                b'S\0\0\0\x04'  # sync
            ]
            struct_fmt.append("27s")

            struct_fmt = ''.join(struct_fmt)
        else:
            # simple query protocol
            content_length = query_len + 4  # including length and term zero
            struct_fmt = f"!ci{query_len}s"
            struct_args = [b'Q', content_length, query]

        msg_struct = struct.Struct(struct_fmt)
        msg_size = msg_struct.size
        if msg_size > BUFFER_SIZE:
            msg = msg_struct.pack(*struct_args)
        else:
            msg_struct.pack_into(self.out_buffer, 0, *struct_args)
            msg = self.out_buffer[:msg_size]

        self.transport.write(msg)
        fut = self.fut = self.loop.create_future()
        return fut

    def close(self):
        transport = self.transport
        if not transport.is_closing():
            transport.write(b'X\0\0\0\x04')
            transport.close()
