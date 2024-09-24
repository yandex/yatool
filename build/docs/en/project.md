## ya project

The `ya project` command is used to create or modify the `ya.make` project file.

`ya project <subcommand>`

### Commands
- `create`: [Creates a new project](#create).
- `update`: [Updates a standard project](#update).
- `fix-peerdirs`: [Adds missing and removes unused `peerdirs` in the project](#fix-peerdirs).
- `macro`: [Adds or removes a macro in the `ya.make` file](#macro).

### create

The `ya project create` command is used to create a standard project. It can create and modify the `ya.make` build description files, as well as any other files, including source code and documentation.

#### Syntax
`ya project create <project-type> [dest_path] [options]`

#### Parameters

- `<project-type>` (required): Type of the project to be created. A list of built-in project types is described below. You can get a full list of available project types (including template ones) using the `ya project create --help` command.
- `dest_path` (optional): A path to the directory where the new project will be created. If this parameter isn't specified, the project will be created in the current directory.
- `options` (optional): Additional parameters for configuring the project to be created.
You can get a list of available options using the `ya project create <project-type> --help` command.

#### Built-in project types

The `ya project create` command supports the following built-in project types (`<project-type>`):

Name | Description
:--- | :---
`library` | Simple C++ library.
`program` | Simple C++ program.
`unittest` | Test for C++ on the `unittest` framework. With the `--for` parameter, it creates tests for the specified library (with the test source code in it).
`mkdocs_theme` | mkdocs Python library. The `--name` parameter must be specified: name and availability of `__init__.py` in the current directory.
`recurse` | Create `ya.make` to build nested targets using `RECURSE()`.

Most built-in projects support the following options:

- `-h`, `--help`: Project-specific options reference.
- `--set-owner`: Set a user or group as the owner.
- `--rewrite`: Rewrite `ya.make`.
- `-r`, `--recursive`: Create projects recursively through the directory tree.

The `ya project create` command supports extensible template-based project generation, which enables users to create projects using custom templates.

To start using this feature, create a project template and register it for use with `ya project create`.

For information about creating and registering templates, see the `ya` template documentation.

#### Project templates

Project templates are stored in the repository and read exactly when the command is run. To see a full list of available project types, use the `ya project create --help` command. The list includes both built-in and template types.

#### Available template projects

As at the time of writing this documentation, the following template projects are available:

| Name | Description |
| ---------------- | -------------------------------------------- |
| docs | Draft documentation. |
| project_template | A template for creating a project template. |
| py_library | An empty Python 3 library with tests. |
| py_program | An empty Python 3 program with tests. |
| py_quick | A Python 3 program from all `.py` files in the directory. |
| ts_library | A template for the TypeScript library. |

#### Generating a project from a template

Generating a project from a template doesn't differ a lot from generating built-in projects — there is only a slight difference in how the options are handled. Templates can support their own options.

When you specify a target directory, write template options strictly after it.

If a template option matches the built-in option, you can pass it to the template after `--`.

**Example**
```bash translate=no
ya project create docs --help
Create docs project

Usage:
  ya project create docs [OPTION]... [TAIL_ARGS]...

Options:
  Ya operation options
    -h, --help          Print help
  Bullet-proof options
    --set-owner=SET_OWNER
                        Set owner of project(default owner is current user)
    --rewrite           Rewrite existing ya.make
```
**When the option matches**
```bash translate=no
ya project create docs -- --help
usage: Docs project generator [-h] [--name NAME]

optional arguments:
  -h, --help   show this help message and exit
  --name NAME  Docs project name
```

#### Adding a template of your project

A project template represents two Python 3 functions (`get_params(context)`, `postprocess(context, env)`) and a set of optional [jinja](https://ru.wikipedia.org/wiki/Jinja) templates.

The template must be located in the `devtools/ya/handlers/project/templates` subdirectory and be registered in the `devtools/ya/handlers/project/templates/templates.yaml` file.

You can create the basis for a project template using the `ya project create project_template` command, which itself serves as an example of template generation.

It looks like this:

```bash translate=no
/devtools/ya/handlers/project/templates
mkdir my_project
ya project create project_template
```

You will get the following directory structure:
```bash translate=no
devtools/ya/handlers/project/templates/my_project
├── template
│   └── place-your-files-here
├── README.md
└── template.py
```

`template.py` will contain templates for two functions:

```python translate=no
def get_params(context):
    """
    Calculate all template parameters here and return them as dictionary.
    """
    env = {}
    return env

def postprocess(context, env):
    """
    Perform any post-processing here. This is called after templates are applied.
    """
    pass
```

#### Preparing parameters

Use the `get_params` function to get the data required to create a project template and generate an `env` dict to be placed in [jinja](https://ru.wikipedia.org/wiki/Jinja) templates and passed to `prostprocess`.

You can get the parameters from the passed context (the `context` parameter) either by parsing the options passed in the context or by interactively requesting the user.

The `context` parameter is a `namedtuple` of the `Context` type from `template_tools.common`. It has four properties:
- `path`: A relative path from the repository root to the project being created.
- `root`: An absolute path of the repository root.
- `args`: A list of non-parsed arguments passed at the end of the `ya project create <project_name> [path] [args]` call.
- `backup`: An object from `template_tools.common` that enables you to save original files before editing them. The files will be restored in case of exceptions during project generation and saved to `~/.ya/tmp` if something goes wrong (files from the last five sessions are saved).

The code for `get_params` may look like this, for example:

```python translate=no
python
from __future__ import absolute_import
from __future__ import print_function
from pathlib2 import Path
from template_tools.common import get_current_user

def get_params(context):
    """
    Calculate all template parameters here and return them as dictionary
    """
    print("Generating sample project")

    path_in_project = Path(context.path)
    env = {
        "user": context.owner or get_current_user(),
        "project_name": str(path_in_project.parts[-1]),
    }
    return env
```

To exit generation during parameter preparation in a proper way, you can return an instance of `template_tools.common.ExitSetup` instead of `env`.

#### Creating templates

Place [jinja](https://ru.wikipedia.org/wiki/Jinja) templates of the files to be generated in the `template` subdirectory. In these templates, you can use substitutions from the `env` dict.

The generated files will be created based on these templates and saved with the same names in the target directory. There is usually at least one template for `ya.make` and multiple templates for the source code.

For example, we can create the following templates for a project:

The **ya.make** file.

```yamake translate=no
PROGRAM(not_var{{project_name}})

SRCS(
    main.cpp
)

END()
```

The **main.cpp** file.

```с++ translate=no
#include <util/stream/output.h>

int main() {
    Cout << "Hello from not_var{{user}}!" << Endl;
}
```
Don't forget to delete the `place-your-files-here` file.

If the project doesn't use templates, but, for example, generates the code directly in `postprocess`, add the `.empty.notemplate` file to the `template` directory.
This will mean that templates haven't been forgotten and instead weren't provided for the project in the first place.

If your project type has an update template on top of the creation template, you can add a template like this:

**.ya_project.default**

```ya_project.default translate=no
name: not_var{{__PROJECT_TYPE__}}
```

This file will set the default project type for the given directory, and you will be able to run the `ya project update` command without explicitly specifying the project type.

#### Finalizing a template

Once the project is generated, the `postprocess` function will be called. Use it to finalize, make changes, or output messages. For example, by calling `add_recurse(context)` from the `template_tools.common` module, you can add a new project to the parent directory.

```python translate=no
python
def postprocess(context, env):
    """
    Perform any post-processing here. This is called after templates are applied.
    """
    from template_tools.common import add_recurse

    add_recurse(context)

    print("You are get to go. Build your project and have fun:)")

    pass
```
You can also write the entire generation process in this function if you don't want to use templates.

#### Connecting and testing

To make the template available in `ya project create`, register it in the `devtools/ya/handlers/project/templates/templates.yaml` file.

Add the following to the file:

```templates.yaml translate=no
yaml
- name: my_project                   # Имя типа проекта, которое надо указывать в команде
  description: Create test project   # Описание для ya project create --help
  create:                            # Шаблон для `ya project create` (есть еще update)
    path: my_project                 # Относительный путь до директории с шаблоном
```

Now you can test the template. It will be automatically used when you run the `ya project create` command, no additional builds are required.

```bash translate=no
ya project create --help
Create project

Usage: ya project create <subcommand>

Available subcommands:
  docs                  Create docs project
  library               Create simple library project
  mkdocs_theme          Create simple mkdocs theme library
  my_project            Create test project                <<<< Это наш шаблон
  program               Create simple program project
  project_template      Create project template
  py_library            Create python 3 library with tests
  py_program            Create python 3 program with tests
  py_quick              Quick create python 3 program from existing sources without tests
  recurse               Create simple recurse project
  unittest              Create simple unittest project
```

For testing, create `ya.make` in the parent directory:

```bash translate=no
cat project/user/tst/ya.make
```

Run:

```bash translate=no
ya project create my_project project/user/tst/myprj
Generating sample project
You are get to go. Build your project and have fun:)
```

Build via the parent `ya.make` and run:
```bash translate=no
ya make project/user/tst
project/user/tst/myprj/myprj
Hello from user!
```

**Directory structure:**
```bash translate=no
project/user/tst
├── myprj
│   ├── main.cpp
│   ├── myprj
│   └── ya.make
└── ya.make
```

**Files:**

`project/user/tst/ya.make`

```ya.make translate=no
RECURSE(
    myprj
)
```
`project/user/tst/myprj/ya.make`

```ya.make translate=no

PROGRAM(myprj)

SRCS(
    main.cpp
)

END()
```
`project/user/tst/myprj/main.cpp`

```cpp translate=no
#include <util/stream/output.h>

int main() {
    Cout << "Hello from user!" << Endl;
}
```

#### File names in a template

Files from a template are usually transferred to the generated project with their names intact. However, there are a few additional rules to help you avoid difficult and unusual cases.

- The `.empty.notemplate` file enables you to create an empty directory. All files in the directory containing that file will be ignored.

- `.notemplate` files are ignored. This enables you to have service files in the template directory that won't be considered templates and won't end up in the project.

- After the template data is substituted, `.template` files are written to the project without the `.template` extension. This enables you to have files in the template that can be specially processed under their ordinary names. For example, you can't have a file named `a.yaml` in the template, but you can use the name `a.yaml.template`, which will become `a.yaml` in the target project after data substitution.

**Examples:**

- To add the `x.template` file to the project, place a template named `x.template.template` for it.
- To add the `x.notemplate` file to the project, place a template named `x.notemplate.template` for it.

We recommend running the `ya project create` command in an empty directory. Although this command doesn't normally overwrite existing files and terminates with an error, this behavior doesn't guarantee protection against possible code errors.

Built-in handlers currently don't create directories for a project if they are missing, whereas template handlers create the necessary directories automatically.

### update

The `ya project update` command is used to update a standard project. It can create and modify both `ya.make` build description files and any other files, including source code and/or documentation. Unlike the `ya project create` command, `ya project update` is used to augment or update an existing project.

`ya project update [project-type] [dest_path] [options]`

- `project-type`: Type of the project to be updated.
- `dest_path`: A path to the directory of the project to be updated.
- `options`: Additional command parameters.

#### Supported functions

`ya project update` supports a small set of built-in project types and extensible template-based project updates. To see a full list of available project types (including template ones), use the command:
```bash translate=no
ya project update --help
```
Some project types can be used in both the `ya project create` and `ya project update` commands, while the others are valid only for one of these commands.

#### Basic types of built-in projects

Updates are available for the following built-in project types:

Name | Description
:--- | :---
`recurce` | Write missing `RECURSE` on child projects to `ya.make`.
`resources` | Update information about auto-updated resources.

#### Options for built-in projects

- `-h`, `--help`: Project-specific options reference.
- `-`r, `--recursive`: Update projects recursively through the directory tree.
- `--dry-run`: Skip updating `resources` while showing what would be updated.

#### Default project file

If the project type isn't specified, `ya project update` will attempt to find a file named `.ya_project.default`. This is a file in `yaml` format where the command will search for the `name` key. This key's value will be used as the project type. Such a file can be created with the `ya project create` command.

**Use cases**
```bash translate=no
project/user/libX
├── bin
│   ├── __main__.py
│   └── ya.make
├── dummy
│   └── file.X
├── lib
│   ├── tests
│   │   ├── test.py
│   │   └── ya.make
│   ├── app.py
│   └── ya.make
└── ya.make
```

Suppose that there are two files missing `RECURSE`.

1. `project/user/libX/ya.make`

```ya.make translate=no

RECURSE(
    bin
)
```

2. `project/user/libX/lib/ya.make`
```ya.make translate=no
PY3_LIBRARY()

PY_SRCS(app.py)

END()
```
When running:
```bash translate=no
ya project update recurse project/user/libX  --recursive

Warn: No suitable directories in project/user/libX/dummy
Info: project/user/libX/lib/ya.make, RECURSE was updated
Info: project/user/libX/ya.make, RECURSE was updated
```
When testing:

1. `project/user/libX/ya.make` (note that we don't need `RECURSE` on `dummy`, because there is no `ya.make`)
```ya.make translate=no

RECURSE(
    bin
    lib
)
```

2. `project/user/libX/lib/ya.make`
```ya.make translate=no
PY3_LIBRARY()

PY_SRCS(app.py)

END()

RECURSE(
    tests
)
```
#### Template-based updating

In addition to the built-in project types described above, the `ya project update` command supports using templates to extend the functionality. Project templates are stored in the repository and are loaded at command runtime.

You can get a full list of available project types, including both built-in and template ones, using the command:
```bash translate=no
ya project update --help
```
#### Available template projects

As at the time of writing this documentation, the following template projects are available for updates:

| Name | Description |
|:---|:---|
| `py_library` | Add missing `.py` files to the project's `PY_SCRS`. |
| `py_program` | Add missing `.py` files to the project's `PY_SCRS`. |
| `py_quick` | Add missing `.py` files to the project's `PY_SCRS`. |

All of these templates use the same mechanism that adds missing files.

#### Specifics of template-based updating

The process of template-based project updating is similar to generating built-in projects, except for some differences in the handling of options. The number of common options is kept to a minimum, but templates can support their own options.

When specifying the target directory, specify the template options strictly after it. If a template option matches the built-in option, you can pass it to the template after `--`.

#### Example of using project update options

For example, updating of projects for Python supports the following options:

```bash translate=no
project update py_quick -- --help
usage: Python project updater [-h] [--recursive]

optional arguments:
  -h, --help   Показать справку
  --recursive  Добавить файлы из поддиректорий тоже
```

Note that the `--recursive` option has different meanings in built-in and template projects.

For built-in projects, `--recursive` means a recursive update through the directory tree.
However, recursive operations aren't supported for template-based updates. In that case, `--recursive` is a template option indicating that `.py` files should be recursively searched for in subdirectories and added to the project being updated.

#### Adding a project update template

The process of adding a template to update projects is almost the same as adding a template to create projects, but there are a few differences.
These differences are intended to ensure that project files are updated securely and reliably.

#### Specifics of adding update templates

**Processing existing files**

- When you create a file using a template, the `ya project create` command fails if the target file already exists.
- In contrast, the `ya project update` command overwrites the existing file. The original file will be saved and restored in case of an error.
- If something goes wrong, original files are stored in the `.ya/tmp` directory with data from the last five runs.
- When modifying files from the template code, we recommend using the `backup` object passed in the context.

#### Registering an update template

To register an update template, specify the path in the `update` section of the `yaml` configuration file. Configuration example:

```yaml
  - name: my_project                   # Name of the project type, which is specified in the command
    description: Create test project   # Description for ya project create --help
    create:                            # Template for `ya project create`
      path: my_project_up              # Relative path to the directory with a creation template
    update:                            # <-- Template for `ya project update`
      path: my_project_up              # <-- Relative path to the directory with an update template
```

The `create` section in the configuration file is optional. You can create projects intended only for updating, not creating.

### fix-peerdirs

Add missing and remove redundant `PEERDIR` writes in the project. Whether writes are required is determined by an analysis of dependencies of the project source files.

Command syntax: `ya project fix-peerdirs [OPTION]... [TARGET]...`

#### Options
```bash
    -h, --help          Help
    -a                  Only add missing PEERDIRs
    -d                  Only remove redundant PEERDIRs
    -v                  Detailed troubleshooting
    -l                  Only troubleshooting
    --sort              Sort PEERDIRs
    -c, --cycle         Find circular dependencies
```

### macro

The `ya project macro` command is used to add or remove macros in the `ya.make` file.

#### Adding a macro

`ya project macro add <macro_string> [OPTIONS]`

- `<macro_string>`: A macro string to be added.
- `[OPTIONS]`: Additional parameters.

**Options for the add command**

- `--after=SET_AFTER`: Add a macro after the specified macro.
- `--append`: Append arguments to the existing macro.

**Examples**

```bash
  ya project macro add "MACRO_NAME(VALUE1 VALUE2 ...)"                               Insert the MACRO_NAME macro at the beginning of the macro list
  ya project macro add --set_after=AFTER_MACRO_NAME "MACRO_NAME(VALUE1 VALUE2 ...)"  Insert the MACRO_NAME macro after AFTER_MACRO_NAME at the beginning of the macro list
```

#### Removing a macro

`ya project macro remove <macro_name>` where `<macro_name>` is the name of the macro to be removed.
