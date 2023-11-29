LIBRARY()

SRCS(
    db.cpp
)

RESOURCE(
    db.sql tc/db
    conn.sql tc/setup_con
    db_drop.sql tc/db_drop
    notify.sql tc/notify
    lock_resource.sql tc/lock_resource
    fs_filler.sql tc/fs_filler_seq
    fs_mod.sql tc/fs_mod_seq
    gc_queries.sql tc/gc_queries_seq
    stat.sql tc/stat_seq
    update_tools.sql tc/update_tools
    validate.sql tc/validate
    version.sql tc/version
    views.sql tc/views
)

PEERDIR(
    devtools/local_cache/common/db-running-procs
    devtools/local_cache/common/db-utils
    devtools/local_cache/toolscache/fs
    devtools/local_cache/toolscache/proto
    library/cpp/threading/future
)

GENERATE_ENUM_SERIALIZATION(db-public.h)

END()

RECURSE(
    ut
)
