LIBRARY()

LICENSE(BSD-3-Clause)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(0.8)

ORIGINAL_SOURCE(https://github.com/gperftools/gperftools/archive/refs/tags/perftools-0.8.tar.gz)

NO_UTIL()

NO_COMPILER_WARNINGS()

IF (OS_DARWIN)
    PEERDIR(
        contrib/libs/gperftools
    )
ELSE()
    SRCS(
        galloc.cpp
        hack.cpp
    )
ENDIF()

END()
