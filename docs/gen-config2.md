# ya gen-config

The `ya gen-config` command is used to generate the `ya` tool configuration file. It helps you create a standard (basic) configuration file that contains a description and commented default values ​​for various settings. You can then customize the configuration file as needed.

## Usage

ya gen-config [OPTION]… [ya.conf]…

- ya gen-config path_proect/${USER}/ya.conf generates a custom configuration file in the specified location. If a configuration already exists, the new configuration retains and takes into account the previously configured parameters.

- If the user places the configuration file named ya.conf in their directory, it will automatically be used to define the ya work parameters.

- The ya.conf values have the lowest priority and can be overridden through environment variables or command line arguments.

- If the project directory (path_proect) is missing, you can save the configuration to ~/.ya/ya.conf.

### Options

- -h, --help: Show help on using the command. Use -hh to display additional options and -hhh for even more help.

## `ya.conf` format

The `ya.conf` file needs to be in [toml](https://github.com/toml-lang/toml) format. Here are the main rules for using the configuration file:

- For parameter-less options, set the value to "true".

- For options that are "dictionaries" (for example, `flags`), you need to open the corresponding section (table). This section contains lines in the `key = "value"` format.


The file contains various parameters that can be configured to control the build process, testing, caching, and other aspects of the project.

#### General project settings

| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| auto_exclude_symlinks | false | Add all symlink-dirs in modules to exclude dirs |
| copy_shared_index_config | false | Copy project config for Shared Indexes if exist |
| detect_leaks_in_pytest | true | Detect leaks in Pytest |
| directory_based | true | Create project in actual (directory based) format |
| eager_execution | false | Eager execution  |
| exclude_dirs | [] | Exclude dirs with specific names from all modules |
| external_content_root_modules | [] | Add external content root modules |
| generate_tests_for_deps | false | Generate tests for PEERDIR dependencies |
| generate_tests_run | false | Generate run configuration for junit tests |
| idea_files_root | "None" | Root for .ipr and .iws files |
| idea_jdk_version | "None" | Project JDK version |
| iml_in_project_root | false | Store ".iml" files in project root tree (stores in source root tree by default) |
| iml_keep_relative_paths | false | Keep relative paths in ".iml" files |
| minimal | false | Create the minimum set of project settings |
| omit_test_data | false | Do not export test_data |
| project_name | "None" | Idea project name (.ipr and .iws file) |
| run_tests_size | 1 | Default test sizes to run (1 for small, 2 for small+medium, 3 to run all tests) |
| with_common_jvm_args_in_junit_template | false | Add common JVM_ARGS flags to default junit template |
| with_content_root_modules | false | Generate content root modules |
| with_long_library_names | false | Generate long library names |

#### IDE integration settings
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| add_py_targets | false | Add Python 3 targets to project |
| content_root | "None" | Root directory for a CLion project |
| dry_run | false | Emulate create project, but do nothing |
| filters | [] | Only consider filtered content |
| full_targets | false | Old Mode: Enable full targets graph generation for project. |
| group_modules | "None" | Group idea modules according to paths: (tree, flat) |
| idea_project_root | "None" | IntelliJ IDEA project root path (-r, --project-root) |
| lite_mode | false | Lite mode for solution (fast open, without build) |
| local | false | Only recurse reachable projects are idea modules (-l, --local) |
| remote_build_path | "None" | Path to the directory for CMake output at the remote host |
| remote_deploy_config | "None" | Name of the remote server configuration tied to the remote toolchain |
| remote_deploy_host | "None" | Hostname associated with remote server configuration |
| remote_repo_path | "None" | Path to the remote repository at the remote host |
| remote_toolchain | "None" | Generate configurations for remote toolchain with this name |
| use_sync_server | false | Deploy local files via sync server instead of file watchers |

#### Build output settings
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| add_host_result | [] | Process selected host build output as a result |
| add_result | [] | Process selected build output as a result |
| all_outputs_to_result | false | Process all outputs of the node along with selected build output as a result |
| create_symlinks | true | Do not create any symlink in source directory |
| force_build_depends | false | Build by DEPENDS anyway |
| ignore_recurses | false | Do not build by RECURSES |
| install_dir | "None" | Path to accumulate resulting binaries and libraries |
| output_root | "None" | Directory with build results |
| suppress_outputs | [] | Do not symlink/copy output for files with given suffix, they may still be saved in cache as result |
| symlink_root | "None" | Result store root |

#### Printing settings
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| default_suppress_outputs | [".o", ".obj", ".mf", "…", ".cpf", ".cpsf", ".srclst", ".fake", ".vet.out", ".vet.txt", ".self.protodesc"] | Do not symlink/copy output for files with given suffix if not overriden with --add-result |
| ext_progress | false | Print extra progress info (--show-extra-progress) |
| mask_roots | "None" | Mask source and build root paths in stderr |
| output_style | "ninja" | Do not rewrite output information (ninja/make) |
| print_statistics | false | Show build execution statistics (--stat) |
| show_timings | false | Print execution time for commands |
| statistics_out_dir | "None" | Additional statistics output dir (--stat-dir) |

#### Platform/build configuration
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| build_type | "release" | Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) |
| host_platform | "None" | Host platform |
| preset_disable_customization | false | Disable ya make customization (--disable-customization) |
| target_platforms | [] | Target platform (--target-platform) |

