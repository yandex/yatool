PY3_LIBRARY()

PY_SRCS(
    __init__.py
    gen_conf_docs.py
)

PEERDIR(
    contrib/python/six
    contrib/python/toml
    devtools/ya/app
    devtools/ya/build
    devtools/ya/build/build_graph_cache
    devtools/ya/build/build_facade
    devtools/ya/build/build_opts
    devtools/ya/build/genconf
    devtools/ya/build/ymake2
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/imprint
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/handlers/dump/debug
    devtools/ya/test/dartfile
    devtools/ya/test/explore
    devtools/ya/yalibrary/debug_store
    devtools/ya/yalibrary/debug_store/processor
    devtools/ya/yalibrary/debug_store/store
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
    devtools/ya/yalibrary/yandex/sandbox/misc
    library/python/fs
    library/python/func
    library/python/tmp
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/handlers/dump/arcadia_specific
        devtools/ya/yalibrary/build_graph_cache
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    bin
    conf/bin
    tests
)
