CODEGEN_TASK = "{ya_path} make --replace-result --keep-going {args} {targets}"
CODEGEN_CPP_TASK = (
    "{ya_path} make --force-build-depends --replace-result --keep-going --output={output_dir} {args} {targets}"
)

CODEGEN_EXTS_BY_LANG = {
    "CPP": [".h", ".hh", ".hpp", ".inc", ".c", ".cc", ".cpp", ".C", ".cxx", ".inl"],
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
    "FUZZ",
)

RUN_WRAPPER_TEMPLATE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
exec '{path}' \"$@\"
"""

RUN_WRAPPER_TEMPLATE_SOURCE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
export Y_PYTHON_SOURCE_ROOT='{arc_root}'
exec '{path}' \"$@\"
"""

TEST_WRAPPER_TEMPLATE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
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

CLANGD_BG_INDEX_DISABLED = "Index:\n  Background: Skip\n"

CLANGD_BG_INDEX_ENABLED = "Index:\n  Background: Build\n"
