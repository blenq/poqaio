import asyncio
import getpass
import os.path
import sys

from .common import TransactionStatus
from .protocol import PGProtocol


class Result:

    def __init__(self, results):
        self._results = results


class Connection:
    def __init__(
            self, protocol, host, port, database, user, application_name,
            fallback_application_name):
        self._protocol = protocol
        self._execute = self._protocol.execute
        self.host = host
        self.port = port
        self.database = database
        self.user = user
        self._application_name = application_name or fallback_application_name
        self._execute_lock = asyncio.Lock()

    async def _startup(self, password):
#         print("starting up")
        await self._protocol.startup(
            self.user, self.database, self._application_name, password)

    @property
    def application_name(self):
        return self.status_parameters.get('application_name')
# 
    @property
    def date_style(self):
        return self.status_parameters.get('DateStyle')
# 
#     @property
#     def client_encoding(self):
#         return self._protocol.client_encoding
# 
#     @property
#     def integer_datetimes(self):
#         return self._protocol.integer_datetimes

    @property
    def is_superuser(self):
        return self.status_parameters.get('is_superuser') == 'on'

    @property
    def time_zone(self):
        return self.status_parameters.get("TimeZone")
 
    @property
    def server_version(self):
        return self.status_parameters.get("server_version")

    @property
    def transaction_status(self):
        return TransactionStatus(self._protocol.transaction_status)

    @property
    def status_parameters(self):
        return self._protocol.status_parameters

    async def execute(self, query, parameters=None):
        async with self._execute_lock:
            return await self._execute(query, parameters)

    async def close(self):
        if self._execute_lock.locked():
            try:
                await self._protocol.abort()
            except Exception:
                pass
        async with self._execute_lock:
            self._protocol.close()


async def connect(
        host=None, port=None, database=None, user=None, password=None,
        # passfile=None,
        connect_timeout=None, application_name=None,
        fallback_application_name=None, **conn_kwargs):

    # TODO:
    #    support already connected socket?
    #    support SSL

    if port is None:
        port = 5432
    port = int(port)
    if user is None:
        user = getpass.getuser()

    if fallback_application_name is None:
        fallback_application_name = 'poqaio'

    loop = asyncio.get_running_loop()
    if not host:
        if sys.platform == 'win32':
            host = 'localhost'
        else:
            for host in ['/var/run/postgresql', '/tmp']:
                if os.path.exists(f"{host}/.s.PGSQL.{port}"):
                    break

    if isinstance(host, os.PathLike):
        host = os.fsdecode(host)

    if host.startswith("/"):
        path = f"{host}{'' if host.endswith('/') else '/'}.s.PGSQL.{port}"
    else:
        path = None
    if path is not None:
        _, protocol = await loop.create_unix_connection(
            PGProtocol, path, **conn_kwargs)
    else:
        _, protocol = await asyncio.wait_for(
            loop.create_connection(PGProtocol, host, port, **conn_kwargs),
            connect_timeout,
        )

    conn = Connection(
        protocol, host, port, database, user, application_name,
        fallback_application_name)

    await conn._startup(password)
    return conn
