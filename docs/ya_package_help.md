```
Build package using json package description in the release build type by default.

Usage:
  ya package [OPTION]... [Package description file name(s)]...

Examples:
  ya package <path to json description>  Create tarball package from json description

Options:
  Ya operation control
    -j=BUILD_THREADS, --threads=BUILD_THREADS
                        Build threads count (default: 2)
    --rebuild           Rebuild all
    -h, --help          Print help. Use -hh for more options and -hhh for even more.
    Advanced options
    --link-threads=LINK_THREADS
                        Link threads count (default: 0)
    --sandboxing        Run command in isolated source root
    --no-clonefile      Disable clonefile option
    Expert options
    --no-content-uids   Disable additional cache based on content-only dynamic uids
    --keep-temps        Do not remove temporary build roots. Print test's working directory to the stderr (use --test-stderr to make sure it's printed at the test start)
    --force-use-copy-instead-hardlink-macos-arm64
                        Use copy instead hardlink when clonefile is unavailable
    -B=CUSTOM_BUILD_DIRECTORY, --build-dir=CUSTOM_BUILD_DIRECTORY
                        Custom build directory (autodetected by default)
    --fetcher-params=FETCHER_PARAMS_STR
                        Fetchers priorities and params
   Build output
    -o=OUTPUT_ROOT, --output=OUTPUT_ROOT
                        Directory with build results
    --force-build-depends
                        Build by DEPENDS anyway
    -R, --ignore-recurses
                        Do not build by RECURSES
    Expert options
    --no-output-for=SUPPRESS_OUTPUTS
                        Do not symlink/copy output for files with given suffix, they may still be save in cache as result
  Printing
    -v, --verbose       Be verbose
    Advanced options
    --log-file=LOG_FILE Append verbose log into specified file
    Expert options
    --html-display=HTML_DISPLAY
                        Alternative output in html format
  Platform/build configuration
    -d                  Debug build
    -r                  Release build
    --build=BUILD_TYPE  Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) 
    --sanitize=SANITIZE Sanitizer type(address, memory, thread, undefined, leak)
    --race              Build Go projects with race detector
    --host-platform-flag=HOST_PLATFORM_FLAGS
                        Host platform flag
    --target-platform=TARGET_PLATFORMS
                        Target platform
    --target-platform-flag=TARGET_PLATFORM_FLAG
                        Set build flag for the last target platform
    -D=FLAGS            Set variables (name[=val], "yes" if val is omitted)
    Advanced options
    --sanitizer-flag=SANITIZER_FLAGS
                        Additional flag for sanitizer
    --lto               Build with LTO
    --thinlto           Build with ThinLTO
    --afl               Use AFL instead of libFuzzer
    --musl              Build with musl-libc
    --hardening         Build with hardening
    --cuda=CUDA_PLATFORM
                        Cuda platform(optional, required, disabled) (default: optional)
    --host-build-type=HOST_BUILD_TYPE
                        Host platform build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) (default: release)
    --host-platform=HOST_PLATFORM
                        Host platform
    --c-compiler=C_COMPILER
                        Specifies path to the custom compiler for the host and target platforms
    --cxx-compiler=CXX_COMPILER
                        Specifies path to the custom compiler for the host and target platforms
    --pgo-add           Create PGO profile
    --pgo-use=PGO_USER_PATH
                        PGO profiles path
    Expert options
    --sanitize-coverage=SANITIZE_COVERAGE
                        Enable sanitize coverage
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
                        Run only "benchmark boost_test exectest fuzz g_benchmark go_bench go_test gtest hermione java jest py2test py3test pytest unittest" test types for the last target platform
    --target-platform-c-compiler=TARGET_PLATFORM_COMPILER
                        Specifies path to the custom compiler for the last target platform
    --target-platform-cxx-compiler=TARGET_PLATFORM_COMPILER
                        Specifies path to the custom compiler for the last target platform
    --target-platform-target=TARGET_PLATFORM_TARGET
                        Source root relative build targets for the last target platform
    --target-platform-ignore-recurses
                        Do not build by RECURSES
  Local cache
    --cache-stat        Show cache statistics
    --gc                Remove all cache except uids from the current graph
    --gc-symlinks       Remove all symlink results except files from the current graph
    Advanced options
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30.0GiB)
    --symlinks-ttl=SYMLINKS_TTL
                        Results cache TTL (default: 168.0h)
    --cache-size=CACHE_SIZE
                        Max cache size (default: 300.0GiB)
    --cache-codec=CACHE_CODEC
                        Cache codec
    --auto-clean=AUTO_CLEAN_RESULTS_CACHE
                        Auto clean results cache (default: True)
 Feature flags
    Expert options
    --no-local-executor Use Popen instead of local executor
    --dir-outputs-test-mode
                        Enable new dir outputs features
    --disable-runner-dir-outputs
                        Disable dir_outputs support in runner
  Testing
   Run tests
    -t, --run-tests     Run tests (-t runs only SMALL tests, -tt runs SMALL and MEDIUM tests, -ttt runs SMALL, MEDIUM and FAT tests)
    -A, --run-all-tests Run test suites of all sizes
    Advanced options
    --test-threads=TEST_THREADS
                        Restriction on concurrent tests (no limit by default) (default: 0)
    --fail-fast         Fail after the first test failure
    Expert options
    --add-peerdirs-tests=PEERDIRS_TEST_TYPE
                        Peerdirs test types (none, gen, all) (default: none)
    --split-factor=TESTING_SPLIT_FACTOR
                        Redefines SPLIT_FACTOR(X) (default: 0)
    --test-prepare      Don't run tests, just prepare tests' dependencies and environment
    --no-src-changes    Don't change source code
   Filtering
    -X, --last-failed-tests
                        Restart tests which failed in last run for chosen target
    -F=TESTS_FILTERS, --test-filter=TESTS_FILTERS
                        Run only test that matches <tests-filter>. Asterics '*' can be used in filter to match test subsets. Chunks can be filtered as well using pattern that matches '[*] chunk'
    --style             Run only style tests and implies --strip-skipped-test-deps (classpath.clash clang_tidy eslint gofmt govet java.style ktlint py2_flake8 flake8 black ruff). Opposite of the --regular-tests
    --regular-tests     Run only regular tests (benchmark boost_test exectest fuzz g_benchmark go_bench go_test gtest hermione java jest py2test py3test pytest unittest). Opposite of the --style
    Advanced options
    --test-size=TEST_SIZE_FILTERS
                        Run only specified set of tests
    --test-type=TEST_TYPE_FILTERS
                        Run only specified types of tests
    --test-tag=TEST_TAGS_FILTER
                        Run tests that have specified tag
    --test-filename=TEST_FILES_FILTER
                        Run only tests with specified filenames (pytest and hermione only)
    --test-size-timeout=TEST_SIZE_TIMEOUTS
                        Set test timeout for each size (small=60, medium=600, large=3600)
   Debugging
    --pdb               Start pdb on errors
    --gdb               Run c++ unittests in gdb
    --dlv               Run go unittests in dlv
    --test-debug        Test debug mode (prints test pid after launch and implies --test-threads=1 --test-disable-timeout --retest --test-stderr)
    Advanced options
    --dlv-args=DLV_ARGS Dlv extra command line options. Has no effect unless --dlv is also specified
    --test-retries=TESTS_RETRIES
                        Run every test specified number of times (default: 1)
    --test-stderr       Output test stderr to console online
    --test-stdout       Output test stdout to console online
    --test-disable-timeout
                        Turn off timeout for tests (only for local runs, incompatible with --cache-tests, --dist)
    --test-binary-args=TEST_BINARY_ARGS
                        Throw args to test binary
    --dump-test-environment
                        List contents of test's build root in a tree-like format to the run_test.log file right before executing the test wrapper
    Expert options
    --no-random-ports   Use requested ports
    --disable-test-graceful-shutdown
                        Test node will be killed immediately after the timeout
   Runtime environment
    --test-param=TEST_PARAMS
                        Arbitrary parameters to be passed to tests (name=val)
    --autocheck-mode    Run tests locally with autocheck restrictions (implies --private-ram-drive and --private-net-ns)
    Advanced options
    --private-ram-drive Creates a private ram drive for all test nodes requesting one
    --private-net-ns    Creates a private network namespace with localhost support
   Test uid calculation
    --cache-tests       Use cache for tests
    --retest            No cache for tests
   Test dependencies
    -b, --build-all     Build targets that are not required to run tests, but are reachable with RECURSE's
    Expert options
    --strip-skipped-test-deps
                        Don't build skipped test's dependencies
    --build-only-test-deps
                        Build only targets required for requested tests
    --strip-idle-build-results
                        Remove all result nodes (including build nodes) that are not required for tests run
    --no-strip-idle-build-results
                        Don't remove all result nodes (including build nodes) that are not required for tests run
   File reports
    --junit=JUNIT_PATH  Path to junit report to be generated
    Advanced options
    --allure=ALLURE_REPORT (deprecated)
                        Path to allure report to be generated
   Pytest specific
    --test-log-level=TEST_LOG_LEVEL
                        Specifies logging level for output test logs ("critical", "error", "warning", "info", "debug")
    Advanced options
    --test-traceback=TEST_TRACEBACK
                        Test traceback style for pytests ("long", "short", "line", "native", "no") (default: short)
    --profile-pytest    Profile pytest (dumps cProfile to the stderr and generates 'pytest.profile.dot' using gprof2dot in the testing_out_stuff directory)
    --pytest-args=PYTEST_ARGS
                        Pytest extra command line options (default: [])
   JUnit specific
    Advanced options
    --junit-args=JUNIT_ARGS
                        JUnit extra command line options
  Packaging
   Python wheel
    --wheel-platform=WHEEL_PLATFORM
                        Set wheel package platform (default: )
  Advanced
    Advanced options
    --strict-inputs (deprecated)
                        Enable strict mode
  Upload
    Expert options
    --ttl=TTL           Resource TTL in days (pass 'inf' - to mark resource not removable) (default: 14)
  
  Authorization
    --ssh-key=SSH_KEYS  Path to private ssh key to exchange for OAuth token
    --token=OAUTH_TOKEN oAuth token
    --user=USERNAME     Custom user name for authorization (default: user)
    Advanced options
    --ssh-key=SSH_KEYS  Path to private ssh key to exchange for OAuth token
```
