PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild.gen.java_target2
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/jbuild/gen/consts
    devtools/ya/yalibrary/graph
)

END()
