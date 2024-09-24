## ya test

The `ya test` command is used to test program projects.

It enables you to run tests, as well as check the code, compliance with style standards, error resistance, and other aspects.

Basic features of the `ya test` command:

- [Running tests](#Запуск-тестов): Runs tests specified in the project.
- [Describing tests](#Описание-тестов): Tests are described in the `ya.make` file where you can specify dependencies, parameters, tags, and resource requirements.
- [Outputting results](#Управление-выводом-результатов): Saves test results and provides information about passed and failed tests.
- [Debugging](#Отладка): Enables debug modes for detailed troubleshooting.
- [Filtering tests](#Выборочное-тестирование): Enables you to run tests by name, tag, size, and type.

The `ya test -h` or `--help` command outputs reference information.
Use `-hh` to display additional options and `-hhh` to get more detailed information.

General command syntax
```bash translate=no
ya test [OPTION]… [TARGET]…
```
Parameters:
- `[OPTION]`: Parameters that specify how tests should be executed (selecting tests, outputting results, and so on). You can specify multiple options by separating them with a space.
- `[TARGET]`: Arguments that define specific test targets (names of test suites or individual tests). You can specify multiple targets by separating them with a space.

Basic definitions

- `Test`: Checking the code for correctness, style standards, and error resistance.
- `Chunk`: A group of tests executed as a single node in the build command graph.
- `Suite`: A collection of single-type tests with common dependencies and parameters.

Relationship between definitions
- Tests are combined into a `Chunk` based on common logic and execution sequence.
- `Chunks` are grouped into a `Suite` that provides contexts and parameters for all the tests it contains.
- `Suite` is a basic unit for running and filtering tests, as well as tracking dependencies and errors.

### Describing tests

Tests are described in the `ya.make` file.

You can use macros and options to set parameters and dependencies:

- `DEPENDS(path1 [path2...])`: Specifies the project dependencies required to run tests.
- `SIZE(SMALL | MEDIUM | LARGE)`: Defines the test size, which helps you manage execution time and resources (values: SMALL, MEDIUM, or LARGE).
- `TIMEOUT(time)`: Sets the maximum test execution time.
- `TAG(tag1 [tag2...])`: Adds tags to tests, which helps you categorize and filter them.
- `REQUIREMENTS()`: Specifies resource requirements for running tests, such as the number of processors, memory capacity, disk usage, and others.
- `ENV(key=[value])`: Sets environment variables required to run tests.
- `SKIP_TEST(Reason)`: Disables all tests in the module build. The `Reason` parameter specifies the reason.

### Recommendations for describing tests for different programming languages

#### Testing for C++
Two major test frameworks for C++ are currently supported:
1. `unittest`: Our own development.
2. `gtest`: A popular solution from Google.

In addition to these frameworks, there is a separate library, `library/cpp/testing/common`, containing useful utilities that aren't framework-specific.

The `google benchmark` library and `G_BENCHMARK` module are used for benchmarking.

In addition to benchmark metrics, you can also report numerical metrics from tests recorded using `UNITTEST()` and `GTEST()`. To add metrics, use the `testing::Test::RecordProperty` function if you work with `gtest` and the `UNIT_ADD_METRIC` macro if you work with `unittest`.

Example of metrics in `gtest`:
```cpp translate=no
  TEST(Solver, TrivialCase) {
      // ...
      RecordProperty("num_iterations", 10);
      RecordProperty("score", "0.93");
  }
```
Example of metrics in `unittest`:
```cpp translate=no
  Y_UNIT_TEST_SUITE(Solver) {
      Y_UNIT_TEST(TrivialCase) {
          // ...
          UNIT_ADD_METRIC("num_iterations", 10);
          UNIT_ADD_METRIC("score", 0.93);
      }
  }
```
To write tests, you need to create the `ya.make` file with a minimal configuration:

```ya.make translate=no
UNITTEST() | GTEST() | G_BENCHMARK()

SRCS(tests.cpp)

END()
```
#### Testing for Python
The main framework for writing tests in Python is [pytest](https://pytest.org/).

Supported Python versions:
- Python 2 (the `PY2TEST` macro is used, but it is considered deprecated).
- Python 3 (the `PY3TEST` macro is used).
- Python 2/3 compatible tests (the `PY23_TEST` macro is used).

All test files are listed in the `TEST_SRCS()` macro.

Example of a `ya.make` configuration file:

```yamake
PY3TEST() # Use pytest for Python 3 (PY2TEST will mean Python 2)

PY_SRCS( # Test dependencies, such as an abstract base test class
    base.py
)

TEST_SRCS( # List all files with tests
    test.py
)

SIZE(MEDIUM)

END()
```
To report metrics from a test, use `funcarg metrics`.
```python translate=no
def test(metrics):
    metrics.set("name1", 12)
    metrics.set("name2", 12.5)
```
For programs built using the `PY2_PROGRAM`, `PY3_PROGRAM`, `PY2TEST`, `PY3TEST`, `PY23_TEST` macros, an import test is automatically executed:
- Verifying import correctness enables you to identify conflicts and missing dependencies.
- You can disable the import test using the `NO_CHECK_IMPORTS()` macro.

Example of disabling the check:
```yamake
PY3TEST()

PY_SRCS(
    base.py
)

TEST_SRCS(
    test.py
)

SIZE(MEDIUM)

NO_CHECK_IMPORTS() # Disable the check of library importing from PY_SRCS

NO_CHECK_IMPORTS( # Disable the importing check only in specified modules
    devtools.pylibrary.*
)

END()
```
Libraries may have imports that are triggered by a certain condition:
```python translate=no
if sys.platform.startswith("win32"):
    import psutil._psmswindows as _psplatform
```
If the import test fails in this place, you can disable it as follows:
```yamake
NO_CHECK_IMPORTS( # Disable the check
    psutil._psmswindows
)
```
#### Testing for Java

This system uses `JUnit` `4.x` and `5.x` to test Java projects.

The test module for `JUnit4` is described by `JTEST()` or `JTEST_FOR(path/to/testing/module)`.
- `JTEST()`: The build system will search for tests in the module's `JAVA_SRCS()`.
- `JTEST_FOR(path/to/testing/module)`: The build system will search for tests in the module being tested.

To enable `JUnit5` instead of `JTEST()`, use `JUNIT5()`.

Test setup is done via `ya.make` files.

The bare-minimum `ya.make` file looks like this:

- **JUnit4**
  ```
  JTEST()

  JAVA_SRCS(FileTest.java)

  PEERDIR(
    # Add here dependencies on your project's source code
    contrib/java/junit/junit/4.12 # The JUnit 4 framework
    contrib/java/org/hamcrest/hamcrest-all # You can connect a set of Hamcrest matchers
  )

  END()
  ```
- **JUnit5**
  ```
  JUNIT5()
  JAVE_SRCS(FileTest.java)
  PEERDIR(
    # Add here dependencies on your project's source code
    contrib/java/org/junit/jupiter/junit-jupiter # The JUnit 5 framework
    contrib/java/org/hamcrest/hamcrest-all # A set of Hamcrest matchers
  )
  END()
  ```
#### Testing for Go

Testing in Go is based on the [standard Go toolkit](https://pkg.go.dev/testing). We recommend using the `library/go/test/yatest` library to work with test dependencies.

All test files must have the `_test.go` suffix. They are specified in the `GO_TEST_SRCS` macro.

The test module is described by the `GO_TEST()` module or `GO_TEST_FOR(path/to/testing/module)`.
- `GO_TEST()`: The build system will search for tests in the module's `GO_TEST_SRCS()`.
- `GO_TEST_FOR(path/to/testing/module)`: The build system will search for tests in `GO_TEST_SRCS` in the module being tested.

The bare-minimum `ya.make` files look like this:
- **GO_TEST()**
  ```text translate=no
  GO_TEST()

  GO_TEST_SRCS(file_test.go)

  END()
  ```
- **GO_TEST_FOR()**

  The `GO_TEST_SRCS` macro in `project/ya.make` lists the test files:
  ```text translate=no
  GO_LIBRARY() | GO_PROGRAM()

  SRCS(file.go)

  GO_TEST_SRCS(file_test.go)

  END()

  RECURSE(tests)
  ```
### Running tests

When running tests in a local development environment, consider a few key points.

- All tests are run in `debug` mode.
- The test execution timeout depends on the test size: SMALL, MEDIUM, or LARGE.
- Test results are saved to the `test-results` directory. It contains subdirectories with logs and files spawned by tests.
- If tests fail, the console displays information about the paths to the results.

In addition, for some tests to work correctly, you need to establish their interaction with the test environment.

To simply run tests, the following command-line keys are used:

- `-t`: Run only SMALL tests.
- `-tt`: Run SMALL and MEDIUM tests.
- `-ttt`, `-A`, `--run-all-tests`: Run all tests.
- `--test-param=TEST_PARAM`: Assign values from the command line to tests.`TEST_PARAM` has the `name=val` form, parameter access depends on the framework you use.
- `--retest`: Force restart tests without using cache.

**Example**
```bash translate=no
$ ya test -t devtools/examples/tutorials/python
```
It will run all tests that it finds by `RECURSE`/`RECURSE_FOR_TESTS` from `devtools/examples/tutorials/python`, including style tests and import tests for Python.

It uses the following defaults for the build:
- The platform will be determined by the *real platform* where the `ya test` is run.
- Tests will be built in `debug` mode that is used by default.
- In addition to tests, all other targets (libraries and programs) found by `RECURSE`/`RECURSE_FOR_TESTS` from `devtools/examples/tutorials/python` will be built.
This includes building of all the necessary dependencies.

By default, the build system will execute all requested tests.
Once they are completed, a summary of the failures for all failed tests will be provided, including links to detailed data.
For successful and ignored tests, only the overall status with their number will be displayed.

### Managing the output of results
The `ya test` command provides several options to manage the output of test results.

These options enable you to control the amount of information returned after running tests and help you find and fix errors faster:

- `-L`, `--list-tests`: Outputs a list of tests to be executed.
```bash translate=no
ya test -tL
```
- `--fail-fast`: Terminates test execution upon the first failure.
```bash translate=no
ya test -t --fail-fast
```
- `-P`, `--show-passed-tests`: Show passed tests.
```bash translate=no
ya test -t --show-passed-tests
```
- `--show-skipped-tests`: Show skipped tests.
- `--show-metrics`: Show test metrics.

After tests are completed, a failure summary (including links to more complete information) for all failed tests will be provided.
For passed and ignored (filtered) tests, only a short overall status with the number of passed and ignored tests will be provided.

Examples of a full report when using different options:
```bash translate=no
ya test -t --show-passed-tests --show-skipped-tests --show-metrics
```
In this example, information about all passed tests, filtered tests, and test metrics is output, which gives a complete overview of completed actions and test results.

**Example**
```bash
# List of all custom tests (without tests and import test or similar checks)
ya test -AL --regular-tests devtools/examples/tutorials/python
```
The `ya test` command supports concurrent test runs.

### Debugging
The `ya test` command provides several debugging options:
- `--test-debug`: Enable the test debugging mode (PID, additional parameters).
- `--pdb`, `--gdb`, `--dlv`: Run tests with integration into various debuggers (for Python, C++, Go).

### Selective testing

The `ya test` command supports a large number of options for filtering and selective test runs.

Test filtering:

1. `-F=TESTS_FILTERS`, `--test-filter=TESTS_FILTERS`: Run tests by name, template, or filter.
```bash translate=no
ya test -A -F "subname"
```
In filters, you can use wildcard characters, such as `*`, which matches any number of characters.
Each subsequent filter expands the set of tests to run.
```bash translate=no
$ ya test -t -F <file>.py::<ClassName>::*
```
2. `--test-tag=TEST_TAGS_FILTER`: Running tests labeled with particular tags.
```bash translate=no
ya test -A --test-tag tag1+tag2-tag3
```
This command runs all tests that have `tag1` and `tag2` and don't have `tag3`.

3. `--test-size=TEST_SIZE_FILTERS`: Running tests of a particular size (SMALL, MEDIUM, or LARGE).
```bash translate=no
ya test -A --test-size=MEDIUM
```
4. `--test-type=TEST_TYPE_FILTERS`: Running tests of a particular type (for example, `UNITTEST`, `PYTEST`).
```bash translate=no
ya test -tt --test-type unittest+gtest
```
This command runs only `unittest` and `gtest` tests.

5. `--test-filename=TEST_FILES_FILTER`: Running tests from the specified source file.
```bash translate=no
ya test -A --test-filename=test_example.py
```
6. `-X`, `--last-failed-tests`: Run only those tests that failed during the previous run.
7. `--regular-tests`: Run only custom tests.
8. `--style`: Run only style tests.

### Code and data correctness checks

The build system has a variety of tools for checking code and data to ensure compliance with quality, style, and security standards. They include linters, static analysis, and other tools developed for different programming languages.

#### Python

For Python, `flake8` automatically checks the code style for all files connected via `ya.make` 
in the sections `PY_SRCS` and `TEST_SRCS`, including plugins to test string length up to 200 characters
and ignoring errors using `# noqa` or `# noqa: E101` comments.
You can suppress the `F401` error in the `__init__.py` file using `# flake8 noqa: F401`.
For the `contrib` directory, you can disable style checking via the `NO_LINT()` macro.

For Python 3 projects, the `black` linter is connected using the `STYLE_PYTHON()` macro that generates tests to check the code style.
You can run `black` tests with the following command:
```bash translate=no
ya test -t --test-type black.
```
Import tests check the importing of Python programs built from modules and detect conflicts and missing files.
These checks are called `import_tests`. They aren't `style tests`, because such tests require the build.
You can disable style checking using `NO_CHECK_IMPORTS` and listing specific exceptions.

#### Java

For Java source files, the code style is checked with the `checkstyle` utility at the normal and strict levels. The latter is activated with the `LINT(strict)` macro, and configuration files are stored in the `resource` directory.
Optionally, you can check for duplicate classes in `classpath` using the `CHECK_JAVA_DEPS(yes)` macro.

#### Kotlin

`Kotlin` projects are automatically checked with the `ktlint` code style utility.
You can disable the check with the `NO_LINT(ktlint)` macro in the build file and add exceptions with the `KTLINT_BASELINE_FILE` macro.

#### Go

Tools for Go projects include `gofmt` (for checking the code style) and `govet` (for static analysis of suspicious constructs and anti-patterns).

#### C++

For C++ code, there's `clang-tidy` for static analysis.
You can run it with the `ya test -t -DTIDY` flag.

Sample `ya.make` file with style checks:
```yamake translate=no
PY3_LIBRARY()

PY_SRCS(
    src/init.py
    src/module.py
)

STYLE_PYTHON()

NO_CHECK_IMPORTS(
    devtools.pylibrary.
)

END()
```
### Canonicalization (and re-canonicalization)

For some types of tests, the build system supports a mechanism for comparison with benchmark (canonical) data.

1. Canonical data is data that test results are compared to.
2. Canonicalization is the process of updating benchmark data to match the current test results.
3. Re-canonicalization is performed to update benchmark data after tests or their environments are changed.
If you need to update data, use the `-Z` (`--canonize-tests`) option.

On local runs, benchmark data is saved to the `canondata` directory next to the test.

### Tests with sanitizers

The system enables you to build instrumented programs with sanitizers.
The following sanitizers are supported: `AddressSanitizer`, `MemorySanitizer`, `ThreadSanitizer`, `UndefinedBehaviorSanitizer`, and `LeakSanitizer`.

Running with a sanitizer:
```bash translate=no
ya test -t --sanitize=address
```
You can fix options for specific tests via the `ENV()` macro for the sanitizer to work properly.

### Fuzzing

Fuzzing enables you to pass incorrect, unexpected, or random data to an application as input.
Automatic fuzzing is supported via `libFuzzer`.

Example of running fuzzing:
```bash translate=no
ya test -r --sanitize=address --sanitize-coverage=trace-div,trace-gep -A --fuzzing
```
During fuzzing, various metrics such as corpus size, number of checked cases, peak memory consumption, and others are recorded.

### Running arbitrary programs (`Exec` tests)
Exec tests enable you to run arbitrary commands and verify their completion that is considered successful when the return code is `0`.
They are particularly useful for testing individual scripts, command line, or complex scenarios that are difficult to implement with standard test frameworks.

Such tests are described in the `ya.make` file as well.
```ya.make
EXECTEST()  # Declaring an Exec test

RUN(  # Command we want to run
    cat input.txt
)

DATA(  # Test data required to run the command (input.txt is located here)
    PROJECT/devtools/ya/test/tests/exectest/data
)

DEPENDS(  # Dependency on other projects (for example, cat source code)
    devtools/dummy_PROJECT/cat
)

# Current directory for the test (directory with input.txt)
TEST_CWD(devtools/ya/test/tests/exectest/data)

END()
```
### Test types

A **test type** is the execution of checks with one specific tool, such as the `pytest` framework for Python or the `go fmt` code formatting check utility for Go.
A full list of test types is given in the table below:

Type | Description
:--- | :---
`black` | Checking Python 3 code formatting using the `black` utility.
`classpath.clash` | Checking for duplicate classes in `classpath` when compiling a Java project.
`eslint` | Checking style and typical TypeScript code errors using the `ESLint` utility.
`exectest` | Executing an arbitrary command and checking its return code.
`flake8.py2` | Checking Python 2 code style using the `Flake8` utility.
`flake8.py3` | Checking Python 3 code style using the `Flake8` utility.
`fuzz` | [Fuzzing](https://en.wikipedia.org/wiki/Fuzzing) test.
`g_benchmark` | Running C++ benchmarks using the [Google Benchmark](https://github.com/google/benchmark) library.
`go_bench` | Running Go benchmarks using the `go bench` utility.
`gofmt` | Checking Go code formatting using the `go fmt` utility.
`go_test` | Running Go tests using the `go test` utility.
`govet` | Running the static Go code analyzer using the `go vet` utility.
`gtest` | Running С++ tests using the [Google Test](https://github.com/google/googletest/) framework.
`java` | Running Java tests using the [JUnit](https://junit.org/) framework.
`java.style` | Checking Java code formatting using the [checkstyle](https://checkstyle.org/) utility.
`ktlint` | Checking Kotlin code style using the [ktlint](https://ktlint.github.io) utility.
`py2test` | Python 2 tests using the [pytest](https://pytest.org/) framework.
`py3test` | Python 3 tests using the [pytest](https://pytest.org/) framework.
`pytest` | Python (any version) tests using the [pytest](https://pytest.org/) framework.
`unittest`| C++ tests using the `unittest` framework.