#### Local cache
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| auto_clean_results_cache | true | Auto-clear results cache |
| build_cache | false | Enable build cache |
| build_cache_conf_str | [] | Override configuration options |
| build_cache_master | false | Enable build cache master mode |
| cache_codec | "None" | Cache codec (--cache-codec) |
| cache_size | 322122547200 | Max cache size (--cache-size) |
| new_store | true | Try alternative storage |
| strip_symlinks | false | Remove all symlink results except files from the current graph |
| symlinks_ttl | 604800 | Results cache TTL |
| tools_cache | false | Enable tools cache |
| tools_cache_conf_str | [] | Override configuration options |
| tools_cache_gl_conf_str | [] | Override configuration options |
| tools_cache_ini | "None" | Override tools cache built-in ini-file |
| tools_cache_master | false | Enable tools cache master mode |
| tools_cache_size | 32212254720 | Max tool cache size |

#### Graph generation
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| compress_ymake_output | false | Сжимать вывод ymake для уменьшения максимального использования памяти |
| compress_ymake_output_codec | "zstd08_1" | Кодек для сжатия вывода ymake |

#### Feature flags
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| dir_outputs_test_mode | false | Включить новый режим вывода директорий |
| dump_debug_enabled | false | Включить отладочный режим дампа |
| local_executor | true | Использовать локальный исполнитель вместо Popen |
| new_runner | true | Использовать альтернативный раннер |
| platform_schema_validation | false | Не проверять целевые платформы по схеме |
| runner_dir_outputs | true | Отключить поддержку вывода директорий в раннере |

