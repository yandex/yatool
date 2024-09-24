## ya dump

The `ya dump` command is used to extract and provide detailed information about various build system and repository aspects.

This information may include data about the build graph, [dependencies between modules](#Анализ-зависимостей), configuration parameters, and more.

### Syntax

The general command format is as follows:
```bash translate=no
ya dump <subcommand> [OPTION] [TARGET]
```
Where:
- `ya dump` is the main command call that signals that you want to extract project data.
- `<subcommand>` indicates a specific action or report you want to receive.
For example, `modules`, `dep-graph`, `conf-docs`, and others. Each subcommand has its own purpose and a set of additional parameters and options.
- `[OPTION]` (optional): Additional flags or keys that modify the behavior of the selected subcommand. Options enable you to configure the command output, specify which data to extract, or change the output format.
- `[TARGET]` (optional): Additional parameters required to execute some subcommands. They may include a directory, module names, or other specific data required to execute the command.

### General options
You can see the up-to-date list of all available commands by running:
```bash translate=no
ya dump --help
```

To see the list of all available options for a particular `ya dump` utility subcommand, you can use the `--help` parameter immediately after specifying the desired subcommand.

This will output a detailed reference for the options and their use for the selected subcommand.

For example, to out all available options for the `modules` subcommand, the command will be as follows:
```bash translate=no
ya dump modules --help
```
`ya dump` command options are very similar to `ya make` options, for example:
* `-xx`: Fully rebuild the graph (without using caches).
* `-d`, `-r`: A graph for debug or release build mode.
* `-k`: Ignore configuration errors.

Options for specifying build and platform parameters:

- `-D=FLAGS`: Set build variables.
- `--host-platform-flag=HOST_PLATFORM_FLAGS`: A host platform flag.
- `--target-platform=TARGET_PLATFORMS`: Specify the target platform.
- `--target-platform-flag=TARGET_PLATFORM_FLAG`: Set flags for the target platform.
- `--build=BUILD_TYPE`: Build type (the default is `release`).
- `--sanitize=SANITIZE`: Sanitizer type.
- `--race`: Building Go projects with a race detector.

Additional options for configuring the build process:

- `--host-build-type=HOST_BUILD_TYPE`: Specify the host platform build type.
- `--host-platform=HOST_PLATFORM`: Specify the host platform.
- `--c-compiler=C_COMPILER`: Specify the C compiler.
- `--cxx-compiler=CXX_COMPILER`: Specify the C++ compiler.
- `--lto`: Enable `Link Time Optimization` (`LTO`).
- `--thinlto`: Enable `ThinLTO`.
- `--afl`: Use `American Fuzzy Lop` for fuzzing.
- `--hardening`: Enable additional, stricter code checks.
- `--cuda=CUDA_PLATFORM`: Enable the `NVIDIA CUDA` integration.
- `--tools-cache-size=TOOLS_CACHE_SIZE`: Set the tool cache size limits.

Filtering options:

These options are used to filter and refine the data to be extracted or presented as a subcommand output. They affect the generated build graph.
- `--force-build-depends` (either `-t` or `-A`): Shows dependencies.
- `--ignore-recurses`: Ignoring `RECURSE` tags in `ya.make` files.
- `--no-tools`: Excluding dependencies related to build tools from the output.
- `--from=<module>`, `--to=<module>`: Specifying the starting and end point to display the dependency path. You can specify the `--from` and `--to` options multiple times.

Formatting options:

Formatting options provide additional capability for configuring the subcommand output format.

- `--json`: Outputs information in JSON format. It can be used when the data is to be further processed programmatically.
- `--flat-json`, `--flat-json-files` (for the dep-graph subcommand): Formats the dependency graph output into a flat JSON list, focusing on file dependencies or arcs of the graph. A dependency graph usually contains file nodes and command nodes. The `--flat-json-files` option enables you to output only file nodes and dependencies between them.
- `-q`, `--quiet`: Output nothing.

### Analyzing dependencies

Dependency management is one of the primary pillars of project development. If you properly define and analyze dependencies, you can ensure that your project is built correctly, avoiding problems related to library incompatibilities, redundant dependencies, and other common pitfalls.

The main goals and tasks of using `ya dump` when analyzing dependencies:

1. Analyzing dependencies for a module and between modules:
`ya dump` enables you to identify which modules depend on each other and how they affect the build process.
2. Dependency filtering:
Use various options (`--ignore-recurses`, `--no-tools`, and others) to get a more detailed dependency graph. You can exclude particular types of dependencies (for example, dependent modules, test dependencies, and build tools) for a more accurate analysis.
3. Defining dependency paths from one module to another, including all possible paths.
4. Various languages and platforms are supported:
`ya dump` works with dependencies for different programming languages (C++, Java, Python, and Go among others) and takes into account their specifics, such as logical and build dependencies.

### Subcommands `<subcommand>`

The `ya dump` subcommands output information about the build graph and provide you with information about the build system.

The **first group** of subcommands may be helpful in analyzing dependencies between modules and troubleshooting. It includes:

- [`ya dump modules`](#ya-dump-modules): A list of dependent modules.
- [`ya dump relation`](#ya-dump-relation): Dependency between two modules.
- [`ya dump all-relations`](#ya-dump-all-relations): All dependencies between two modules.
- [`ya dump dot-graph`](#ya-dump-dot-graph): A graph of all inter-module dependencies of a given project.
- [`ya dump dep-graph`](#dep-graph-и-json-dep-graph), `ya dump json-dep-graph`: A graph of build system dependencies.
- [`ya dump build-plan`](#ya-dump-build-plan): A build command graph.
- [`ya dump loops`, `ya dump peerdir-loops`](#ya-dump-loops-и-peerdir-loops): Information about loops in the dependency graph.
- [`ya dump compile-commands`, `ya dump compilation-database`](#ya-dump-compile-commands-и-compilation-database): Information about build commands (`compilation database`).

By default, the output of these subcommands is based on the graph of an ordinary build without tests.

To search for information taking into account the test builds, add the `-t` option. For example, `ya dump modules -t`.

The **second group** provides various information from the build system. It includes:

- [`ya dump groups`](#ya-dump-groups): Groups of project owners.
- [`ya dump json-test-list`](#ya-dump-json-test-list): Information about tests.
- [`ya dump recipes`](#ya-dump-recipes): Information about recipes.
- [`ya dump conf-docs`](#ya-dump-conf-docs): Macro and module documentation.
- [`ya dump debug`](#ya-dump-debug): Debug `bundle` build.

#### ya dump modules

It displays a list of all dependencies for a particular *target* (current directory unless explicitly specified otherwise).

Command: `ya dump modules [option]... [target]...`

**Example:**
```bash translate=no
spreis@starship:~/yatool$ ./ya dump modules devtools/ymake | grep sky
module: Library devtools/ya/yalibrary/yandex/skynet $B/devtools/ya/yalibrary/yandex/skynet/libpyyalibrary-yandex-skynet.a
module: Library infra/skyboned/api $B/infra/skyboned/api/libpyinfra-skyboned-api.a
module: Library skynet/kernel $B/skynet/kernel/libpyskynet-kernel.a
module: Library skynet/api/copier $B/skynet/api/copier/libpyskynet-api-copier.a
module: Library skynet/api $B/skynet/api/libpyskynet-api.a
module: Library skynet/api/heartbeat $B/skynet/api/heartbeat/libpyskynet-api-heartbeat.a
module: Library skynet/library $B/skynet/library/libpyskynet-library.a
module: Library skynet/api/logger $B/skynet/api/logger/libpyskynet-api-logger.a
module: Library skynet/api/skycore $B/skynet/api/skycore/libpyskynet-api-skycore.a
module: Library skynet/api/srvmngr $B/skynet/api/srvmngr/libpyskynet-api-srvmngr.a
module: Library skynet/library/sky/hostresolver $B/skynet/library/sky/hostresolver/libpylibrary-sky-hostresolver.a
module: Library skynet/api/conductor $B/skynet/api/conductor/libpyskynet-api-conductor.a
module: Library skynet/api/gencfg $B/skynet/api/gencfg/libpyskynet-api-gencfg.a
module: Library skynet/api/hq $B/skynet/api/hq/libpyskynet-api-hq.a
module: Library skynet/api/netmon $B/skynet/api/netmon/libpyskynet-api-netmon.a
module: Library skynet/api/qloud_dns $B/skynet/api/qloud_dns/libpyskynet-api-qloud_dns.a
module: Library skynet/api/samogon $B/skynet/api/samogon/libpyskynet-api-samogon.a
module: Library skynet/api/walle $B/skynet/api/walle/libpyskynet-api-walle.a
module: Library skynet/api/yp $B/skynet/api/yp/libpyskynet-api-yp.a
module: Library skynet/library/auth $B/skynet/library/auth/libpyskynet-library-auth.a
module: Library skynet/api/config $B/skynet/api/config/libpyskynet-api-config.a
```
### ya dump relation

It finds and shows a dependency between the module from the **current directory** and the *target* module.

Command: `ya dump relation [option]... [target]...`

Options:

- `-C`: A module the graph will be **built** from. By default, the module from the current directory is used.
- `--from`: A starting target the dependency chain will be **displayed** from. By default, it is the project the graph was built from.
- `--to`: Name of a module or directory the dependency chain will be displayed **up to**.
- `--recursive`: Enables displaying dependencies up to an arbitrary directory/module/file from the *target* directory.
- `-t`, `--force-build-depends`: When calculating the dependency chain, takes into account the dependencies of the tests (`DEPENDS` and `RECURSE_FOR_TESTS`).
- `--ignore-recurses`: When calculating dependencies, excludes the dependencies by `RECURSE`.
- `--no-tools`: When calculating dependencies, excludes the dependencies on build tools.
- `--no-addincls`: When calculating dependencies, excludes the dependencies by `ADDINCL`.

Note that:
- The dependency graph is built for the project in the current directory. You can change this using the `-С` option; the `--from` option only selects the starting point in the graph.
- `target`: Module name according to `ya dump modules` or project directory of that module.
- The `--recursive` flag can be used to display the path to one arbitrary module/directory/file located in the *target* directory.

Note that there may be multiple paths between modules in the dependency graph. The utility finds and arbitrarily displays one of them.

**Examples:**

Find a path to the `contrib/libs/libiconv` directory:
```bash translate=no
~/yatool/devtools/ymake$ ya dump relation contrib/libs/libiconv
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Library (Include): $B/devtools/ymake/include_parsers/libdevtools-ymake-include_parsers.a ->
Library (Include): $B/library/cpp/xml/document/libcpp-xml-document.a ->
Library (Include): $B/library/cpp/xml/init/libcpp-xml-init.a ->
Library (Include): $B/contrib/libs/libxml/libcontrib-libs-libxml.a ->
Directory (Include): $S/contrib/libs/libiconv
```

Find a path to an arbitrary file node from `contrib/libs`:
```bash translate=no
~/yatool/devtools/ymake$ ya dump relation contrib/libs --recursive
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Directory (Include): $S/contrib/libs/linux-headers
```

### ya dump all-relations

Outputs in `dot` or `json` format all dependencies in the internal graph between *source* (by default, all targets from the current directory) and *target*.

Command: `ya dump all-relations [option]... [--from <source>] --to <target>`

Options:

- `--from`: A starting target the graph will be displayed from.
- `--to`: Module name or project directory of that module.
- `--recursive`: Enables displaying dependencies to all modules from the *target* directory. Displays paths to all modules available by `RECURSE` from `target`, provided `target` is a directory.
- `--show-targets-deps`: If the `-recursive` option is enabled, also displays dependencies between all modules from the *target* directory.
- `-t`, `--force-build-depends`: When calculating dependencies, takes into account the dependencies of the tests (`DEPENDS` and `RECURSE_FOR_TESTS`).
- `--ignore-recurses`: When calculating dependencies, excludes the dependencies by `RECURSE.`
- `--no-tools`: When calculating dependencies, excludes the dependencies on tools.
- `--no-addincls`: When calculating dependencies, excludes the dependencies by `ADDINCL`.
- `--json`: Outputs all dependencies in JSON format.

**Example:**

```bash translate=no
~/yatool/devtools/ymake/bin$ ya dump all-relations --to contrib/libs/libiconv | dot -Tpng > graph.png
```

Use the `--from` option to change the starting target:

```bash translate=no
~/yatool/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv | dot -Tpng > graph.png
```

You can specify the `--from` and `--to` options multiple times. This way you can look at a fragment of the internal graph and not draw it entirely with `ya dump dot-graph`.

You can use the `--json` option to change the output format:

```bash translate=no
~/yatool/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv --json > graph.json
```

Use the `--recursive` option to output all dependencies to all modules from the *target* directory:

```bash translate=no
~/yatool/devtools/ymake/symbols$ ya dump all-relations --to library/cpp/on_disk --recursive | dot -Tpng > graph2.png
```

### ya dump dot-graph

Outputs all dependencies of a given project in `dot` format. It is similar to `ya dump modules` with the drawn dependencies between modules.

Command: `ya dump dot-graph [OPTION]... [TARGET]...`
```bash translate=no
ya dump dot-graph
...
"ydb/library/ydb_issue/proto/libpy3library-ydb_issue-proto.a" -> "contrib/python/grpcio/libpy3contrib-python-grpcio.a";
    "ydb/library/ydb_issue/proto/libpy3library-ydb_issue-proto.a" -> "library/cpp/resource/liblibrary-cpp-resource.a";
    "ydb/core/protos/libpy3ydb-core-protos.a" -> "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a";
    "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a" -> "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libscheme-statistics-protos.a";
    "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a" -> "build/platform/python/ymake_python3/platform-python-ymake_python3.pkg.fake";
...
```
### ya dump dep-graph and json-dep-graph

Outputs a dependency graph in internal (indented) or JSON format.

Command:
- `ya dump dep-graph [OPTION]... [TARGET]...`
- `ya dump json-dep-graph [OPTION]... [TARGET]...`

The `--flat-json` and `--flat-json-files` options are available for `ya dump dep-graph`. Use them to get a JSON-formatted `dep` graph. It looks like a flat list of arcs and a list of vertices.

A dependency graph usually contains file nodes and command nodes. The `--flat-json-files` option enables you to output only file nodes and dependencies between them.

### ya dump build-plan

Outputs a JSON-formatted build command graph roughly corresponding to what will be executed when running the `ya make` command.
You can get a more accurate graph by running `ya make -j0 -k -G`

Command:
`ya dump build-plan [OPTION]... [TARGET]...`

Many filtering options aren't applicable to the build command graph and aren't supported.
```bash translate=no
ya dump build-plan
Traceback (most recent call last):
  File "devtools/ya/app/__init__.py", line 733, in configure_exit_interceptor
    yield
  File "devtools/ya/app/__init__.py", line 107, in helper
    return action(args, **kwargs)
           ^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/entry/entry.py", line 48, in do_main
    res = handler.handle(handler, args, prefix=['ya'])
          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/handler.py", line 222, in handle
    return handler.handle(self, args[1:], prefix + [name])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/dispatch.py", line 38, in handle
    return self.command().handle(root_handler, args, prefix)
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/handler.py", line 222, in handle
    return handler.handle(self, args[1:], prefix + [name])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```
### ya dump loops and peerdir-loops

The commands output dependency loops between files or projects.

Command:

- `ya dump loops [OPTION]... [TARGET]...` : Displays any dependencies, including loop includes between header files.
- `ya dump peerdir-loops [OPTION]... [TARGET]...` : Displays only dependencies by `PEERDIR` between projects.

### ya dump compile-commands and compilation-database

Outputs a comma-separated list of JSON descriptions of build commands. Each command has three properties: `"command"`, `"directory"`, and `"file"`.
The compiler, target platform, dependencies, and other information are specified in the `command` parameter. The project folder is located in `directory` and the file itself is located in `file`.

Command:
`ya dump compile-commands [OPTION]... [TARGET]...`

Frequently used options:
- `-q, --quiet`: Output nothing.
- `--files-in=FILE_PREFIXES`: Output only commands with appropriate prefixes relative to the repository root for `"file"`.
- `--files-in-targets=PREFIX`: Filter by the `"directory"` prefix.
- `--no-generated`: Exclude commands for processing generated files.
- `--cmd-build-root=CMD_BUILD_ROOT`: Use a path as a build directory in commands.
- `--cmd-extra-args=CMD_EXTRA_ARGS`: Add options to commands.

The majority of `ya make` build options are supported as well:
- `--rebuild` is used to fully rebuild the project, ignoring all previously generated results, and outputs a list of build command descriptions. All intermediate and final files created during previous build processes are deleted to prevent deprecated or incorrect data from affecting the new build.
- `-C=BUILD_TARGETS` and `--target=BUILD_TARGETS` are used to set specific build targets specified by the `BUILD_TARGETS` value and output a list of build command descriptions in JSON format.
- `-j=BUILD_THREADS` and `--threads=BUILD_THREADS` are used to specify the number of build threads to be involved in the project build process.
- `--clear` is used to delete temporary data and the results of previous builds located in the project directory and outputs a list of build command descriptions.
- `--add-result=ADD_RESULT` is used to manage build system results. It enables you to determine exactly which files should be included in the final results set.
- `--add-protobuf-result` enables the build system to explicitly obtain the generated Protobuf source code for the relevant programming languages.
- `--add-flatbuf-result` enables the build system to automatically obtain the generated source code of FlatBuffers (flatc) output files for the relevant programming languages.
- `--replace-result` enables you to build only the targets specified in `--add-result` and output a list of build command descriptions.
- `--force-build-depends` ensures comprehensive preparation for testing by forcefully building all dependencies declared in `DEPENDS`.
- `-R` and `--ignore-recurses` prevent the automatic build of projects declared using the `RECURSE` macro in `ya.make` files.
- `--no-src-links` enables you to avoid creating symbolic links in the source directory and output a list of build command descriptions.
- `--stat` is used to display the build statistics.
- `-v` or `--verbose` are used to activate the detailed output mode.
- `-T` is used to output the build status in row orientation.
- `-D=FLAGS` enables you to define or redefine build environment variables (build flags) by setting their name and value.
- `--cache-stat` enables you to output statistics on local cache filling and a list of build command descriptions before building.
- `--gc` orders the system to check the local cache, clear it of unnecessary data, and only then start the build with the output of build command descriptions.
- `--gc-symlinks` is used to clear the build cache, focusing on removing symbolic links.

**Example:**
```bash translate=no
~/yatool$ ya dump compilation-database devtools/ymake/bin
...
{
    "command": "clang++ --target=x86_64-linux-gnu --sysroot=/home/spreis/.ya/tools/v4/244387436 -B/home/spreis/.ya/tools/v4/244387436/usr/bin -c -o /home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp.o /home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp -I/home/spreis/yatool -I/home/spreis/yatool -I/home/spreis/yatool/contrib/libs/linux-headers -I/home/spreis/yatool/contrib/libs/linux-headers/_nf -I/home/spreis/yatool/contrib/libs/cxxsupp/libcxx/include -I/home/spreis/yatool/contrib/libs/cxxsupp/libcxxrt -I/home/spreis/yatool/contrib/libs/zlib/include -I/home/spreis/yatool/contrib/libs/double-conversion/include -I/home/spreis/yatool/contrib/libs/libc_compat/include/uchar -fdebug-prefix-map=/home/spreis/yatool=/-B -Xclang -fdebug-compilation-dir -Xclang /tmp -pipe -m64 -g -ggnu-pubnames -fexceptions -fstack-protector -fuse-init-array -faligned-allocation -W -Wall -Wno-parentheses -Werror -DFAKEID=5020880 -Dyatool_ROOT=/home/spreis/yatool -Dyatool_BUILD_ROOT=/home/spreis/yatool -D_THREAD_SAFE -D_PTHREADS -D_REENTRANT -D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES -D_LARGEFILE_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -UNDEBUG -D__LONG_LONG_SUPPORTED -DSSE_ENABLED=1 -DSSE3_ENABLED=1 -DSSSE3_ENABLED=1 -DSSE41_ENABLED=1 -DSSE42_ENABLED=1 -DPOPCNT_ENABLED=1 -DCX16_ENABLED=1 -D_libunwind_ -nostdinc++ -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt -mcx16 -std=c++17 -Woverloaded-virtual -Wno-invalid-offsetof -Wno-attributes -Wno-dynamic-exception-spec -Wno-register -Wimport-preprocessor-directive-pedantic -Wno-c++17-extensions -Wno-exceptions -Wno-inconsistent-missing-override -Wno-undefined-var-template -Wno-return-std-move -nostdinc++",
    "directory": "/home/spreis/yatool",
    "file": "/home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp"
},
...
```
### ya dump groups

It outputs information in JSON format about all participants or selected groups, including name, participants, and mailing list.

Command: `ya dump groups [OPTION]... [GROUPS]...`

Options:
- `--all_groups`: All information about groups (default).
- `--groups_with_users`: Information only about participants.
- `--mailing_lists`: Only mailing lists.

### ya dump json-test-list

Outputs JSON-formatted information about tests.

Command: `ya dump json-test-list [OPTION]... [TARGET]...`

### ya dump recipes

Outputs JSON-formatted information about recipes used in tests.

Command: `ya dump recipes [OPTION]... [TARGET]...`

Options:
- `--json`: Output JSON-formatted information about recipes.
- `--skip-deps`: Only recipes of a given project, no recipes of dependencies.
- `--help`: A list of all options.

### ya dump conf-docs

Generates module and macro documentation in Markdown (for reading) or JSON (for automatic processing) format.

Command: `ya dump conf-docs [OPTIONS]... [TARGET]...`

Options:
- `--dump-all`: Outputs information including internal modules and macros that can't be used in `ya.make`.
- `--json`: Outputs information about all modules and macros, including internal ones, in JSON format.

### ya dump debug

Collects debugging information about the last `ya make` run. When ran locally, it is used with the `--dry-run` option.

Command: `ya dump debug [last|N] --dry-run`

- `ya dump debug`: View all available `bundles`.
- `ya dump debug last`: Collect the `bundle` from the last `ya make` run.
- `ya dump debug 2`: Collect the **penultimate** `bundle`.
- `ya dump debug 1`: Collect the last `bundle`.

**Example:**
```bash translate=no
┬─[user@linux:~/a/yatool]─[11:50:28]
╰─>$ ./ya dump debug

10: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-17 20:16:24 (v1)
9: `ya-bin make devtools/dummy_yatool/hello_world/ --stat`: 2021-06-17 20:17:06 (v1)
8: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-17 20:17:32 (v1)
7: `ya-bin make devtools/dummy_yatool/hello_world/ --stat`: 2021-06-17 20:18:14 (v1)
6: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-18 12:28:15 (v1)
5: `ya-bin make -r /Users/yatool/devtools/ya/test/programs/test_tool/bin -o /Users/build/ya --use-clonefile`: 2021-06-18 12:35:17 (v1)
4: `ya-bin make -A devtools/ya/yalibrary/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 12:51:51 (v1)
3: `ya-bin make -A devtools/ya/yalibrary/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 13:04:08 (v1)
2: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-21 10:26:31 (v1)
1: `ya-bin make -r /Users/yatool/devtools/ya/test/programs/test_tool/bin -o /Users/build/ya --use-clonefile`: 2021-06-21 10:36:21 (v1)
```
#### Additional parameters

In the context of using the `ya dump` command and its subcommands, additional `[TARGET]` parameters represent specific values or data that must be provided with the command for it to run correctly.

Unlike options that change the command's behavior, additional parameters point to specific objects, such as modules or directories, that the operation is performed on: for example, `--target=<path>`.
