# ya gen-config

Команда `ya gen-config` используется для генерации конфигурационного файла инструмента `ya`. Это позволяет пользователю создать стандартный (базовый) конфигурационный файл, содержащий описание и закомментированные значения по умолчанию для различных настроек. Пользователь может затем настроить файл конфигурации в соответствии со своими требованиями.

## Использование

`ya gen-config [OPTION]… [ya.conf]…`

- ya gen-config path_proect/${USER}/ya.conf генерирует пользовательский конфигурационный файл в указанном месте. Если конфигурация уже существует, новая конфигурация сохраняет и учитывает ранее заданные параметры.

- Если пользователь поместит конфигурацию в свой домашний каталог с именем ya.conf, она будет автоматически использоваться для определения параметров работы ya.

- Значения в ya.conf имеют наименьший приоритет и могут быть переопределены через переменные окружения или аргументы командной строки.

- Если каталог проекта (path_proect) отсутствует, можно сохранить конфигурацию в ~/.ya/ya.conf.

### Опции

- -h, --help - Показать справку по использованию команды. Используйте -hh для вывода дополнительных опций и -hhh для ещё более расширенной помощи.

## Формат `ya.conf`

Файл `ya.conf` должен быть в формате [toml](https://github.com/toml-lang/toml). Вот основные правила для работы с конфигурационным файлом:

- Для опций без параметров следует указывать true в качестве значения.

- Для опций, представляющих собой “словари” (например, `flags`), необходимо открыть соответствующую секцию (таблицу). В этой секции указываются записи в формате `key = "value"`.

## Пример ya.conf 
```
# Save config to the path_proect/{USER}/ya.conf or to the ~/.ya/ya.conf
#
# Add all symlink-dirs in modules to exclude dirs (--auto-exclude-symlinks)
# auto_exclude_symlinks = false
#
# Copy project config for Shared Indexes if exist (--copy-shared-index-config)
# copy_shared_index_config = false
#
# detect_leaks_in_pytest = true
#
# Create project in actual (directory based) format (--directory-based)
# directory_based = true
#
# eager_execution = false
#
# Exclude dirs with specific names from all modules (--exclude-dirs)
# exclude_dirs = []
#
# Add external content root modules (--external-content-root-module)
# external_content_root_modules = []
#
# fail_maven_export_with_tests = false
#
# Generate tests for PEERDIR dependencies (--generate-tests-for-dependencies)
# generate_tests_for_deps = false
#
# Generate run configuration for junit tests (--generate-junit-run-configurations)
# generate_tests_run = false
#
# Root for .ipr and .iws files (--idea-files-root)
# idea_files_root = "None"
#
# Project JDK version (--idea-jdk-version)
# idea_jdk_version = "None"
#
# Store ".iml" files in project root tree(stores in source root tree by default) (--iml-in-project-root)
# iml_in_project_root = false
#
# Keep relative paths in ".iml" files (works with --iml-in-project-root) (--iml-keep-relative-paths)
# iml_keep_relative_paths = false
#
# Create the minimum set of project settings (--ascetic)
# minimal = false
#
# oauth_exchange_ssh_keys = true
#
# oauth_token_path = "None"
#
# Do not export test_data (--omit-test-data)
# omit_test_data = false
#
# List of test statuses omitted by default. Use '-P' to see all tests. Acceptable statuses: crashed, deselected, diff, fail, flaky, good, internal, missing, not_launched, skipped, timeout, xfail, xfaildiff, xpass
# omitted_test_statuses = [ "good", "xfail", "not_launched",]
#
# Idea project name (.ipr and .iws file) (--project-name)
# project_name = "None"
#
# regenarate_with_project_update = "None"
#
# Default test sizes to run (1 for small, 2 for small+medium, 3 to run all tests)
# run_tests_size = 1
#
# Do not merge tests modules with their own libraries (--separate-tests-modules)
# separate_tests_modules = false
#
# setup_pythonpath_env = true
#
# strip_non_executable_target = "None"
#
# test_fakeid = ""
#
# use_atd_revisions_info = false
#
# use_command_file_in_testtool = false
#
# use_jstyle_server = false
#
# use_throttling = false
#
# Add common JVM_ARGS flags to default junit template (--with-common-jvm-args-in-junit-template)
# with_common_jvm_args_in_junit_template = false
#
# Generate content root modules (--with-content-root-modules)
# with_content_root_modules = false
#
# Generate long library names (--with-long-library-names)
# with_long_library_names = false
#
# ya_bin3_required = "None"
#
# ========== Idea project options =============================================
#
# Add Python 3 targets to project (--add-py3-targets)
# add_py_targets = false
#
# Root directory for a CLion project (-r, --project-root)
# content_root = "None"
#
# Emulate create project, but do nothing (-n, --dry-run)
# dry_run = false
#
# Only consider filtered content (-f, --filter)
# filters = []
#
# Old Mode: Enable full targets graph generation for project. (--full-targets)
# full_targets = false
#
# Group idea modules according to paths: (tree, flat) (--group-modules)
# group_modules = "None"
#
# IntelliJ IDEA project root path (-r, --project-root)
# idea_project_root = "None"
#
# Lite mode for solution (fast open, without build) (-m, --mini)
# lite_mode = false
#
# Only recurse reachable projects are idea modules (-l, --local)
# local = false
#
# Path to the directory for CMake output at the remote host (--remote-build-path)
# remote_build_path = "None"
#
# Name of the remote server configuration tied to the remote toolchain (--remote-deploy-config)
# remote_deploy_config = "None"
#
# Hostname associated with remote server configuration (--remote-host)
# remote_deploy_host = "None"
#
# Path to the remote repository at the remote host (--remote-repo-path)
# remote_repo_path = "None"
#
# Generate configurations for remote toolchain with this name (--remote-toolchain)
# remote_toolchain = "None"
#
# Deploy local files via sync server instead of file watchers (--use-sync-server)
# use_sync_server = false
#
# ========== Integration wuth IDE plugin ======================================
#
# Type of a project to use in `ya project update` upon regernation from Idea (--project-update-kind)
# project_update_kind = "None"
#
# Run `ya project update` for this dirs upon regeneration from Idea (--project-update-targets)
# project_update_targets = []
#
# ========== Ya operation control =============================================
#
# Build threads count (-j, --threads)
# build_threads = 2
#
# Build as much as possible (-k, --keep-going)
# continue_on_fail = false
#
# Custom build directory (autodetected by default) (-B, --build-dir)
# custom_build_directory = "None"
#
# Set default node requirements, use `None` to disable (--default-node-reqs)
# default_node_requirements_str = "None"
#
# Fetchers priorities and params (--fetcher-params)
# fetcher_params_str = "None"
#
# Link threads count (--link-threads)
# link_threads = 0
#
# Do not use ymake caches on retry (--no-ymake-caches-on-retry)
# no_caches_on_retry = false
#
# Enable additional cache based on content-only dynamic uids [default] (--content-uids)
# request_content_uids = false
#
# Set nice value for build processes (--nice)
# set_nice_value = 10
#
# Use clonefile instead of hardlink on macOS (--use-clonefile)
# use_clonefile = true
#
# ========== Build output =====================================================
#
# Process selected host build output as a result (--add-host-result)
# add_host_result = []
#
# Process selected build output as a result (--add-result)
# add_result = []
#
# Process all outputs of the node along with selected build output as a result (--all-outputs-to-result)
# all_outputs_to_result = false
#
# Do not create any symlink in source directory (--no-src-links)
# create_symlinks = true
#
# Build by DEPENDS anyway (--force-build-depends)
# force_build_depends = false
#
# Do not build by RECURSES (--ignore-recurses)
# ignore_recurses = false
#
# Path to accumulate resulting binaries and libraries (-I, --install)
# install_dir = "None"
#
# Directory with build results (-o, --output)
# output_root = "None"
#
# Do not symlink/copy output for files with given suffix, they may still be save in cache as result (--no-output-for)
# suppress_outputs = []
#
# Result store root (--result-store-root)
# symlink_root = "None"
#
# ========== Printing =========================================================
#
# Do not symlink/copy output for files with given suffix if not overriden with --add-result (--no-output-default-for)
# default_suppress_outputs = [ ".o", ".obj", ".mf", "..", ".cpf", ".cpsf", ".srclst", ".fake", ".vet.out", ".vet.txt", ".self.protodesc",]
#
# Print extra progress info (--show-extra-progress)
# ext_progress = false
#
# Mask source and build root paths in stderr (--mask-roots)
# mask_roots = "None"
#
# Do not rewrite output information (ninja/make) (-T)
# output_style = "ninja"
#
# Show build execution statistics (--stat)
# print_statistics = false
#
# Print execution time for commands (--show-timings)
# show_timings = false
#
# Additional statistics output dir (--stat-dir)
# statistics_out_dir = "None"
#
# ========== Platform/build configuration =====================================
#
# Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug)
# build_type = "release"
#
# Host platform (--host-platform)
# host_platform = "None"
#
# Disable ya make customozation (--disable-customization)
# preset_disable_customization = false
#
# Target platform (--target-platform)
# target_platforms = []
#
# ========== Local cache ======================================================
#
# Auto clean results cache (--auto-clean)
# auto_clean_results_cache = true
#
# enable build cache (--ya-ac)
# build_cache = false
#
# Override configuration options (--ya-ac-conf)
# build_cache_conf_str = []
#
# enable build cache master mode (--ya-ac-master)
# build_cache_master = false
#
# Cache codec (--cache-codec)
# cache_codec = "None"
#
# Max cache size (--cache-size)
# cache_size = 322122547200
#
# Try alternative storage (--new-store)
# new_store = true
#
# Remove all symlink results except files from the current graph (--gc-symlinks)
# strip_symlinks = false
#
# Results cache TTL (--symlinks-ttl)
# symlinks_ttl = 604800
#
# enable tools cache (--ya-tc)
# tools_cache = false
#
# Override configuration options (--ya-tc-conf)
# tools_cache_conf_str = []
#
# Override configuration options (--ya-gl-conf)
# tools_cache_gl_conf_str = []
#
# Override tools cache built-in ini-file (--ya-tc-ini)
# tools_cache_ini = "None"
#
# enable tools cache master mode (--ya-tc-master)
# tools_cache_master = false
#
# Max tool cache size (--tools-cache-size)
# tools_cache_size = 32212254720
#
# ========== Graph generation =================================================
#
# Compress ymake output to reduce max memory usage (--compress-ymake-output)
# compress_ymake_output = false
#
# Codec to compress ymake output with (--compress-ymake-output-codec)
# compress_ymake_output_codec = "zstd08_1"
#
# ========== Feature flags ====================================================
#
# Enable new dir outputs features (--dir-outputs-test-mode)
# dir_outputs_test_mode = false
#
# Enable dump debug (--dump-debug)
# dump_debug_enabled = false
#
# Use local executor instead of Popen (--local-executor)
# local_executor = true
#
# Try alternative runner (--new-runner)
# new_runner = true
#
# Do not validate target-platforms (--disable-platform-schema-validation)
# platform_schema_validation = false
#
# Disable dir_outputs support in runner (--disable-runner-dir-outputs)
# runner_dir_outputs = true
#
# ========== Testing ==========================================================
#
# Use FS cache instead memory cache (only read) (--cache-fs-read)
# cache_fs_read = false
#
# Use FS cache instead memory cache (only write) (--cache-fs-write)
# cache_fs_write = false
#
# Use cache for tests (--cache-tests)
# cache_tests = false
#
# Allows to specify backend for canonical data with pattern (--canonization-backend)
# canonization_backend = "None"
#
# Allows to specify canonization backend protocol(https by default) (--canonization-scheme)
# canonization_scheme = "https"
#
# Tar testing output dir in the intermediate machinery (--no-dir-outputs)
# dir_outputs = true
#
# Enable dir outputs support in nodes (--dir-outputs-in-nodes)
# dir_outputs_in_nodes = false
#
# Enable all flake8 checks (--disable-flake8-migrations)
# disable_flake8_migrations = true
#
# Enable all java style checks (--disable-jstyle-migrations)
# disable_jstyle_migrations = false
#
# Fail after the first test failure (--fail-fast)
# fail_fast = false
#
# Disable truncation of the comments and print diff to the terminal (--inline-diff)
# inline_diff = false
#
# JUnit extra command line options (--junit-args)
# junit_args = "None"
#
# Path to junit report to be generated (--junit)
# junit_path = "None"
#
# Restart tests which failed in last run for chosen target (-X, --last-failed-tests)
# last_failed_tests = false
#
# Don't merge split tests testing_out_stuff dir (with macro FORK_*TESTS) (--dont-merge-split-tests)
# merge_split_tests = true
#
# Remove implicit path from DATA macro (--remove-implicit-data-path)
# remove_implicit_data_path = false
#
# remove result node from graph, print test report in ya and report skipped suites after configure (--remove-result-node)
# remove_result_node = false
#
# remove top level testing_out_stuff directory (--remove-tos)
# remove_tos = false
#
# Run tests marked with ya:yt tag on the YT (--run-tagged-tests-on-yt)
# run_tagged_tests_on_yt = false
#
# Show passed tests (-P, --show-passed-tests)
# show_passed_tests = false
#
# Show skipped tests (--show-skipped-tests)
# show_skipped_tests = false
#
# Store original trace file (--store-original-tracefile)
# store_original_tracefile = false
#
# Remove all result nodes (including build nodes) that are not required for tests run (--strip-idle-build-results)
# strip_idle_build_results = false
#
# Don't build skipped test's dependencies (--strip-skipped-test-deps)
# strip_skipped_test_deps = false
#
# Specifies output files limit(bytes) (--test-node-output-limit)
# test_node_output_limit = "None"
#
# Specifies compression filter for tos.tar (none, zstd, gzip) (--test-output-compression-filter)
# test_output_compression_filter = "zstd"
#
# Specifies compression level for tos.tar using specified compression filter (--test-output-compression-level)
# test_output_compression_level = 1
#
# Output test stderr to console online (--test-stderr)
# test_stderr = false
#
# Output test stdout to console online (--test-stdout)
# test_stdout = false
#
# Test traceback style for pytests ("long", "short", "line", "native", "no") (--test-traceback)
# test_traceback = "short"
#
# specify millicpu requirements for distbuild.") (--ytexec-wrapper-m-cpu)
# ytexec_wrapper_m_cpu = 250
#
# ========== Packaging ========================================================
#
# Disable docker cache (--docker-no-cache)
# docker_no_cache = false
#
# ========== Advanced =========================================================
#
# Hide MacOS arm64 host warning (--hide-arm64-host-warning)
# hide_arm64_host_warning = false
#
# ========== For Ya developers ================================================
#
# Clear ymake cache (-xx)
# clear_ymake_cache = false
#
# Do not remove temporary data incrementally (--incremental-build-dirs-cleanup)
# incremental_build_dirs_cleanup = false
#
# Cache TTL in seconds
# new_store_ttl = 259200
#
# Use cache for json-graph in ymake (-xs)
# use_json_cache = true
#
# Validate build root content by cached hash in content_uids mode (--validate-build-root-content)
# validate_build_root_content = false
#
# ========== Authorization ====================================================
#
# oAuth token (--token)
# oauth_token = "None"
#
# ========== Various table options ============================================
#
# Uncomment table name with parameters
#
# [test_types_fakeid]
#
# Allows to remap markup colors (e.g bad = "light-red")
# [terminal_profile]
#
# Set variables (name[=val], "yes" if val is omitted) (-D)
# [flags]
#
# Host platform flag (--host-platform-flag)
# [host_platform_flags]
#
# Set test timeout for each size (small=60, medium=600, large=3600) (--test-size-timeout)
# [test_size_timeouts]
```

В файле представлены различные параметры, которые можно настроить для управления процессом сборки, тестирования, интеграции с IDE, кэшированием и другими аспектами работы с проектом.

К сожалению, сейчас нет удобного инструмента для того, чтобы определить, что делает тот или иной параметр. В будущем мы планируем добавить описания для параметров конфигурации и сделать работу с ними прозрачней.

Пока для того, чтобы настроить нужный аспект работы `ya` через файл конфигурации нужно:
- Найти аргумент в исходном коде (например `-j`, он будет обёрнут в класс `*Consumer`)
- Найти переменную, которая выставляется с помощью аргумента, обычно это `*Hook` (в данном случае `build_threads`)
- Найти соответствующий `ConfigConsumer`, это и будет нужное название параметра

Конфигурационный файл можно настроить под конкретные нужды проекта, активировав или деактивировав определенные функции, чтобы оптимизировать процесс разработки и сборки. 

## Порядок применения опций

Порядок применения опций для настройки инструментария ya описывает иерархию и логику переопределения настроек конфигураций, которые используются при работе с системой сборки.

Опции `ya`, указанные в файлах конфигурации или переданные через командную строку, применяются в следующем порядке, где каждый последующий уровень может переопределять настройки предыдущего.

Вот возможные места:

1. `$path_proect/ya.conf` - общие настройки для проекта.
2. `$path_proect/${USER}/ya.conf` - пользовательские настройки в рамках одного проекта.
3. `$repo/../ya.conf` - если требуется иметь разные настройки для разных репозиториев.
4. `~/.ya/ya.conf` - глобальные пользовательские настройки на уровне системы.
5. Переменные окружения.
6. Аргументы командной строки.

### Возможности именования `ya.conf`

Файлы конфигурации могут иметь специализированные имена, которые позволяют менять настройки в зависимости от конкретной системы или команды:

- `ya.conf` - базовый файл конфигурации.
- `ya.${system}.conf` - для конкретной операционной системы.
- `ya.${command}.conf` - для конкретной команды.
- `ya.${command}.${system}.conf` - для конкретной команды под конкретной операционной системе.

Модификаторы `${system}` и `${command}` адресуют конфигурационные файлы к определенной системе или команде, например, `ya.make.darwin.conf` для команды `ya make` на системе darwin.

## Примеры опций для конкретных команд `ya`

### Глобальные настройки и локальные переопределения
```
project_output = “/default/path”

[ide.qt]
project_output = “/path/to/qt/project”

[ide.msvs]
project_output = “c:\path\to\msvs\project”
```
В приведенном примере задается общий путь до проекта как `"/default/path"`, однако для команд `ya ide qt` и `ya ide msvs` устанавливаются специализированные пути.

### Переопределение словарных опций
```
[flags]
NO_DEBUGINFO = “yes”

[dump.json-test-list.flags]
MY_SPECIAL_FLAG = “yes”
```
Здесь для большинства сценариев используется флаг `NO_DEBUGINFO="yes"`, но для команды `ya dump json-test-list` задается дополнительный флаг `MY_SPECIAL_FLAG="yes"`, в то время как `NO_DEBUGINFO` не применяется.

## Подстановка переменных окружения

Строковые ключи могут указывать переменные окружения в формате `${ENV_VAR}`, которые будут подменяться после загрузки конфигов.

## Настройка цветов

`ya` использует систему маркировки текста с применением переменных окружения для управления цветовой схемой в терминале. Это позволяет пользователям менять настройки цветового отображения различных элементов терминала для улучшения читаемости и визуального восприятия.

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
Для изменения цветов, связанных с этими маркерами, можно использовать секцию terminal_profile в конфигурационном файле `ya.conf`. Это позволяет задать пользовательские цвета для каждого из маркеров.

### Пример конфигурационного файла:
```
[terminal_profile]
bad = “light-red”
unimp = “default”
```
В примере выше, цвет для маркера `bad` изменен на `light-red` (светло-красный), а для `unimp` используется цвет по умолчанию.

Чтобы добавить интересующие целевые платформы, достаточно несколько раз описать следующую конструкцию:
```
[[target_platform]]
platform_name = "default-darwin-arm64"
build_type = "relwithdebinfo"

[target_platform.flags]
ANY_FLAG = "flag_value"
ANY_OTHER_FLAG = "flag_value"
```
На каждый параметр командной строки `--target-platform-smth` существует аналогичный ключ для файла конфигурации.

## Описание дополнительных опций (alias)

Alias в `ya` позволяет объединять часто используемые аргументы в единое короткое обозначение, облегчая выполнение повторяющихся задач и упрощая командные вызовы. Это особенно полезно, когда нужно постоянно задавать одни и те же аргументы в командах сборки `ya make`.

Alias-ы описываются в конфигурационных файлах с использованием синтаксиса TOML Array of Tables. Это позволяет группировать настройки и легко применять их при необходимости.

### Примеры использования Alias

#### Добавление .go файлов

Для добавления симлинков на сгенерированные .go файлы в обычном режиме необходимо указать множество аргументов:
```
ya make path/to/project --replace-result --add-result=.go --no-output-for=.cgo1.go --no-output-for=.res.go --no-output-for=_cgo_gotypes.go --no-output-for=_cgo_import.go
```

##### Конфигурация alias-а в ya.make.conf:
```
# path_proect/<username>/ya.make.conf
replace_result = true  # --replace-result
add_result_extend = [“.go”]  # --add-result=.go
suppress_outputs = [“.cgo1”, “.res.go”, “_cgo_gotypes.go”]  # --no-output-for options
```
##### Создание alias-а в ya.conf:

```
# path_proect/<username>/ya.conf

[[alias]]
replace_result = true
add_result_extend = [“.go”]
suppress_outputs = [“.cgo1”, “.res.go”, “_cgo_gotypes.go”]

[alias._settings.arg]
names = [“–add-go-result”]
help = “Add generated .go files”
visible = true
```
Такой alias позволяет заменить длинную команду на:
`ya make path/to/project --add-go-result`

#### Отключение предпостроенных тулов

Для отключения использования предпостроенных тулов в обычном режиме нужны следующие аргументы:

`ya make path/to/project -DUSE_PREBUILT_TOOLS=no --host-platform-flag=USE_PREBUILT_TOOLS=no`

Описание желаемого поведения пропишем в `ya.conf`:
```
[host_platform_flags]
USE_PREBUILT_TOOLS = “no”

[flags]
USE_PREBUILT_TOOLS = “no”
```
Теперь опишем alias, который будет включаться по аргументу, переменной окружения или выставлением значения в любом `ya.conf`:
```
[[alias]]
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
Теперь для активации поведения можно использовать один из следующих способов:
```
# Длинный аргумент:
  `ya make path/to/project --disable-prebuild-tools`
# Короткий аргумент:
  `ya make path/to/project -p`
# Переменная окружения:
  `YA_DISABLE_PREBUILD_TOOLS=yes ya make path/to/project`
# Значение в конфиге:
  echo “\ndisable_prebuild_tools=true\n” >> path_proect/$USER/ya.conf
  ya make path/to/project
```
## Работа с несколькими Alias-ами

Alias-ы в `ya` предлагают гибкий способ для упрощения и автоматизации командных вызовов, предоставляя возможность группировать часто используемые аргументы. Эта возможность не ограничивается лишь одним alias-ом или одним файлом, а позволяет создавать и применять множество alias-ов, разбросанных по различным файлам конфигурации. При этом alias-ы из разных файлов дополняют друг друга, а не перезаписывают настройки.

### Множественные Alias-ы

Можно создавать любое количество alias-ов, включая их в один или несколько файлов конфигурации. Это обеспечивает значительную гибкость в настройке среды разработки.

Alias-ы, определенные в разных файлах, не конфликтуют и не заменяют друг друга, что позволяет комбинировать различные конфигурационные файлы без риска потери настроек.

#### Пример с множественными файлами
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
В этом примере, конфигурации alias-ов из двух разных файлов будут успешно применены и не повлияют друг на друга отрицательно.

### Пример с использованием target_platform
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
help = "This alias are awesome! It really helps me"  # Help string
visible = true  # make it visible in `ya make --help` 
[alias._settings.env]  # Create environment consumer for alias, must starts with YA
name = "YA_MY_COOL_ALIAS"
[alias._settings.conf]  # Create config consumer for alias, can be enabled from any config-file
name = "my_cool_alias"

```

## Семантика Alias-ов

Внутри одного блока `[[alias]]` можно задавать произвольное количество опций и подопций.

### Создание аргумента или переменной окружения

Для добавления аргумента или переменной окружения, используются ключи `[alias._settings.arg]` и `[alias._settings.env]`. Определенные таким образом настройки становятся доступными во всех подкомандах ya.

Для создания опции, которая будет существовать только в конкретной команде (например make), достаточно дописать между ключами `alias` и `settings` произвольный префикс:
```
- [alias.make._settings.args] – будет активно только для ya make ...
- [alias.ide.idea._settings.args] – работает только для ya ide idea ...
```
### Включение через конфигурационный файл

Ключ `[alias._settings.conf]` позволяет включить определенный alias через любой конфигурационный файл. Это добавляет уровень гибкости, позволяя активировать alias, даже если он описан в файле, который применяется раньше по порядку обработки.

таким образом, появляется возможность применять один alias внутри другого, обеспечивая таким образом простейшую композицию.

## Композиция

Если возникла необходимость вынести общую часть из alias-ов, можно воспользоваться композицией.
Пусть у нас есть два alias-а:
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
Чтобы вынести общую часть, нужно создать новый alias c параметром конфигурации:
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
Теперь, при вызове `ya <команда> --second` применится alias `common_alias`, и выставится значение для `third_value`.

Особенности:

- Можно вызывать несколько alias-ов внутри другого alias-а
- Глубина «вложенности» может быть любой
- Есть защита от циклов
- Можно использовать alias, объявленный позже места использования или находящийся в другом файле
