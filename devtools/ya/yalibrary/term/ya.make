PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.term
    console.py
    size.py
)

PEERDIR(
    devtools/ya/exts
)

END()
