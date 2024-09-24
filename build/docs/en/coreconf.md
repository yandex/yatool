## Setting up and extending the build system

`ymake.core.conf` is the main configuration file of the `ymake` build system and plays a key role in managing the project build process.

It describes macros, modules, multi-modules, global variables, system headers, and other build configuration aspects.

The file uses a declarative DSL (Domain-Specific Language) and enables you to flexibly configure rules for different platforms, set build command templates, define their properties and dependencies, and manage command inputs and outputs by linking them into a graph.

The configuration file `ymake.core.conf` helps:
- Set rules for configuring variables for different platforms and parameters.
- Describe macros, their parameters, and behavior.
- Determine templates of build commands with their additional properties, such as the display name and resources required.
- Specify input and output data for linking commands into a graph.
- Apply macros depending on file extensions.
- Set module properties and link them to commands and to each other.
- Set rules for modules: their properties and commands depend on the platform and parameters.
- Determine multi-module variants and their dependencies.

### Rules for configuring variables
When describing a module, you can make some variables global. To do this, use the `.GLOBAL` module property. To refer to a global variable, add the `_GLOBAL` suffix to its name.

Example:
```python translate=no
module MY_MOD {
    .CMD=builder --list $MY_VAR_GLOBAL --extra $ANOTHER_VAR_GLOBAL
    .GLOBAL=MY_VAR ANOTHER_VAR
    ...

    SET(MY_VAR_GLOBAL $MODDIR)
    SET(ANOTHER_VAR_GLOBAL VERBOSE)
}
```
As you configure a module, global variables behave like local ones. You can only get the values set in the current module.
However, at the rendering stage, a global variable collects all values set in all the modules that the current module depends on. The values are collected in the reverse order of dependency graph traversal: from root to leaves.
This means that values from root nodes will be at the end of the list, and values from leaf nodes will be at the beginning.

**Dependency example:**
```text translate=no

  ┌─────► lib-mid1──────┐
  │                     ▼
main                  lib-leaf
  │                     ▲
  └─────► lib-mid2──────┘

```
For the `main` end module, build commands will be as follows:

```bash translate=no
# lib-leaf
builder --list lib-leaf --extra VERBOSE

# lib-mid1
builder --list lib-leaf lib-mid1 --extra VERBOSE VERBOSE

# lib-mid2
builder --list lib-leaf lib-mid2 --extra VERBOSE VERBOSE

# main
builder --list lib-leaf lib-mid1 lib-mid2 main --extra VERBOSE VERBOSE VERBOSE VERBOSE
```
Global variables are deployed at the rendering stage. This means that the `input` and `output` modifiers on global variables won't add additional dependent edges to the graph. However, you can use filters and formatters (for example, `suf`, `pre`, `join`, or `ext`) to process values.

Usage example:
```bash translate=no
${pre=arc/;suf=/bin:MY_VAR_GLOBAL}
```

The project also has predefined global variables, such as `PEERS`.

Global resources are subject to special rules. If a global resource is defined in multiple modules, all definitions must be identical.
Non-matching values result in a configuration error.

Example of a global resource:
```text translate=no
{resource:RESOURCES_URIS_GLOBAL}
```

Only one value is used in the rendering of a global resource, further declarations are ignored.

Global variables and resources facilitate the propagation of parameters and settings between modules, making it easier to manage complex project dependencies and configurations.

### Underlying principles of writing macros.

Macros are used to create reusable code blocks that perform specific tasks, such as configuring modules, setting variables, disabling features, and more. 

Macros can be roughly classified into two groups:

1. Macros that set properties: These macros call other macros and form variables that will be used in the commands of other macros or the module. In addition, they can add dependencies or set module properties.
2. Macros that describe commands: These macros describe the commands to be executed during the build process. A macro forms a command embedded in the build graph of its module by linking its outputs to the inputs of other commands (consumers) and, possibly, its inputs to the outputs of other commands (sources). It is these macros that form the build graph executed during the build process.

A macro is defined by the `macro` keyword followed by the macro name. Macros can also accept parameters, which are specified in brackets right after its name. When calling a macro, the parameters must be space separated.

Example:
```plaintext translate=no
macro MY_MACRO(PARAM1, PARAM2) {
   // Код макроса
 }
```
Parameters are divided into three types: strings, lists of strings, and booleans. They can be named or unnamed.

**Unnamed parameters** are involved in the call with their value:
- Scalar: A string (one word). Can be optional.
In the documentation, it is denoted as `<value>` (optional) or `[<value>]` optional.
Note that in this case the value is denoted by a single word that isn't fully capitalized.

