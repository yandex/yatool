# Yatool

Yatool is a cross-platform distribution, building, testing, and debugging toolkit focused on monorepositories.

All utilities are available for use through a single entry point `ya`.
The main handler in `ya` utility is `make`, which is a high-level universal build system.

## ya make

`ya make` build system can be described as

- **Completely static**.
  All dependencies are analyzed in advance and changes are recorded in the command graph.
  Based on the analysis, each command receives a unique UID, which fixes its result on a given state of input data and dependencies.
  The immutability of the UID indicates the immutability of its result and therefore serves as a key in the results cache, and is also used when analyzing changes to exclude a command from execution.

- **Universal and high-level**.
  The description of the build system is done at the level of modules, macros and dependencies between modules.
  Our build system hides a lot.
  It itself builds inter-file dependencies, both direct (`a.cpp` includes `b.h`) and induced by generation (if `x.proto` imports `y.proto`, then `x.pb.h` will include `y.pb.h`), allowing developers to avoid wasting time specifying highly granular file dependencies.
  These dependencies are internally mapped to commands: the compilation command `a.cpp` will be restarted when `b.h` is changed, and the command change in `y.proto` will entail not only a regeneration for `x.proto`, but also a recompilation of `z.cpp`, which includes `x.pb.h`.
  It itself builds file processing chains - including the w.proto file in the `GO_LIBRARY()` module will entail the generation of `.pb.go` from it and the further translation of this file as part of the package.

- **Declarative**, mostly.
  In the assembly description, most of the structures record the properties of modules and commands and the connections between them. However, some of the constructions are performed sequentially: setting and calculating local variables, conditional constructions - this is something that depends on the order in which it is written in the `ya.make` file.

## Warning - bumpy road ahead

`ya make` has been used at Yandex for more than 10 years and successfully meets all challenges within the company, coping with its tasks against the background of the explosive growth of the monorepository and projects in it.
Developers focus on developing products rather than overcoming complexities in building and testing projects.
However, such experience is strongly integrated into the internal ecosystem and is difficult to alienate.
As part of future releases, we want to provide similar experience for the development of open source products.
At this moment `ya` does not have a stable release and might not provide flawless and integrated experience for external users.
Work in progress, stay tuned.

## License
Yatool is licensed under the [Apache-2](LICENSE).

## Building

You can use `ya` to build itself. Get the source codes and just run the command:

```(bash)
./ya make
```

You can also build the first generation of build utilities without using `ya` using bootstrap.
For more details see [bootstrap guide](devtools/ya/bootstrap/README.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for instructions to contribute.

## Documentation

Documentation in [Russian](build/docs/ru/README.md).

Documentation in English is on the way.

[Project examples](devtools/examples/tutorials/README.md).
