PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild.gen.consts
    __init__.py
)

PEERDIR(
    devtools/ya/test/const
)

END()
