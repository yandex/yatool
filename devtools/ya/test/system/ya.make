PY23_LIBRARY()

PEERDIR(
    devtools/ya/test/system/env
    devtools/ya/test/system/process
)

END()

RECURSE(
    env
    process
)