#### Test settings:
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| cache_fs_read | false | Использовать кеш файловой системы вместо памяти (только для чтения) |
| cache_fs_write | false | Использовать кеш файловой системы вместо памяти (только для записи) |
| cache_tests | false | Использовать кеш для тестов |
| canonization_backend | "None" | Назначить бэкенд для канонизации с шаблоном |
| canonization_scheme | "https" | Протокол для бэкенда канонизации (https по умолчанию) |
| dir_outputs | true | Архивировать выходную директорию тестирования |
| dir_outputs_in_nodes | false | Включить поддержку вывода директорий в узлах |
| disable_flake8_migrations | true | Включить все проверки flake8 |
| disable_jstyle_migrations | false | Включить все проверки стиля java |
| fail_fast | false | Завершать при первом тесте с ошибкой |
| inline_diff | false | Отключить усечение комментариев и выводить diff в терминал |
| junit_args | "None" | Дополнительные параметры командной строки для JUnit |
| junit_path | "None" | Путь для генерации отчета junit |
| last_failed_tests | false | Перезапускать тесты, которые не прошли последним запуском |
| merge_split_tests | true | Не объединять разделенные тесты в директорию ({testing_out_stuff}) c макросом FORK_*TESTS |
| remove_implicit_data_path | false | Удалить неявный путь из макроса DATA |
| remove_result_node | false | Удалить узел результата из графа, печатать отчет по тестам в ya |
| remove_tos | false | Удалить верхний уровень директории {testing_out_stuff} |
| run_tagged_tests_on_yt | false | Запускать тесты с тэгом ya:yt на YT |
| show_passed_tests | false | Показывать пройденные тесты |
| show_skipped_tests | false | Показывать пропущенные тесты |
| store_original_tracefile | false | Хранить оригинальный файлы trace |
| strip_idle_build_results | false | Удалить все узлы результата (включая узлы сборки), которые не нужны для выполнения тестов |
| strip_skipped_test_deps | false | Не строить зависимости пропущенных тестов |
| test_node_output_limit | "None" | Лимит размера выводных файлов тестов (в байтах) |
| test_output_compression_filter | "zstd" | Фильтр сжатия вывода тестов (none, zstd, gzip) |
| test_output_compression_level | 1 | Уровень сжатия вывода тестов для указанного фильтра |
| test_stderr | false | Выводить stderr тестов в консоль в режиме реального времени |
| test_stdout | false | Выводить stdout тестов в консоль в режиме реального времени |
| test_traceback | "short" | Стиль backtrace для тестов ("long", "short", "line", "native", "no") |
| ytexec_wrapper_m_cpu | 250 | Требования к millicpu для distbuild. |

#### Various table options
| Option | The default value | Notes |
|----------------|-----|----------------------------------------------------|
| test_types_fakeid | | Переименовывание цветов для разметки (например, bad = "light-red") |
| terminal_profile | | Профиль терминала |
| flags | | Установка переменных (name[=val], "yes" если значение опущено) |
| host_platform_flags | | Флаги платформы хоста |
| test_size_timeouts | | Установка таймаутов тестов для каждого размера (small=60, medium=600, large=3600) |

Each parameter in the file begins with a key, followed by the equals sign (`=`) and the parameter value. The value ​​can be a Boolean (`true` or `false`), string (in quotes), or list (in square brackets).

Comments (beginning with `#`) are used to describe parameters, suggested changes, or links to additional documentation. Some parameters have default values ​​and can be changed according to project requirements or developer preferences.

The configuration file can be customized to suit the specific needs of the project. You can enable or disable certain features to optimize the development and build process.

## Order of options implementation

The order of applying options to configure the ya toolkit describes the hierarchy and logic of overriding configuration settings used when working with the build system.

The `ya` options that are specified in configuration files or passed in the command line are applied in the order set out below, where each subsequent level can override the settings of the previous one.

Possible locations:

1. `$path_proect/ya.conf`: General settings for the project.
2. `$path_proect/${USER}/ya.conf`: Custom settings within a single project.
3. `$repo/../ya.conf`: If you need different settings for different repositories.
4. `~/.ya/ya.conf`: Global user settings at the system level.
5. Environment variables.
6. Command line arguments.

### Renaming options for `ya.conf`

Configuration files can have special names that provide for changing the settings depending on a specific system or command:

- `ya.conf`: Basic configuration file.
- `ya.${system}.conf`: For system specifications.
- `ya.${command}.conf`: For command specifications.

The `${system}` and `${command}` modifiers refer configuration files to a specific system or command. For example, `ya.make.darwin.conf` for the `ya make` command in the Darwin OS.

## Examples of options for specific `ya` commands

### Global settings and local overrides
```
project_output = “/default/path”

[ide.qt]
project_output = “/path/to/qt/project”

[ide.msvs]
project_output = “c:\path\to\msvs\project”
```
In the example above, `"/default/path"` is set as the general path to the project, but the `ya ide qt` and `ya ide msvs` commands have their own paths set for them.

