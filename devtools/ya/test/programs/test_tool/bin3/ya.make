PY3_PROGRAM(test_tool3)

STYLE_PYTHON()

PY_SRCS(
    __main__.py
)

PEERDIR(
    devtools/ya/test/programs/test_tool/main_entry_point
)

WINDOWS_LONG_PATH_MANIFEST()

END()
