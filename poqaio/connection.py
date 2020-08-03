import asyncio
import getpass
import os.path
import sys

from .protocol import PGProtocol


class Result:

    def __init__(self, results):
        self._results = results


class Connection:
    def __init__(
            self, protocol, host, port, database, user, application_name,
            fallback_application_name):
        self._protocol = protocol
        self.host = host
        self.port = port
        self.database = database
        self.user = user
        self._application_name = application_name or fallback_application_name

    async def _startup(self):
        await self._protocol.startup(
            self.user, self.database, self._application_name)

    @property
    def application_name(self):
        return self._protocol.parameters.get('application_name')

    @property
    def date_style(self):
        return self._protocol.date_style

    @property
    def client_encoding(self):
        return self._protocol.client_encoding

    @property
    def integer_datetimes(self):
        return self._protocol.integer_datetimes

    @property
    def is_superuser(self):
        return self._protocol.parameters.get('is_superuser') == 'on'

    @property
    def time_zone(self):
        return self._protocol.parameters.get("TimeZone")

    @property
    def server_version(self):
        return self._protocol.parameters.get("server_version")

    async def execute(self, query, parameters=None):
        return await self._protocol.execute(query, parameters)

    def close(self):
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
        path = host
    elif host.startswith("/"):
        path = f"{host}{'' if host.endswith('/') else '/'}.s.PGSQL.{port}"
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

    await conn._startup()
    return conn
