## ya style

The `ya style` command is used to correct the style of C++, Python, or Go code.

Files can be transferred directly or processed recursively in directories.

### Syntax

`ya style [OPTION]... [FILE OR DIR]...`

### Style options
* `--reindent`: Align rows by the minimum indentation.
* `-h`, `--help`: Provide the help.

When filtering files by programming language, you can use keys to specify languages.

These keys can be combined for more precise results.

For example, to filter Python, C++, or Go files, you can use the following keys:
* `--py`: For Python.
* `--cpp`: For C++.
* `--go`: For Go.
* `--yamake`: For `ya.make` files.
* `--cuda`: Process only `cuda` files.
* `--all`: Run all checks: `py`, `cpp`, `go`, `yamake`, and `cuda`.

### Linters by language

#### C++
The `clang-format` utility is used to format C++ code.

#### Python
The command supports code block protection using `# fmt: on/off`.
Formatting is done using the `black` linter.
If you prefer the `Ruff` formatting style, use the `--ruff` option that enables you to format Python files with the `Ruff` tool instead of the standard `black`.

#### Go
The `yoimports` utility is used as a linter for Go, so when you run `ya style`, only `imports` are updated.

For robust linting, use `yolint`.

### Examples of using ya style
```bash
ya style file.cpp  # update the style of file.cpp
ya style           # update the style of text from <stdin>, redirect the result to <stdout>
ya style .         # update the style of all files in this directory recursively
ya style folder/   # update the style of all files in all subfolders recursively
ya style . --py    # update the style of all python files in this directory recursively
```

### Java
There is a `ya jstyle` command for `java`.

Format Java code.

`ya jstyle [OPTION]... [TARGET]...`

The `ya jstyle` command is used to format Java code and applies the same options as `ya style`.
It is based on the JetBrains IntelliJ IDEA formatter.

#### Example
```bash
  ya jstyle path/to/dir  # update the style of all supported files in this directory
```
