# ya gen-config

Команда ya gen-config используется для генерации конфигурационного файла инструмента ya. Это позволяет пользователю создать стандартный (базовый) конфигурационный файл, содержащий описание и закомментированные значения по умолчанию для различных настроек. Пользователь может затем настроить файл конфигурации в соответствии со своими требованиями.

## Использование

ya gen-config [OPTION]… [ya.conf]…

- ya gen-config junk/${USER}/ya.conf генерирует пользовательский конфигурационный файл в указанном месте. Если конфигурация уже существует, новая конфигурация сохраняет и учитывает ранее заданные параметры.

- Если пользователь поместит конфигурацию в свой каталог junk с именем ya.conf, она будет автоматически использоваться для определения параметров работы ya.

- Значения в ya.conf имеют наименьший приоритет и могут быть переопределены через переменные окружения или аргументы командной строки.

- Если каталог junk отсутствует, можно сохранить конфигурацию в ~/.ya/ya.conf.

### Опции

- -h, --help - Показать справку по использованию команды. Используйте -hh для вывода дополнительных опций и -hhh для ещё более расширенной помощи.

## Формат ya.conf

Файл ya.conf должен быть в формате [toml](https://github.com/toml-lang/toml). Вот основные правила для работы с конфигурационным файлом:

- Для опций без параметров следует указывать true в качестве значения.

- Для опций, представляющих собой “словари” (например, flags), необходимо открыть соответствующую секцию (таблицу). В этой секции указываются записи в формате key = "value".

## Пример ya.conf 
```
# Save config to the junk/{USER}/ya.conf or to the ~/.ya/ya.conf
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
# Path to the arc repository at the remote host (--remote-repo-path)
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
# ========== Selective checkout ===============================================
#
# Prefetch directories needed for build (--prefetch)
# prefetch = false
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
# Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type (--build)
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
# ========== YT cache =========================================================
#
# Bazel-remote base URI (--bazel-remote-base-uri)
# bazel_remote_baseuri = "http://[::1]:8080/"
#
# Bazel-remote password file (--bazel-remote-password-file)
# bazel_remote_password_file = "None"
#
# Use Bazel-remote storage (--bazel-remote-store)
# bazel_remote_store = false
#
# Bazel-remote username (--bazel-remote-username)
# bazel_remote_username = "None"
#
# Remove all non-tool binaries from build results. Works only with --bazel-remote-put mode (--dist-cache-evict-bins)
# dist_cache_evict_binaries = false
#
# Don't build or download build results if they are present in the dist cache (--dist-cache-evict-cached)
# dist_cache_evict_cached = false
#
# YT storage cypress directory pass (--yt-dir)
# yt_dir = "//home/devtools/cache"
#
# YT storage proxy (--yt-proxy)
# yt_proxy = "hahn.yt.yandex.net"
#
# Use YT storage (--yt-store)
# yt_store = true
#
# On read mark cache items as fresh (simulate LRU) (--yt-store-refresh-on-read)
# yt_store_refresh_on_read = false
#
# YT store max threads (--yt-store-threads)
# yt_store_threads = 1
#
# YT token path (--yt-token-path)
# yt_token_path = "/home/mtv2000/.yt/token"
#
# ========== YT cache put =====================================================
#
# YT store filter (--yt-store-filter)
# yt_cache_filter = "None"
#
# YT storage max size (--yt-max-store-size)
# yt_max_cache_size = "None"
#
# Upload to YT store (--yt-put)
# yt_readonly = true
#
# YT store codec (--yt-store-codec)
# yt_store_codec = "None"
#
# YT store ttl in hours(0 for infinity) (--yt-store-ttl)
# yt_store_ttl = 24
#
# Populate local cache while updating YT store (--yt-write-through)
# yt_store_wt = true
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
# ========== Bullet-proof options =============================================
#
# Setup default arcadia's clang-tidy config in a project (--setup-tidy)
# setup_tidy = false
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
# ========== Upload to mds ====================================================
#
# Upload to MDS (--mds)
# mds = false
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
В файле представлены различные параметры, которые можно настроить для управления процессом сборки, тестирования, интеграции с IDE, кэшированием и другими аспектами работы с проектом. Ниже представлено краткое описание основных разделов и параметров конфигурационного файла:

1. Общие настройки проекта:
- auto_exclude_symlinks: исключить символические ссылки из директорий модулей.
- directory_based: создать проект в формате, основанном на директориях.
- oauth_exchange_ssh_keys: обмен SSH ключами через OAuth.

2. Настройки для интеграции с IDE:
- idea_files_root, idea_jdk_version: настройки для файлов проекта IntelliJ IDEA.
- external_content_root_modules: добавление внешних корневых модулей содержимого.

3. Настройки тестирования:
- detect_leaks_in_pytest: обнаружение утечек в PyTest.
- run_tests_size: определение размера тестов для выполнения.

4. Интеграция с системами сборки и кэширования:
- build_threads, continue_on_fail: настройки для процесса сборки.
- auto_clean_results_cache, build_cache: настройки локального кэша результатов сборки.

5. YT и Bazel кэш:
- yt_store, bazel_remote_store: использование удаленного кэширования для ускорения сборки.

6. Настройки вывода:
- mask_roots, output_style: настройки для управления выводом информации во время сборки.

7. Параметры платформы/конфигурации сборки:
- build_type, target_platforms: настройки для определения типа сборки и целевых платформ.

8. Опции для разработчиков:
- use_json_cache, validate_build_root_content: параметры для ускорения разработки и проверки корректности сборки.

Каждый параметр в файле начинается с ключа, за которым следует равно (=) и значение параметра. Значения могут быть логическими (true или false), строковыми (заключенными в кавычки) или списочными (заключенными в квадратные скобки).

Комментарии (начинающиеся с #) используются для описания параметров, предлагаемых изменений или ссылок на дополнительную документацию. Некоторые параметры имеют значения по умолчанию и могут быть изменены в соответствии с требованиями проекта или предпочтениями разработчика.

Конфигурационный файл можно настроить под конкретные нужды проекта, активировав или деактивировав определенные функции, чтобы оптимизировать процесс разработки и сборки. 

Более подробный перевод параметров нможно изучить [здесь](ya_conf.md)






