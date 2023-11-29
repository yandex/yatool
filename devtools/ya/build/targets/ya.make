PY23_LIBRARY()

PY_SRCS(
    NAMESPACE build.targets

    __init__.py
)

PEERDIR(
    devtools/ya/build/makelist
    devtools/ya/yalibrary/find_root
)

END()
