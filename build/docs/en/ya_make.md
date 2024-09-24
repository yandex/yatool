## ya make: basic build tool

A command that automates project builds, including compiling code, running tests, and creating executables.

A build target in the `ya make` system is a program or library that needs to be built.
A build target means:
1. An artifact — the build result, such as a program or library file.
2. A build description — the `ya.make` file containing the build instructions.

Not every build target creates an artifact.
Some targets set dependencies or implement build flags.

The build target is specified by the directory containing the `ya.make` file.
The `ya.make` file [describes](#Настройки-сборки) what will be built, how it will be built, and what targets need to be built.
[The build results](#Результаты-сборки) are placed in the target directories as symbolic links by default.

The `ya make` system rule:

**One build target per directory**.

Each directory contains only one `ya.make` file that describes the build of a single module.

General command form

`ya make [OPTIONS]… [TARGET]…`

- `[OPTIONS]…` : Options to modify build or test behavior.
- `[TARGET]…` : Build targets.
If there is no explicitly specified target, `ya make` builds the target from the current directory.

`ya make` uses the options to flexibly configure the build parameters:

- [Build settings](#Настройки-сборки): Selecting additional [build results](#Результаты-сборки).
- [Build type and form](#Тип-и-вид-сборки): debug/release, LTO, sanitizers, and others.
- [Platform parameters](#Платформенные-параметры): Target hardware platform and OS, build platform settings.
- [Build variables](#Сборочные-переменные): For example, `-DCFLAGS=-Wall`, `-DDEBUGINFO_LINES_ONLY`.
- [Work with tests](#Работа-с-тестами): Running tests and working with them.

Options are also divided into types:
1. Basic
2. Extended
3. Expert

To get help with the `ya make` command, use:
- `ya make -h`, `--help`: Prints a reference.
- `ya make -hh`: To get a full list of options.
- `ya make -hhh`: To display a list of options that are used in specific scenarios.

### Build settings

The build system is used to transform source files in various languages into libraries, programs, and tests ([build results](#Результаты-сборки)) according to the strict rules, as well as to [run tests](#Работа-с-тестами) on demand.

The rebuild should ideally be minimal and affect only the modified components.

To achieve this goal, the build system must have:
- [Described rules for transforming source files into results](#Описание-макросов-для-модуля).
- [Defined dependencies to minimize rebuilds](#Строгий-контроль-зависимостей).
- [Described build properties to ensure consistency between targets and their dependencies in the build](#Описание-параметров-сборки-модулей-и-программ).
- Specified additional properties, such as the code owner and license.

#### How the ya.make build is described

The description of the `ya.make` build is modular.

Each `ya.make` file describes the build of no more than one module.

A module can be a library, a program, a test, or a group of targets (`PACKAGE` and `UNION` modules).

A logic module may sometimes be implemented in multiple variants, which is called a multi-module. Examples of multi-modules include `PY23_LIBRARY` and `PROTO_LIBRARY`

The description structure is declarative. This means that only the necessary build information is specified, not the sequence of operations.

**Key characteristics:**
- The build process is detailed through the use of variables and conditional operators.
- Automatic detection of some dependencies based on file extensions.
- Each `ya.make` file is unique for its directory and describes the build of a single module.

#### Description of macros for a module

The first step in build configuration is to determine the key module parameters using special commands called macros, which look like calls of functions with parameters.

The `ya make` build system provides a large number of macros for different languages and applications.

Macros in a module can be roughly divided into three categories:

1. Control macros:
Control or conditional macros, such as `INCLUDE`, `EXCLUDE_TAGS`, `ONLY_TAGS`, as well as macros like `IF()`, `ELSEIF()`, `ELSE()`, `ENDIF()` affect the way the `ya.make` file is interpreted.
They enable you to set up different builds based on values of variables that can be set locally (using the `SET()` macro) or globally to define a build configuration of a given module.
For example, global variables may include `OS_WINDOWS` when building for Windows or `PYTHON3` in the appropriate multi-module cases.

2. Macros that set properties
These macros can directly affect built-in properties.
For example, `PEERDIR` sets dependencies for a module, and `SRCDIR` or `ADDINCL` specify the paths to search for names.
The `PY_NAMESPACE` macro defines namespaces for importing files described in the module.

They can also form values of variables used in other macros or module commands.
For example, `CFLAGS` sets flags for C/C++ file compilation commands.
The `SET(VAR VALUE)` macro sets the value of a variable: if the variable is present in the global configuration, its value changes for this module; if not, the variable will appear and be available in the module context for subsequent macros.

3. Macros that describe commands
These macros set commands to be executed during the build process.
They form commands and integrate them into the module build graph by linking their outputs to the inputs of other commands (consumers) and, possibly, their inputs to the outputs of other commands (sources).
For example, the `COMPILE_LUA` macro creates a command that complies `Lua` code into a representation suitable for `LuaJit`.
The output of this command will be associated with other commands of the module (for example. packaging into a library), and the library will further be linked to the final program by dependencies.

The initial `ya.make` for the project list contains a list of directories in the `RECURSE` macro:

```bash translate=no
RECURSE(
commie
core
lib
solver
ut
)
```
Most macros don't require build commands to be explicitly specified.
Commands are hidden macro properties that change depending on configuration parameters and are described a level below (in [core.conf](./coreconf.md)).
Only the arguments for these commands are specified in macros.

Macros, such as `SRCS`, can themselves define build commands by file extension.

#### Description of module and program build parameters

In a typical `ya.make` file, you can specify:
- Module owner (`SUBSCRIBER`): Usually a username or group responsible for the module.
- Build type (`PROGRAM`, `LIBRARY`, `PY3_LIBRARY`, and others): Indicates whether the module is an executable file, library, or something else.
- Sources (`SRCS`): A list of source code files to be compiled.
- Dependencies (`PEERDIR`, `DEPENDS`): Other modules or programs that the current module depends on.
- Compilation parameters and flags (`CFLAGS`, `LDFLAGS`, and others) for setting up the build process.
- `END()`: Signals the end of the settings block for the module.

In addition to macros, there are also comments that start with the `#` symbol and continue up until the end of the line.

**Example of a basic `ya.make` file:**
```bash
SUBSCRIBER(username) # Macro that specifies the module owner
PROGRAM() # Module type macro: program (C++)
PEERDIR( # Module dependency macro
util/draft
)
SRCS( # Module source file macro
main.cpp
)
END() # Closing module macro
RECURSE_FOR_TESTS(test) # Communication macro that compiles and runs tests from the test subdirectory
```
#### Strict dependency control
To manage dependencies between different project components, `ya.make` files use the `PEERDIR` and `DEPENDS` macros.

- The `PEERDIR` macro points to other project modules required to build the current module, ensuring that local dependencies are properly resolved.
- The `DEPENDS` macro specifies dependencies on other projects that need to be built and whose results should be available for testing.

The parameters list relative paths from the repository root.

Dependencies are controlled at the configuration stage: this stage analyzes command dependencies and assigns unique IDs (UIDs) to all required files.

#### Description of ya.make macros for tests

There are special `ya.make` macros that help organize module tests:

- `RECURSE_FOR_TESTS()`: Indicates that the tests in the specified directory must also be built.
- `DEPENDS()`: Test's dependencies on other modules or libraries that must be built to run it.
- `FORK_TESTS()`, `FORK_SUBTESTS()`: Running tests concurrently.
- `USE_RECIPE()`: Connects the test environment configuration recipe to the test. If the test is described using the `FORK_TEST()` / `FORK_SUBTESTS()` macros, the recipe will create a separate environment for each run.
- `SKIP_TEST(Reason)`: Disables all tests in the module build. The *Reason* parameter specifies the reason.

#### Description of test parameters

Test parameters determine how the tests will be run:
- Command-line parameters that can be passed to the test process (`--test-param`).
- Set the timeout period for the tests (`TIMEOUT()`).
- Set test-specific environment variables (`ENV()`).
- Configurations of special test execution modes, for example, for different operating systems or architectures (`REQUIREMENTS()`).

### Build results

The default build targets are modules directly specified in `ya.make` and recursively specified in all `RECURSE` macros available from `ya.make`.

You can change this behavior with the following options:
- `--target/-C`: Build targets and environment parameters.
- `-t`, `-A`: Running tests.
- `--force-build-depends`: Enable building of all dependencies for the tests.
- `-DTRAVERSE_RECURSE_FOR_TESTS`: Treat `RECURSE_FOR_TESTS` as ordinary `RECURSE`.
- `--ignore-recurses`: Ignore `RECURSE`, run only the specified targets.
- `-o`, `--output <path>`: Directory where the results will be stored.
- `--no-src-links`: Disabling symbolic links.
- `--keep-temps`: Save files in build directories.

To expand the set of build results, you can use the `--add-result=.<suff>` option.
In the `ya.conf` configuration file, you can set `add_result = [".suff"]`.
```bash translate=no
ya make -o=<output> <target> --add-result=.o --add-result=.obj
```
Stores object files in the `<output>` directory and (on Linux and MacOS) creates links to files from the results cache in the source directory.

Additional options:
- `--add-protobuf-result`: Add all generation results for `Protobuf`.
- `--add-flatbuf-result`: Add generation results for `Flatbuffers`.
- `--add-modules-to-results`: Transform all modules (including those dependent by `PEERDIR`) into build results.
- `--add-host-result=.<suff>`: Add the selected results from the build platform.
- `--all-outputs-to-result`: Skip filtering command results.
- `--add-result`: Usually filters command results by extension. You can use this option to request all results for commands matching `--add-result`.

Examples:
- `ya make --add-result=.pb.h`: The result is only `.pb.h` files.
- `ya make --add-result=.pb.h --all-outputs-to-result`: The result is both `.pb.h` and `.pb.cc` files.

You may sometimes want to build only generated files, but not libraries/programs.
To do this, use the `--replace-result` option.
It leaves in the build results only what was selected in `--add-result`; everything else will be deleted and won't be built at all.

You can limit the set of displayed results using:
  - `--no-output-for=.<suff>`: Limit the set of displayed results.
  - `suppress_outputs = [".suff"]`: Configuration file parameter.

These options don't exclude files from the build, merely informing the build system that the results mustn't be placed in the working copy and output directory.
The `--no-output-for` option doesn't interact with `--replace-result`.
Files excluded from the results will still be built and remain in the cache.

### Build type and form
`ya make` currently supports only local builds.
We plan to add distributed builds, for which the `--dist` option is reserved.

By default, the code is built in the `Debug` configuration, without optimizations and with debugging information and `assert`.

The following options are available for build types:
- `-d`, `--debug`: Activates building in debug mode. Adds debugging information to executable files and reduces the level of optimization.
- `-r`, `--release`: Initiates building in release mode. Optimizes the executable code to ensure maximum performance, including by removing debugging information.
- `-v`, `--verbose`: Enables the detailed output mode. Provides additional information about the build process.
- `--rebuild`: Forces to ignore the results of previous builds and restart the process.
- `--build=BUILD_TYPE`: Used to specify the build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug).

You can see a complete up-to-date list of build options by running:
```bash translate=no
ya make --help | grep 'Build type`
```
or

```bash translate=no
ya make --build help
```
In addition to the `--build` parameter, there are also other flags that affect the build type:
- `--lto/--thinlto`: Build with global optimizations (link-time optimizations, `LTO`) for C++.
- `--sanitize <sanitizer>`: Build with a sanitizer for C++.
- `--race`: Build with a `race detector` for Go.
- `--hardening`: Build with stricter code checks.
- `--musl`: C/С++ build with `MUSL` instead of `glibc`.
- `--cuda=<optional|required|disabled>`: Build with `CUDA`.

The local build uses the following keys to configure directories:

- `-S=CUSTOM_SOURCE_ROOT`, `--source-root=CUSTOM_SOURCE_ROOT`: Path to the project from which the repository root will be searched. By default, this is the current working directory where the `ya make` command runs.
- `-B=CUSTOM_BUILD_DIRECTORY`, `--build-dir=CUSTOM_BUILD_DIRECTORY`: Root for build directories.

Build commands are executed here. By default, directories are located at `~/.ya/build/`.

Directories for the build results are configured with the following parameters:
- `-o=OUTPUT_ROOT`, `--output=OUTPUT_ROOT`: Path for build results (on Linux and macOS, hard links are used).
The results are placed at the specified path.

You can specify where to write build and event logs:
- `--log-file=LOG_FILE`: Adds a detailed log to the specified file.
By default, a different log is created for each build in `~/.ya/logs/<date>/<time>.log`.
- `--evlog-file=EVLOG_FILE`: Writes the event log to the specified file.
By default, a different log is created for each build in `~/.ya/evlogs/<date>/<time>.evlog.zst`.

Local builds support the **content UID** mode.
In this mode, the build doesn't restart the node build if the results of running its dependencies haven't changed.

You can disable the mode using the `--no-content-uids` option or the `content_uids = false` setting in `ya.conf`.

To see the use of content UIDs, use the `--stat` option:
```bash translate=no
[=] ya make path/test/content_uids_cache/simple/original --stat
Cache hit ratio is 71.43% (5 of 7). Local: 2 (28.57%), dist: 0 (0.00%), >>> by dynamic uids: 3 (42.86%) <<<
[CC-DYN_UID_CACHE] - 6 ms. <<<
[LD-DYN_UID_CACHE] - 2 ms. <<<
```

#### Build status display

To display the build status, use the `ninja` style.
The progress is displayed in a single line and shows the status of one command with information about other commands at the end of the line (`+X more`).

You can change the status display to line-by-line using the `-T` option.
This mode is enabled when redirecting the output of the `ya make` command to a file.

The `ya make` command output can be extended with the following options:
- `-v`, `--verbose`: Output the text of completed build commands.
- `--do-not-output-stderrs`: Skip outputting stderr commands.
- `--show-timings`: Output command execution time to the status, works only with the `-T` option.
- `--show-extra-progress`: Show extended progress (number of completed commands) in the status. Works only with `-T`.
- `--cache-stat`: Show local cache fill statistics before the build.
- `--stat`: Show build statistics at the end of the build. Statistics include cache usage, critical path, longest build steps, and other information.

### Platform parameters

A platform includes a triplet of compiler, operating system, and processor architecture.
For example, `<clang11, Windows, x86_64>` or `<clang10, Android, armv8a>`.

By default, the target and build platform is the one on which the build is running.
This means that the operating system will match the one you are using (for example, `Linux`, `Darwin (macOS)`, or `Windows`), the architecture willl be `x86_64` or `arm64` (for Mac with the M1 processor), and the compiler will be selected by default (`default`) for that operating system.

To change the target platform, use the `--target-platform` flag.
Specify the parameters in `<compiler-OS-architecture>` format, connecting the values with hyphens.
You can also specify only a part of the triplet, and the remaining components will be automatically filled with default values.
To select the default compiler, use the `DEFAULT` value.

Examples:
- `--target-platform clang-win-x86_64`: Build for `Windows` on the `x86_64` processor with the use of the `Clang` compiler.
- `--target-platform default-android-armv8a`: Build for `Android` on the `ARM` processor version `armv8a` with the default compiler.
- `--target-platform windows`: Build for `Windows` on the `x86-64` architecture with the default compiler.

### Build variables

You can use the `ya make` command to specify additional flags (variables) via the `-DVAR[=VALUE]` option.
If you don't specify a value for a variable, it defaults to `yes`.

You can define:
- Predefined variables for passing the build flags
`ya make -DLDFLAGS="-fblabla=foo -fbar"`, `ya make -DCFLAGS="-DNN_DOUBLE_PRECISION -Wno-strict-aliasing"`.
- Predefined variables that manage the configuration, such as `ya make -DCATBOOST_OPENSOURCE` (build as for `open source`) or `ya make -DCUDA_VERSION=10.1` (build with `CUDA`).
- Variables to be used in `ya.make` `ya make -DMAKE_VAR1 -DMAKE_VAR2=42` where, for example, `IF (MAKE_VAR1)` is written in `ya.make`.

### Work with tests

By default, test results aren't cached locally.

At the moment, local test running considers timeouts based on the size (1 minute for SMALL, 10 minutes for MEDIUM, 1 hour for LARGE).

By default, local builds are done in the `debug` configuration. For automatic testing, you can select the `relwithdebinfo` mode, a release build with debugging information.

Test results are locally placed to the `test-results` directory, which appears as a symbolic link in the working copy in the test directory (except on Windows or when using the `--no-src-links` flag).

Inside is the `<suittype>/testing_out_stuff` directory with logs and other files spawned by tests.

If tests fail, the console may display information about the paths in the actual directories within the build directory where the tests were run.
To save this data after the tests are completed, build directories aren't cleared immediately, but only at the next build.
As a result, data in these directories remains available until the next build or test run.
If you need to save this data, copy it in advance.

#### Running tests (-t, -tt, -ttt, -A)
- `-t`: This basic option runs all tests marked as SMALL. These are quick tests that usually require few resources and little time to execute.
- `-tt`: Extension of the basic `-t` option that includes running both SMALL- and MEDIUM-sized tests. Medium tests tend to take more time and resources.
- `-ttt`: This option runs all tests, including LARGE ones. Large tests often include integration and load tests that require a significant amount of time to run and may include external dependencies.
- `-A, --run-all-tests`: Similar to `-ttt`, runs all tests regardless of their size.

#### Managing the output of results
- `-L, --list-tests`: Outputs a list of tests to be executed without actually running them. This helps developers quickly check which tests are included in the test plan.
- `–-fail-fast`: Terminates the test run immediately after the first encountered failure. This option is used to save time and resources, especially searching for a specific error.

#### Selective testing
- `–-test-filter=TESTS_FILTERS`: This option enables you to restrict testing only to certain tests that match the specified filters. You can filter tests by name, part, or other identifying patterns.
- `-–test-tag=TEST_TAGS_FILTER`: This option enables you to run only those tests that are labeled with certain tags. Tags are custom labels used to group tests by specific attributes or features.
- `–-test-size=TEST_SIZE_FILTERS`: A filter for running tests of a specific size (SMALL, MEDIUM, LARGE) that you can use to more precisely adjust the size of tests to be run depending on your current task at hand.
- `–-test-type=TEST_TYPE_FILTERS`: A filter for running only those tests that belong to a certain type or types (for example, `UNITTEST`, `PYTEST`). It is convenient if you need to run specific types of tests.

### Error messages

`ya make` provides detailed error and warning messages at the configuration stage (analyzing `ya.make` files, building a dependency graph). They can include information about missing files, circular dependencies, configuration issues, and more.

- Configuration errors (`BadAuto`, `BadDir`, and others) indicate that there are problems with the build settings, such as incorrectly specified directories or files. Make sure that all paths are specified correctly, and your configurations match the requirements of your project.
- Dependency errors (`DEPENDENCY_MANAGEMENT`, `DupSrc`) indicate that there are problems with dependency management, including duplicate sources or incorrect dependent module management. To address them, check your dependency declarations in `ya.make` files and eliminate duplicates or invalid links.
- Syntax errors (`Syntax`) often occur due to incorrect use of macros or typos in `ya.make` files. Thoroughly check the syntax of your configuration files for compliance with the `ya make` documentation.
- User errors (`UserErr`, `UserWarn`) are generated directly from `ya.make` files described by users in `MESSAGE(FATAL_ERROR msg)` macros and usually check some logic in the build description.

### Popular recipes

- `ya make -r`: Build code with optimizations and debugging information.
- `ya make -v`: Build in verbose mode. **Note:** `ya -v make` and `ya make -v` produce different effect. The former adds the `ya` utility output, and the latter adds the builds.
- `ya make -T`: Switch to line-by-line display of information on the screen.
- `ya make --add-result=".h" --add-result=".cpp"`: Build and add cpp- and h- files generated during the build process to the results.
- `ya make --add-result=".pb.h" --add-result=".pb.cc" --replace-result`: Run only protobuf code generation.
- `ya make -t`: Run only quick tests.
- `ya make -tL`: Output a list of quick tests.
- `ya make -A`: Run all tests.
