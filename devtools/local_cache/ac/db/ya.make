LIBRARY()

SRCS(
    cas.cpp
    db.cpp
    gc.cpp
)

RESOURCE(
    ac.sql ac/ac
    db.sql ac/db
    db_drop.sql ac/db_drop
    conn.sql ac/setup_con
    conn_fk_on.sql ac/foreign_keys_on
    conn_fk_off.sql ac/foreign_keys_off
    cas.sql ac/cas
    gc_queries.sql ac/gc_queries
    stat.sql ac/stat
    validate.sql ac/validate
    views.sql ac/views
)

PEERDIR(
    devtools/local_cache/common/db-utils
    devtools/local_cache/common/db-running-procs
    devtools/local_cache/ac/fs
    devtools/local_cache/ac/proto
)

GENERATE_ENUM_SERIALIZATION(db-public.h)

END()

RECURSE_FOR_TESTS(ut)
