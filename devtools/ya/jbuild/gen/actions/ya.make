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
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/arc
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

END()