### Overriding dictionary options
```
[flags]
NO_DEBUGINFO = “yes”

[dump.json-test-list.flags]
MY_SPECIAL_FLAG = “yes”
```
Here, most scenarios have the `NO_DEBUGINFO="yes"` flag, but for the `ya dump json-test-list` command, the additional `MY_SPECIAL_FLAG="yes"` flag is set. `NO_DEBUGINFO` is not applied.

## Environment variable substitution

String keys can specify environment variables in the `${ENV_VAR}`format, which will be replaced once the configs load.

## Color settings

The text highlighting system of `ya` uses environment variables for controlling the color scheme in the terminal. This allows users to change the color settings for various terminal elements so as to improve readability and appearance.

```
alt1 = "cyan"
alt2 = "magenta"
alt3 = "light-blue"
bad = "red"
good = "green"
imp = "light-default"
path = "yellow"
unimp = "dark-default"
warn = "yellow"
```
To change the colors associated with these markers, you can use the terminal_profile section in the `ya.conf` configuration file. This way, you can set custom colors for each of the markers.

### Sample configuration file:
```
[terminal_profile]
bad = “light-red”
unimp = “default”
```
In the example above, the color for the `bad` marker is changed to `light-red`, while `unimp` uses the default color.

To add target platforms, just reiterate the following:
```
[[target_platform]]
platform_name = "default-darwin-arm64"
build_type = "relwithdebinfo"

[target_platform.flags]
ANY_FLAG = "flag_value"
ANY_OTHER_FLAG = "flag_value"
```
For each `--target-platform-smth` command line parameter, there is a corresponding configuration file key.

## Additional options (alias)

In `ya`, aliases let you combine frequently used arguments into a single short expression, making repetitive tasks easier and simplifying command calls. This is especially useful when you need to repeatedly specify the same arguments in `ya make` build commands.

Aliases ​​are described in configuration files using TOML syntax (array of tables). This allows for grouping settings and easily applying them when needed.

### Alias usage

#### Adding .go files

To add symlinks to generated .go files, you normally need to specify many arguments:
```
ya make path/to/project --replace-result --add-result=.go --no-output-for=.cgo1.go --no-output-for=.res.go --no-output-for=_cgo_gotypes.go --no-output-for=_cgo_import.go
```

##### Configuring aliases in ya.make.conf:
```
# path_proect/<username>/ya.make.conf
replace_result = true  # --replace-result
add_result_extend = [“.go”]  # --add-result=.go
suppress_outputs = [“.cgo1”, “.res.go”, “_cgo_gotypes.go”]  # --no-output-for options
```
##### Creating aliases in ya.conf:

```
# path_proect/<username>/ya.conf

[alias]
replace_result = true
add_result_extend = [“.go”]
suppress_outputs = [“.cgo1”, “.res.go”, “_cgo_gotypes.go”]

[alias._settings.arg]
names = [“–add-go-result”]
help = “Add generated .go files”
visible = true
```
This alias allows you to replace a long command with:
`ya make path/to/project --add-go-result`.

#### Disabling prebuild tools

To disable the use of prebuild tools in normal mode, the following arguments are needed:

`ya make path/to/project -DUSE_PREBUILT_TOOLS=no --host-platform-flag=USE_PREBUILT_TOOLS=no`

Specify the desired behavior in `ya.conf`:
```
[host_platform_flags]
USE_PREBUILT_TOOLS = “no”

[flags]
USE_PREBUILT_TOOLS = “no”
```
Now let's describe an alias that will be triggered by an argument, environment variable, or setting a value in any `ya.conf`:
```
[alias]
[alias.host_platform_flags]
USE_PREBUILT_TOOLS = “no”

[alias.flags]
USE_PREBUILT_TOOLS = “no”

[alias._settings.arg]
names = [“-p”, “–disable-prebuild-tools”]
help = “Disable prebuild tools”
visible = true

[alias._settings.env]
name = “YA_DISABLE_PREBUILD_TOOLS”

[alias._settings.conf]
name = “disable_prebuild_tools”
```
You can now use one of the following methods to invoke the behavior:
```
# Long argument:
  `ya make path/to/project --disable-prebuild-tools`
# Short argument:
  `ya make path/to/project -p`
# Environment variable:
  `YA_DISABLE_PREBUILD_TOOLS=yes ya make path/to/project`
# Config value:
  echo “\ndisable_prebuild_tools=true\n” >> path_proect/$USER/ya.conf
  ya make path/to/project
```
## Using multiple aliases

