PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.large_files
    __init__.py
    resource_dict_sb.py
    large_file.py
    exc.py
)

PEERDIR(
    devtools/ya/yalibrary/yandex/sandbox/misc
)

END()

RECURSE_FOR_TESTS(
    tests
)
