CODEGEN_TASK = "{ya_path} make --replace-result --keep-going {args} {targets}"
CODEGEN_CPP_TASK = (
    "{ya_path} make --force-build-depends --replace-result --keep-going --output={output_dir} {args} {targets}"
)

CODEGEN_EXTS_BY_LANG = {
    "CPP": [".h", ".hh", ".hpp", ".inc", ".c", ".cc", ".cpp", ".C", ".cxx"],
    "PY3": [".py", ".pyi", ".pysrc"],
    "GO": [".go", ".gosrc"],
}

SUPRESS_EXTS_BY_LANG = {
    "CPP": [],
    "PY3": [],
    "GO": [".cgo1.go", ".res.go", "_cgo_gotypes.go", "_cgo_import.go"],
}

PROGRAM_MODULE_TYPES = (
    "PY3_BIN__from__PY3_PROGRAM",
    "GO_PROGRAM",
    "PROGRAM",
)

TEST_MODULE_TYPES = (
    # Python
    "PY3TEST_PROGRAM__from__PY23_TEST",
    "PY3TEST_PROGRAM__from__PY3TEST",
    # Go
    "GO_TEST",
    # C++
    "UNITTEST",
    "UNITTEST_FOR",
    "YT_UNITTEST",
    "UNITTEST_WITH_CUSTOM_ENTRY_POINT",
    "GTEST",
    "G_BENCHMARK",
    "BOOSTTEST",
    "BOOSTTEST_WITH_MAIN",
)

RUN_WRAPPER_TEMPLATE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
exec '{path}' \"$@\"
"""

TEST_WRAPPER_TEMPLATE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
export Y_PYTHON_SOURCE_ROOT='{arc_root}'
export Y_PYTHON_CLEAR_ENTRY_POINT=1
export YA_TEST_CONTEXT_FILE='{test_context}'
exec '{path}' \"$@\"
"""

EXTENSIONS_BY_LANG = {
    "CPP": [],
    "PY3": [
        "ms-python.python",
        "ms-python.vscode-pylance",
    ],
    "GO": [
        "golang.go",
    ],
}