Examples:
```plaintext translate=no
  ### @usage: PY_NAMESPACE(<name.space>)
  PY_NAMESPACE(my.namespace)
  ### @usage: KV(<Key> <Value>)
  KV(the_key, the_value) # Key=the_key, Value=the_value
```
- Loose-list: A list of words not related to other parameters.
Denoted as `Value...` in the documentation.

 Examples:
```plaintext translate=no
  ### @usage: ALL_SRCS([GLOBAL] Filenames...)
  ALL_SRCS(x.cpp GLOBAL y.cpp)       # Filenames=x.cpp y.cpp, GLOBAL=True
  
  ### @usage: SRC(<File>, Flags...)
  SRCS(x.cpp -Wno-error -std=c++17)  # Flags=-Wno-error -std=c++17
  SRCS(y.cpp)                        # Flags=<empty>
```
**Named parameters** are denoted by a name and a possible value. They are written in [SCREAMING_SNAKE_CASE](https://ru.wikipedia.org/wiki/Snake_case):
- Booleans: Defined in the macro description by availability in the list. In the documentation, this parameter is described as `[PARM_NAME]`. Note that in this case the parameter name is fully capitalized, without angle brackets.

Example:
 ```plaintext translate=no
  ### @usage: ALL_SRCS([GLOBAL] Filenames...)
  ALL_SRCS(GLOBAL x.cpp y.cpp)  # GLOBAL is True
  ALL_SRCS(a.cpp b.cpp)         # GLOBAL is False
 ```
- Scalar: A string (one word) after the parameter name. Can be optional. In the documentation, this parameter looks like `NAME <value>` or `[NAME <value>]`. The parameter name is written in all caps, and the value is a single word in angle brackets.

Example:
 ```plaintext translate=no
  ### @usage: COMPILE_LUA(Src, [NAME <import_name>])
  .
  COMPILE_LUA(func.lua NAME my.func)  # NAME=my.func
  COMPILE_LUA(the_func.lua)           # NAME=<default>
  ```
- List: A list of words after the parameter name and before the next name or the closing bracket. It can occur multiple times. In the documentation, this parameter looks like `NAME <list of values>` or `[NAME <list of values>]`. The parameter name is written in all caps, and the value is written as multiple words in angle brackets. 

Examples:
 ```plaintext translate=no
  ### @usage: FROM_ARCHIVE(Src [RENAME <resource files>] OUT <output files> [PREFIX <prefix>] [EXECUTABLE])
  # Src    - обязательный неименованный скалярный
  # RENAME - опциональный именованный списочный
  # OUT    - обязательный именованный списочный
  # PREFIX - опциональный именованный скалярный
  # EXECUTABLE - булев
  .
  FROM_ARCHIVE(
      resource.tar.gz
      PREFIX y
      RENAME y/a.txt OUT y/1.txt
      RENAME y/b/c.txt OUT y/2.txt
      RENAME RESOURCE OUT 3.tar.gz
      OUT y/d.txt
  )
  # Src=resource.tar.gz 
  # PREFIX=y
  # RENAME=y/a.txt y/b/c.txt RESOURCE  ## RESOURCE в данном случае значение, а не имя параметра
  # OUT=y/1.txt y/2.txt 3.tar.gz y/d.txt
  ```
You can set macro conditions to perform actions depending on the values of variables. These conditions are defined by the `when` keyword and checked for truthiness.

Rules (triggers) set the values of variables based on other variables. Conditional expressions in the configuration language aren't executed in the sequence in which they are written, but are activated when the value of the corresponding condition is changed. They operate declaratively rather than imperatively, so the `when/elsewhen/otherwise` syntax is used instead of the `if/then/else` construct.

Example:
```plaintext translate=no
   macro CONDITIONAL_SET(CONDITION) {
       when ($CONDITION == "yes") {
           SET(VAR "value1")
       }
       otherwise {
           SET(VAR "value2")
       }
   }
```
You can disable macro features (functions or dependencies) using the `DISABLE` function and enable them with the `ENABLE` function.

Example:
   ```plaintext translate=no
### @usage: WERROR()
### Consider warnings as errors in the current module.
### In the bright future will be removed, since WERROR is the default.
### Priorities: NO_COMPILER_WARNINGS > NO_WERROR > WERROR_MODE > WERROR.
macro WERROR() {
    ENABLE(WERROR)
}

### @usage: NO_WERROR()
### Override WERROR() behavior
### Priorities: NO_COMPILER_WARNINGS > NO_WERROR > WERROR_MODE > WERROR.
macro NO_WERROR() {
    DISABLE(WERROR)
}
```
Macros can use global variables and attributes that affect the behavior of the entire project.

### Underlying principles for writing modules.

A module sets the command build rules and a number of additional properties:
- What forms the module name.
- What are the automatic inputs of its commands.
- How is it linked to other modules.
- Which macros are allowed in its build description and which aren't.
- Renaming macros for modular polymorphism.

Modules define various project components, such as libraries, executables, tests, and others. 

A module is defined by the `module` keyword and the module name. It can inherit properties and behavior from another module specified after the colon. This prevents duplication of common properties between modules. 

For example, modules `PROGRAM` and `DLL` are inherited from module `_LINK_UNIT`, which sets common properties for linking libraries.

Child properties can either override parent properties or merge with them, depending on the specific property. Macros of the parent module will be invoked before those of the child module.

Example:
```plaintext translate=no
module MY_LIBRARY: _LIBRARY {
       // Код модуля
}
```
Within the module, you can set variables using the built-in `SET` macro and call other functions to perform certain actions. Modules may contain conditions that depend on the values of variables. A module may have local variables that can change the behavior of both its own command and other commands associated with its build. Conditional module behavior rules can be defined based on variables as well.

Example:
```plaintext translate=no
module MY_LIBRARY: _LIBRARY {
  when ($CONDITION == "yes") {
       SET(MODULE_TYPE LIBRARY)
       SET(MODULE_LANG CPP)
   }
}
```
A module enables you to set various attributes that define its functionality and properties. 
These attributes can include global variables and other parameters that affect the behavior and performance of the entire project.

Example:
```plaintext translate=no
module MY_LIBRARY: _LIBRARY {
    .GLOBAL=_AARS _PROGUARD_RULES
    .SEM=CPP_LIBRARY_SEM
    .DEFAULT_NAME_GENERATOR=ThreeDirNames
    SET(MODULE_TYPE LIBRARY)
    SET(MODULE_LANG CPP)
}
```
The list of available macros and modules is constantly growing. 
To keep it up to date, the documentation containing information about all currently available macros and modules is automatically generated based on comments and descriptions.

### Plugins

The build system core allows expanding the functionality using `Python` and `C++` plugins. Although code written in `Python` is processed slower compared to the built-in `DSL`, it helps bypass certain restrictions of the built-in language.

You can use `Python` to:
- Create macros that set properties and describe commands.
- Parse dependencies.

The code for `Python` plugins is located at `build/plugins`. Macro implementations are `Python` functions formatted as `on<macro_name>`. For example, an implementation of the `PY_SRCS` macro would be represented as the `onpy_srcs` function.

`Python` plugins are loaded at build time and interpreted by the build system core with the embedded `Python` interpreter. This means that any changes to plugins, just like in the configuration, don't require a build system release.

The build system also supports `C++` plugins. They are embedded in the build system core, but are implemented not in the core logic, but as separate entities called via the plugin interface.

The advantages of `C++` plugins are high execution speed and the ability to add commands to the graph.

The disadvantage is that `C++` plugins require a release cycle. They are embedded in the core and released along with it. When such plugins are developed and tested locally, you need to rebuild the build system core (`devtools/ymake/bin` program).

For example, the `RESOURCE` macro is implemented using a `C++` plugin.

Plugins are used in macros that can accept parameters to configure the behavior. When using plugins, consider their dependencies on other modules or libraries that must be specified using `PEERDIR`. You also need to specify the correct path to the plugin and its name in the macro to ensure their proper use by the build system.

### Scripts.

Scripts in the build system are a key element for implementing project-specific build commands.
They can come in handy in cases where the basic build toolkit is insufficient and provide additional options for adapting and expanding the build process functionality.

Scripts are located at `build/scripts`. 
This is a centralized storage space with all the scripts you need to manage the project build.

Scripts are written in `Python`.

Scripts in the build system are called via macros. 
Macros describe the commands to be executed at build time and help embed them in module build graphs. This is achieved by linking the outputs of some commands to the inputs of other commands (consumers) and, possibly, inputs of some commands to the outputs of other commands (sources).

Scripts support transmission of various parameters, meaning you can tailor commands to your specific build needs. Parameters are transmitted via macros and may include file paths, compilation options, flags, and other important data.

**Example of a script call macro**

```make translate=no
macro CUSTOM_BUILD_STEP(Param1, Param2) {
    .CMD=YMAKE_PYTHON ${input:“build/scripts/my_build_script.py”} ${input:Param1} ${output:Param2}
}
```
- {input:"build/scripts/my_build_script.py"} is the path to your script.
- {input:Param1} and {output:Param2} are the parameters transmitted to the script.

### UnkStatm error

The `UnkStatm` error occurs when an unknown macro is called in the `ya.make` project build file. Using macros that start with underscore in build description files is not allowed. Macros that you can use in the build are described in `ymake.core.conf` or plugins at `build/plugins`.