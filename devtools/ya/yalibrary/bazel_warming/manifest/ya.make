PY3_LIBRARY()

PY_NAMESPACE(yalibrary.bazel_warming.manifest)

PY_SRCS(
    __init__.py
    model.py
    stream.py
    writer.py
)

END()

RECURSE_FOR_TESTS(tests)
