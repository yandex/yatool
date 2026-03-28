## Basic build tools

The main build tool is the `ya make` command.

## Command line (format, options)

Build commands are integrated into the universal `ya` utility. The utility offers a wide range of functions and command line parameters that you can use to adapt the build process to different project requirements.
Running `ya` without parameters displays help:
```
Yet another build tool.

Usage: ya [--precise] [--profile] [--error-file ERROR_FILE] [--keep-tmp]
          [--no-logs] [--no-report] [--no-tmp-dir] [--print-path] [--version]
          [-v]

Options:
  --precise             show precise timings in log
  --profile             run python profiler for ya binary
  --error-file ERROR_FILE
  --keep-tmp
  --no-logs
  --no-report
  --no-tmp-dir
  --print-path
  --version
  -v, --verbose-level

Available subcommands:
  dump            Repository related information
  gc              Collect garbage
  gen-config      Generate default ya config
  ide             Generate project for IDE
  java            Java build helpers
  make            Build and run tests
                  To see more help use -hh/-hhh
  package         Build package using json package description in the release build type by default.
  style           Run styler
  test            Build and run all tests
                  ya test is alias for ya make -A
  tool            Execute specific tool
Examples:
  ya test -tt         Build and run small and medium tests
  ya test -t          Build and run small tests only
  ya dump debug       Show all items
  ya test             Build and run all tests
  ya dump debug last  Upload last debug item
```
### General use

ya [options] [subcommands]

Main options:
- --precise: Displays the log's exact timestamps.
- --profile: Runs a Python profiler for the ya binary.
- --error-file ERROR_FILE: Specifies the path to the file where error messages will be sent.
- --keep-tmp: Saves temporary files after the build is completed.
- --no-logs: Disables log output.
- --no-report: Disables reports.
- --no-tmp-dir: Prevents the creation of a temporary directory during execution.
- --print-path: Outputs the path to the ya executable.
- --version: Displays information about the utility version.
- -v, --verbose-level: Adjusts the verbosity level of messages.

Commands:
- dump: Display information associated with the repository.
- gc: Performs garbage collection.
- gen-config: Generates default configuration for ya.
- ide: Generates an integrated development environment project.
- java: Auxiliary functions for building Java projects.
- make: Executes the build process and runs tests.
- package: Creates a package using the JSON package description.
- style: Executes utilities to check code style.
- test: Compiles and runs all tests (this is an alias for "ya make -A").
- tool: Executes a specific tool.

For more information on any command and its parameters, add the -h, -hh, or -hhh options after the command. This will show you an extended description and use cases.

Examples:
- Run only small and medium tests:
   ```ya test -tt```
- Run only small tests:
   ```ya test -t```
- Show all elements using "dump":
   ```ya dump debug```
- Run all tests:
   ```ya test```
- Load the last debug element:
   ```ya dump debug last```

`ya` is a platform-agnostic utility that can run on most operating systems, including Linux, macOS, and Windows.
To run `ya` in Windows without specifying the full path, you can add it to the PATH environment variable.
For Linux or Bash, you can use the following command:
```
echo "alias ya='~/yatool/ya'" >> ~/.bashrc
source ~/.bashrc
```
`ya` uses configuration files in `TOML` format, which you need to place in the project root or in a special directory.

You can learn more about configuration files [here](gen-config.md "Configuration for ya").
