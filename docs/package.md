## ya package

Команда пакетирования `ya package` позволяет собирать различные типы пакетов, описанных в специальных `JSON`-файлах, и публиковать в различных фиксированных конфигурациях.

### Синтаксис
Общий формат команды выглядит следующим образом:

`ya package [OPTION]... [PACKAGE DESCRIPTION FILE NAME(S)]...`

где:
- [OPTION] - это дополнительные флаги или ключи, которые модифицируют поведение выбранной подкоманды.
- [PACKAGE DESCRIPTION FILE NAME(S)] - Названия файлов описания пакета в формате `JSON` (package.json)

### Поддерживаемые форматы пакетов

Для пакетирования доступны следующие форматы, выбираемые ключом командной строки:

* `--tar` tar-архив (по умолчанию)
* `--debian` deb-пакет
* `--rpm` rpm-пакет
* `--docker` docker-образ
* `--wheel` Python wheel-пакет
* `--aar` aar - нативный пакет для Android
* `--npm` npm - пакет для Node.js


## Опции

### Опции пакетирования
```
    --no-cleanup        Do not clean the temporary directory
    --change-log=CHANGE_LOG
                        Change log text or path to the existing changelog file
    --publish-to=PUBLISH_TO
                        Publish package to the specified dist
    --strip             Strip binaries (only debug symbols: "strip -g")
    --full-strip        Strip binaries
    --no-compression    Don't compress tar archive (for --tar only)
    --create-dbg        Create separate package with debug info (works only in case of --strip or --full-strip)
    --key=KEY           The key to use for signing
    --debian            Build debian package
    --tar               Build tarball package
    --docker            Build docker
    --rpm               build rpm package
    --wheel             Build wheel package
    --wheel-repo-access-key=WHEEL_ACCESS_KEY_PATH
                        Path to access key for wheel repository
    --wheel-repo-secret-key=WHEEL_SECRET_KEY_PATH
                        Path to secret key for wheel repository
    --docker-registry=DOCKER_REGISTRY
                        Docker registry (default: registry.yandex.net)
    --docker-repository=DOCKER_REPOSITORY
                        Specify private repository (default: )
    --docker-save-image Save docker image to archive
    --docker-push       Save docker image to archive
    --docker-network=DOCKER_BUILD_NETWORK
                        --network parameter for `docker build` command
    --raw-package       Used with --tar to get package content without tarring
    --raw-package-path=RAW_PACKAGE_PATH
                        Custom path for --raw-package
    --codec=CODEC       Codec name for uc compression
    --codecs-list       Show available codecs for --uc
    --ignore-fail-tests Create package, no matter tests failed or not
    --new               Use new ya package json format
    --old               Use old ya package json format
    --not-sign-debian   Do not sign debian package
    --custom-version=CUSTOM_VERSION
                        Custom package version
    --debian-distribution=DEBIAN_DISTRIBUTION
                        Debian distribution (default: unstable)
    --arch-all          Use "Architecture: all" in debian
    --force-dupload     dupload --force
    -z=DEBIAN_COMPRESSION_LEVEL, --debian-compression=DEBIAN_COMPRESSION_LEVEL
                        deb-file compresson level (none, low, medium, high)
    -Z=DEBIAN_COMPRESSION_TYPE, --debian-compression-type=DEBIAN_COMPRESSION_TYPE
                        deb-file compression type used when building deb-file (allowed types: gzip, xz, bzip2, lzma, none)
    --tests-data-root=CUSTOM_TESTS_DATA_ROOT
                        Custom location for arcadia_tests_data dir, defaults to <source root>/../arcadia_tests_data
    --data-root=CUSTOM_DATA_ROOT
                        Custom location for data dir, defaults to <source root>/../data
    --upload            Upload created package to sandbox
    --dupload-max-attempts=DUPLOAD_MAX_ATTEMPTS
                        How many times try to run dupload if it fails (default: 1)
    --nanny-release=NANNY_RELEASE
                        Notify nanny about new release
    --dupload-no-mail   dupload --no-mail
    --overwrite-read-only-files
                        Overwrite read-only files in package
    --dump-arcadia-inputs=DUMP_INPUTS
                        Only dump inputs, do not build package
    --ensure-package-published
                        Ensure that package is available in the repository
```
### Основные опции
```
    -d                  Debug build
    -r                  Release build
    --sanitize=SANITIZE Sanitizer type(address, memory, thread, undefined, leak)
    --sanitizer-flag=SANITIZER_FLAGS
                        Additional flag for sanitizer
    --lto               Build with LTO
    --thinlto           Build with ThinLTO
    --sanitize-coverage=SANITIZE_COVERAGE
                        Enable sanitize coverage
    --afl               Use AFL instead of libFuzzer
    --musl              Build with musl-libc
    --pch               Build with Precompiled Headers
    --hardening         Build with hardening
    --race              Build Go projects with race detector
    --cuda=CUDA_PLATFORM
                        Cuda platform(optional, required, disabled) (default: optional)
    -j=BUILD_THREADS, --threads=BUILD_THREADS
                        Build threads count (default: 32)
    --checkout          Checkout missing dirs
    --report-config=REPORT_CONFIG_PATH
                        Set path to TestEnvironment report config
    -h, --help          Print help
    -v, --verbose       Be verbose
    --ttl=TTL           Resource TTL in days (pass 'inf' - to mark resource not removable) (default: 14)
```
### Опции запуска тестов"
```
    -t, --run-tests     Run tests (-t runs only SMALL tests, -tt runs SMALL and MEDIUM tests, -ttt runs SMALL, MEDIUM and FAT tests)
    -A, --run-all-tests Run test suites of all sizes
    --add-peerdirs-tests=PEERDIRS_TEST_TYPE
                        Peerdirs test types (none, gen, all) (default: none)
    --test-tool-bin=TEST_TOOL_BIN
                        Path to test_tool binary
    --test-tool3-bin=TEST_TOOL3_BIN
                        Path to test_tool3 binary
    --profile-test-tool=PROFILE_TEST_TOOL
                        Profile specified test_tool calls
```
### Расширенные опции 
```
    --build=BUILD_TYPE  Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://wiki.yandex-team.ru/yatool/build-types (default: release)
    --host-build-type=HOST_BUILD_TYPE
                        Host platform build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://wiki.yandex-team.ru/yatool/build-types (default: release)
    --host-platform=HOST_PLATFORM
                        Host platform
    --host-platform-flag=HOST_PLATFORM_FLAGS
                        Host platform flag
    --c-compiler=C_COMPILER
                        Specifies path to the custom compiler for the host and target platforms
    --cxx-compiler=CXX_COMPILER
                        Specifies path to the custom compiler for the host and target platforms
    --target-platform=TARGET_PLATFORMS
                        Target platform
    --target-platform-build-type=TARGET_PLATFORM_BUILD_TYPE
                        Set build type for the last target platform
    --target-platform-release
                        Set release build type for the last target platform
    --target-platform-debug
                        Set debug build type for the last target platform
    --target-platform-tests
                        Run tests for the last target platform
    --target-platform-test-size=TARGET_PLATFORM_TEST_SIZE
                        Run tests only with given size for the last target platform
    --target-platform-test-type=TARGET_PLATFORM_TEST_TYPE
                        Run tests only with given type for the last target platform
    --target-platform-regular-tests
                        Run only "boost_test exectest fuzz go_test gtest java junit py2test py3test pytest testng unittest" test types for the last target platform
    --target-platform-flag=TARGET_PLATFORM_FLAG
                        Set build flag for the last target platform
    --target-platform-c-compiler=TARGET_PLATFORM_COMPILER
                        Specifies path to the custom compiler for the last target platform
    --target-platform-cxx-compiler=TARGET_PLATFORM_COMPILER
                        Specifies path to the custom compiler for the last target platform
    --universal-binaries
                        Generate multiplatform binaries
    --lipo              Generate multiplatform binaries with lipo
    --rebuild           Rebuild all
    --strict-inputs     Enable strict mode
    --share-results     Share results with skynet
    --build-results-report=BUILD_RESULTS_REPORT_FILE
                        Dump build report to file in the --output-dir
    --build-results-report-tests-only
                        Report only test results in the report
    --build-report-type=BUILD_REPORT_TYPE
                        Build report type(canonical, human_readable) (default: canonical)
    --build-results-resource-id=BUILD_RESULTS_RESOURCE_ID
                        Id of sandbox resource id containing build results
    --use-links-in-report
                        Use links in report instead of local paths
    --report-skipped-suites
                        Report skipped suites
    --report-skipped-suites-only
                        Report only skipped suites
    --dump-raw-results  Dump raw build results to the output root
    --no-local-executor Use Popen instead of local executor
    --force-build-depends
                        Build by DEPENDS anyway
    --ignore-recurses   Do not build by RECURSES
    -S=CUSTOM_SOURCE_ROOT, --source-root=CUSTOM_SOURCE_ROOT
                        Custom source root (autodetected by default)
    -B=CUSTOM_BUILD_DIRECTORY, --build-dir=CUSTOM_BUILD_DIRECTORY
                        Custom build directory (autodetected by default)
    --html-display=HTML_DISPLAY
                        Alternative output in html format
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30GiB)
    --cache-size=CACHE_SIZE
                        Max cache size (default: 300GiB)
    -D=FLAGS            Set variables (name[=val], "yes" if val is omitted)
    --log-file=LOG_FILE Append verbose log into specified file
    --evlog-file=EVLOG_FILE
                        Dump event log into specified file
    --no-evlogs         Disable standard evlogs in YA_CACHE_DIR
    --evlog-dump-platform
                        Add platform in event message
    --keep-temps        Do not remove temporary build roots. Print test's working directory to the stderr (use --test-stderr to make sure it's printed at the test start)
    --dist              Run on distbuild
    --dump-distbuild-result=DUMP_DISTBUILD_RESULT
                        Dump result returned by distbuild (default: False)
    --build-time=BUILD_EXECUTION_TIME
                        Set maximum build execution time (in seconds)
    -E, --download-artifacts
                        Download build artifacts when using distributed build
    -G, --dump-graph    Dump full build graph to stdout
    --dump-json-graph   Dump full build graph as json to stdout
    -x=DEBUG_OPTIONS, --dev=DEBUG_OPTIONS
                        ymake debug options
    --vcs-file=VCS_FILE Provides VCS file
    --dump-files-path=DUMP_FILE_PATH
                        Put extra ymake dumps into specified directory
    --ymake-bin=YMAKE_BIN
                        Path to ymake binary
    --no-ymake-resource Do not use ymake binary as part of build commands
    --no-ya-bin-resource
                        Do not use ya-bin binary as part of build commands
    --cache-stat        Show cache statistics
    --gc                Remove all cache except uids from the current graph
    --gc-symlinks       Remove all symlink results except files from the current graph
    --symlinks-ttl=SYMLINKS_TTL
                        Results cache TTL (default: 168.0h)
    --yt-store          Use YT storage
    --yt-proxy=YT_PROXY YT storage proxy (default: hahn.yt.yandex.net)
    --yt-dir=YT_DIR     YT storage cypress directory pass (default: //home/devtools/cache)
    --yt-token=YT_TOKEN YT token
    --yt-token-path=YT_TOKEN_PATH
                        YT token path (default: /home/spreis/.yt/token)
    --yt-put            Upload to YT store
    --yt-create-tables  Create YT storage tables
    --yt-max-store-size=YT_MAX_CACHE_SIZE
                        YT storage max size
    --yt-store-filter=YT_CACHE_FILTER
                        YT store filter
    --yt-store-ttl=YT_STORE_TTL
                        YT store ttl in hours(0 for infinity) (default: 24)
    --yt-store-codec=YT_STORE_CODEC
                        YT store codec
    --yt-replace-result Build only targets that need to be uploaded to the YT store
    --yt-store-threads=YT_STORE_THREADS
                        YT store max threads (default: 3)
```
### Опции авторизации
```
    --ssh-key=SSH_KEYS  Path to private ssh key to exchange for OAuth token
    --token=OAUTH_TOKEN oAuth token
    --user=USERNAME     Custom user name for authorization
```

## Пример
`ya package <path to json description>  Create tarball package from json description`
