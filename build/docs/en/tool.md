## ya tool

`ya tool` is a utility for running various tools provided by the `ya` package.

### How to use

To run a tool provided by the `ya` utility, use the following syntax:

`ya tool <subcommand> [parameters]`

### List of available tools

To get the list of available tools, run the command: `ya tool`

It will display a list of tools that can be executed with the utility:

- `atop`: System process monitoring utility.
- `black`: Python code formatter (only for Python 3).
- `buf`: Linter and change detector for `Protobuf`.
- `c++`: Running the C++ compiler.
- `c++filt`: Running `c++filt` to convert encoded symbols into a readable C++ format.
- `cc`: Running the C compiler.
- `clang-format`: Running the `Clang-Format` source code formatter.
- `clang-rename`: Running the `Clang-Rename` refactoring tool.
- `gcov`: Running the `gcov` test coverage program.
- `gdb`: Running `GNU Debugger gdb`.
- `gdbnew`: Running `gdb` for Ubuntu 16.04 or higher.
- `go`: Running Go tools.
- `gofmt`: Running the `gofmt` Go code formatter.
- `llvm-cov`: Running the `LLVM` coverage utility.
- `llvm-profdata`: Running the `LLVM` profile data utility.
- `llvm-symbolizer`: Running the `LLVM` symbolizer utility.
- `nm`: Running the nm utility for displaying symbols from object files.
- `objcopy`: Running the `objcopy` utility for copying and transforming binary files.
- `strip`: Running the `strip` utility for removing symbols from binary files.

### ya tool parameters

To get detailed information about the parameters and arguments supported by each tool, use the `-h` option with a specific subcommand: `ya tool <subcommand> -h`.

**Example**
```bash translate=no
ya tool buf -h
```
This command will provide a detailed description of how to use the tool, its parameters, and examples.
```bash translate=no
Usage:
  buf [flags]
  buf [command]

Available Commands:
  beta            Beta commands. Unstable and will likely change.
  check           Run lint or breaking change checks.
  generate        Generate stubs for protoc plugins using a template.
  help            Help about any command
  image           Work with Images and FileDescriptorSets.
  ls-files        List all Protobuf files for the input location.
  protoc          High-performance protoc replacement.

Flags:
  -h, --help                help for buf
      --log-format string   The log format [text,color,json]. (default "color")
      --log-level string    The log level [debug,info,warn,error]. (default "info")
      --timeout duration    The duration until timing out. (default 2m0s)
  -v, --version             version for buf

Use "buf [command] --help" for more information about a command.
```
### Additional parameters

* `--ya-help` provides information about the use of a particular tool launched via `ya tool`.
Command syntax: `ya tool <subcommand> --ya-help`.
* `--print-path` is used to output the path to the executable file of a particular tool launched via the `ya tool` utility. The parameter enables the user to know where exactly the tool is located in the file system. For example, to get the path to the executable file of the `clang-format` tool, run the following command: `ya tool clang-format --print-path`.
* `--force-update` is used to check for updates and to update a particular tool launched via the `ya tool` utility. For example, to force an update of the `go` tool and then run it, use the following command: `ya tool go --force-update`.
