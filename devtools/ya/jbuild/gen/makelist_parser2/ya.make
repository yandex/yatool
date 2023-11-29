PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild.gen.makelist_parser2
    __init__.py
)

PEERDIR(
    devtools/ya/jbuild/gen/consts
    devtools/ya/jbuild/gen/java_target2
    devtools/ya/yalibrary/graph
)

END()
