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

## Sample ya.conf
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
# Store ".iml" files in project root tree (stores in source root tree by default) (--iml-in-project-root)
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
# List of test statuses omitted by default. Use "-P" to see all tests. Allowed statuses: crashed, deselected, diff, fail, flaky, good, internal, missing, not_launched, skipped, timeout, xfail, xfaildiff, xpass
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
# Type of a project to use in "ya project update" upon regernation from Idea (--project-update-kind)
# project_update_kind = "None"
#
# Run "ya project update" for this dirs upon regeneration from Idea (--project-update-targets)
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
# Enable additional cache based on content-only dynamic UIDs [default] (--content-uids)
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
# Do not symlink/copy output for files with given suffix, they may still be saved in cache as result (--no-output-for)
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
# Disable ya make customization (--disable-customization)
# preset_disable_customization = false
#
# Target platform (--target-platform)
# target_platforms = []
#
# ========== Local cache ======================================================
#
# Auto-clear results cache (--auto-clean)
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
# Use FS cache instead of memory cache (only read) (--cache-fs-read)
# cache_fs_read = false
#
# Use FS cache instead of memory cache (only write) (--cache-fs-write)
# cache_fs_write = false
#
# Use cache for tests (--cache-tests)
# cache_tests = false
#
# Allows to specify backend for canonical data with pattern (--canonization-backend)
# canonization_backend = "None"
#
# Allows to specify canonization backend protocol (https by default) (--canonization-scheme)
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
# Remove result node from graph, print test report in ya and report skipped suites after configure (--remove-result-node)
# remove_result_node = false
#
# Remove top level testing_out_stuff directory (--remove-tos)
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
# Remove all result nodes (including build nodes) that are not required for running tests (--strip-idle-build-results)
# strip_idle_build_results = false
#
# Don't build skipped test's dependencies (--strip-skipped-test-deps)
# strip_skipped_test_deps = false
#
# Specifies output files limit (bytes) (--test-node-output-limit)
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
# Allows to remap markup colors (e.g., bad = "light-red")
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
The file contains various parameters that can be configured to control the build process, testing, IDE integration, caching, and other aspects of the project. Below is a brief description of the main sections and parameters of the configuration file:

1. General project settings:
- `auto_exclude_symlinks`: Exclude symlinks from module directories.
- `directory_based`: Create a project in a directory-based format.
- `oauth_exchange_ssh_keys`: Exchange SSH keys via OAuth.

2. IDE integration settings:
- `idea_files_root, idea_jdk_version`: Settings for the IntelliJ IDEA project files.
- `external_content_root_modules`: Add external root content modules.

3. Test settings:
- `detect_leaks_in_pytest`: Detect leaks in pytest.
- `run_tests_size`: Determine the size of tests to be run.

4. Integration with build and caching systems:
- `build_threads`, `continue_on_fail`: Build process settings.
- `auto_clean_results_cache`, `build_cache`: Local cache settings for the build result.

5. Output settings:
- `mask_roots`, `output_style`: Settings controlling the output of information during build.

6. Platform or build configuration options:
- `build_type`, `target_platforms`: Settings determining the build type and target platforms.

7. Options for developers:
- `use_json_cache`, `validate_build_root_content`: Parameters for speeding up development and validating the build.

Each parameter in the file begins with a key, followed by the equals sign (`=`) and the parameter value. The value ​​can be a Boolean (`true` or `false`), string (in quotes), or list (in square brackets).

Comments (beginning with `#`) are used to describe parameters, suggested changes, or links to additional documentation. Some parameters have default values ​​and can be changed according to project requirements or developer preferences.

The configuration file can be customized to suit the specific needs of the project. You can enable or disable certain features to optimize the development and build process.

You can find a more detailed description of the parameters [here](ya_conf.md).

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
