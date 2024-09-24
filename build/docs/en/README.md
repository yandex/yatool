## Basic concepts of the ya make build system

### General information

The `ya make` project build automation system was developed by **Yandex**.

`ya make` enables you to:

- Compile code in different languages and ensure transparent interaction between method calls.
- Manage dependencies.
- Run tests.
- Generate executables and packages in a single and consistent environment, regardless of the project complexity.

The system structure uses a declarative approach to describing builds, making it [similar](https://cmake.org/cmake/help/book/mastering-cmake/cmake/Help/guide/tutorial/index.html#id3) to `CMakeLists.txt` and therefore easier to grasp if you're familiar with `CMake` concepts.

The entire build configuration is described in `ya.make` files by calling macros with parameters.

The system supports more than 300 macros and 70 modules that you can customize using a special macro description language or plugins in Python.
This makes it possible to flexibly adapt the system to diverse project needs.

`ya make` is used to build C++, Python, Java, and Go projects with various test frameworks.
It integrates with various code generation technologies, such as `protobuf` and `ragel`.

The build system runs on Linux, Windows, and macOS. Cross-compilation enables you to build programs for many different platforms, including mobile devices, on a single local machine.

Strict dependency and data management eliminates the influence of external and changing resources, ensuring that the build results are stable and identical each time the build is run.

### Build system commands

Build commands are integrated into the [universal ya utility](command.md), which offers a wide range of functions and command-line parameters for tailoring the build process to individual project requirements:

- [Main build tool](ya_make.md): `ya make`.
- [Building with further packaging](package.md): `ya package`.
- [Obtaining various information from the build system](ya_dump.md): `ya dump`.
- [Generating and modifying a build description](project.md): `ya project`.
- [Analyzing the build time](analyze_make.md): `ya analyze-make`.
- [Running various tools](tool.md): `ya tool`.
- [Correcting code style](style.md): `ya style`.
- [Testing software projects](test.md): `ya test`.
- [Generating a configuration file](gen-config.md): `ya gen-config`.
- [Clearing temporary files (cache)](gc.md): `ya gc`.

Additional information:

- [Describing macros for ya.make in a language](coreconf.md): `core.conf`.