In `ya`, aliases are a flexible way to group frequently used arguments in order to simplify and automate command calls. This feature is not limited to just one alias or one file, allowing you to create and implement many aliases across multiple configuration files. In this case, aliases from different files complement each other without any setting being overwritten.

### Multiple aliases

You can create any number of aliases and include them in one or more configuration files. This affords a high degree of flexibility in setting up a development environment.

Aliases from different files don't conflict or substitute each other, which means you can combine different configuration files without the risk of losing settings.

#### Example with multiple files
```
# path/to/first/ya.conf
some_values = true

third_alias = true

[[alias]]
# ...
[alias._settings.conf]
name = "first_alias"

[[alias]]
# ...

# path/to/second/ya.conf
some_other_values = true
[[alias]]
# ...
[alias._settings.conf]
name = "third_alias"

[[alias]]
first_alias = true
# ...

```
In this example, the alias configurations from two different files will both be implemented without them affecting each other.

### Example with target_platform
```
[[alias]]

[[alias.target_platform]]  # --target-platform
platfom_name = "..."
build_type = "debug"  # --target-platform-debug
run_tests = true  # --target-platform-tests
target_platform_compiler = "c_compiler"  # --target-platform-c-compiler
# ...
[alias.target_platform.flags]  # --target-platform-flag
FLAG = true
OTHER_FLAG = "other_value"

[alias._settings.arg]  # Create argument consumer for alias
names = ["-a", "--my-cool-alias"]  # Short and long name
help = "This alias is awesome! It really helps me"  # Help string
visible = true  # make it visible in `ya make --help`
[alias._settings.env]  # Create environment consumer for alias, must start with YA
name = "YA_MY_COOL_ALIAS"
[alias._settings.conf]  # Create config consumer for alias, can be enabled from any config-file
name = "my_cool_alias"

```

## Alias semantics

In a single `[[alias]]` block, you can specify any number of options and suboptions.

### Creating an argument or environment variable

To add an argument or environment variable, use the `[alias._settings.arg]` and `[alias._settings.env]` keys. Settings defined in this way become available in all ya subcommands.

To create an option that will exist only in a specific command (for example, "make"), just add a prefix between the `alias` and `settings` keys:
```
- [alias.make._settings.args]: Active only for "ya make" ...
- [alias.ide.idea._settings.args]: Works only for "ya ide idea" ...
```
### Enabling via a configuration file

The `[alias._settings.conf]` key allows you to enable a specific alias using any configuration file. This enhances flexibility by making it possible to activate even the aliases that are described in files which run earlier in the processing order.

Thus, you can use one alias inside another providing for the most basic composition.

## Composition

If you need to bring out the common part from aliases, you can use composition.
Let's say you have two aliases:
```
[[alias]]
first_value = 1
second_value = 2
[[alias._settings.arg]]
names = ["--first"]

[[alias]]
second_value = 2
third_value = 3
[[alias._settings.arg]]
names = ["--second"]
```
To bring out the common part, you need to create a new alias with the following configuration parameter:
```
[[alias]]
second_value = 2
[[alias._settings.conf]]
name = "common_alias"

[[alias]]
first_value = 1
common_alias = true  # Call alias
[[alias._settings.arg]]
names = ["--first"]

[[alias]]
common_alias = true  # Call alias
third_value = 3
[[alias._settings.arg]]
names = ["--second"]
```
Now, calling `ya <command> --second` will invoke the `common_alias` alias and set the value for `third_value`.

Notes:

- You can call multiple aliases from within another alias.
- There is no limit on the nesting depth.
- There is a protection against loops.
- You can use aliases declared later than the point of use or located in another file
