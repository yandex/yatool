PY23_LIBRARY()

PY_SRCS(
    compile.py
    fetch_test_data.py
    funcs.py
    idea.py
    missing_dirs.py
    parse.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
    devtools/ya/build/build_plan
    devtools/ya/build/targets
    devtools/ya/jbuild/commands
    devtools/ya/jbuild/gen/base
    devtools/ya/jbuild/gen/makelist_parser2
    devtools/ya/jbuild/gen/node
    devtools/ya/jbuild/idea_templates
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/rglob
    devtools/ya/yalibrary/vcs
)

IF (PYTHON3)
    PEERDIR(
        devtools/ya/yalibrary/tools
    )
ENDIF()

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

END()
