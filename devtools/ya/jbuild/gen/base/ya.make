PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild.gen.base
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/jbuild/gen/configure
    devtools/ya/jbuild/gen/consts
    devtools/ya/yalibrary/graph
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/checkout
    )
ENDIF()

END()
