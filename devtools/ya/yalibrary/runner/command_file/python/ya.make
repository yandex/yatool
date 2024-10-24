PY3_LIBRARY()

PEERDIR(
    devtools/ya/yalibrary/runner/command_file
)

PY_SRCS(
    NAMESPACE yalibrary.runner.command_file.python
    command_file.pyx
)

END()
