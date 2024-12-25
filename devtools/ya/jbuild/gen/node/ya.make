PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/yalibrary/graph
    devtools/ya/jbuild/gen/base
    devtools/ya/jbuild/gen/consts
    devtools/ya/jbuild/commands
)

END()
