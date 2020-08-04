import asyncio
import logging
from pprint import pprint
import sys

from poqaio import connect


async def main():
    cn = await connect(database="postgres", user="bart")
    print(cn.application_name)
    print(type(cn._protocol.transaction_status))
    pprint(await cn.execute("SELECT 3 as col1, 'hi' as col2; SELECT 3 as col1, 'hi' as col2"))
    pprint(await cn.execute("SELECT a, a * 1.2 FROM generate_series(1, 10) t (a)"))
    pprint(await cn.execute("SELECT $1, $2, $3", [3, None, 'hoi']))

    pprint(await cn.execute("SELECT $1", [6.3]))
    pprint(await cn.execute("SELECT $1", [True]))

    pprint(await cn.execute(
        """select
            typname,
            typnamespace,
            typowner,
            typlen,
            typbyval,
            typcategory,
            typispreferred,
            typisdefined,
            typdelim,
            typrelid,
            typelem,
            typarray
        from pg_type
        where typtypmod = $1 and typisdefined = $2""", [-1, True]), width='300')

    pprint(await cn.execute("SET TIMEZONE TO 'Europe/Amsterdam'"))
    pprint(await cn.execute("SELECT i FROM generate_series(1, $1) AS i", [10]))
    pprint(await cn.execute("CREATE TEMPORARY TABLE yo AS SELECT $1;", [2]))
    pprint(await cn.execute("SET CLIENT_ENCODING TO 'LATIN1';"))
    print(cn.status_parameters["client_encoding"])
    pprint(await cn.execute("SET DATESTYLE TO 'german';"))
    print(cn.date_style);
    pprint(await cn.execute("SET DATESTYLE TO 'iso';"))
    print(cn.date_style);
    cn.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    asyncio.run(main(), debug=True)
