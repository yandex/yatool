LIBRARY()

SRCS(
    db.cpp
)

RESOURCE(
    insert.sql insert
    running_db.sql running_db
    running_db_inc.sql running_db_inc
    running_task.sql running
    running_task_inc.sql running_inc
)

PEERDIR(
    devtools/local_cache/common/db-utils
    devtools/local_cache/psingleton/proto
)

GENERATE_ENUM_SERIALIZATION(db-public.h)

END()
