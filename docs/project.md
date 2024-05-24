## ya project

Создать или изменить файл ya.make проекта.

`ya project <subcommand>`

### Команды
```
 create            Create project
 fix-peerdirs      Add missing and delete unused peerdirs to the project
 macro             Add or delete macro in ya.make
 owner             Process owners
```

### Примеры
```
  ya project macro add "MACRO_NAME(VALUE1 VALUE2 ...)"                               Insert macro MACRO_NAME in top of the makelist
  ya project macro add --set_after=AFTER_MACRO_NAME "MACRO_NAME(VALUE1 VALUE2 ...)"  Insert macro MACRO_NAME after AFTER_MACRO_NAME in top of the makelist
```

### create

Команда `ya project create` используется для создания нового типового проекта. Команда может создавать и модифицировать файлы описания сборки `ya.make`, а также любые другие файлы, включая исходный код и документацию.

#### Синтаксис
`ya project create <project-type> [dest_path] [options]`

#### Параметры

- `<project-type>` (обязательный): Тип создаваемого проекта. Список встроенных типов проектов описан ниже. Полный список доступных типов проектов (включая шаблонные) можно получить с помощью команды `ya project create --help`.
- `dest_path` (необязательный): Путь к каталогу, в котором будет создан новый проект. Если параметр не указан, проект будет создан в текущем каталоге.
- `options` (необязательный): Дополнительные параметры для настройки создаваемого проекта. Список доступных опций можно получить с помощью команды `ya project create <project-type> --help`.

#### Встроенные типы проектов

Команда `ya project create` поддерживает следующие встроенные типы проектов (`<project-type>`):

Имя | Описание
:--- | :---
library | Простая C++ библиотека
program | Простая C++ программа
unittest | Тест для C++ на фреймворке unittest. С параметром `--for` создаёт тесты для указанной библиотеки (с исходным код тестов в ней)
mkdocs_theme | Python-библиотека темы mkdocs. Требует указания параметра `--name` - имя шаблона и наличия `__init__.py` в текущей директории.
recurse | Создать `ya.make` для сборки вложенных целей с помощью `RECURSE()`.

Большинство встроенных проектов поддерживают следующие опции:

```
-h, --help        справка по опциям для конкретного проекта
--set-owner       установить владельцем пользователя или группу
--rewrite         перезаписать ya.make
-r, --recursive   создать проекты рекурсивно по дереву директорий
```

Команда `ya project create` поддерживает расширяемую генерацию проектов по шаблонам, что позволяет пользователям создавать проекты с использованием собственных шаблонов.

Чтобы начать использовать эту функцию, вам необходимо создать шаблон проекта и зарегистрировать его для использования с `ya project create`.

Информацию о создании и регистрации шаблонов можно найти в документации по шаблонам `ya`.

#### Шаблоны проектов

Шаблоны проектов хранятся в репозитории и зачитываются непосредственно в момент запуска команды. Полный список доступных типов проектов доступен по команде `ya project create --help`. Список включает как встроенные, так и шаблонные типы.

##### Доступные шаблонные проекты

На момент написания этой документации были доступны следующие шаблонные проекты:

| Имя | Описание |
| ---------------- | -------------------------------------------- |
| docs | Проект документации |
| project_template | Шаблон для создания шаблона проекта |
| py_library | Пустая Python 3 библиотека с тестами |
| py_program | Пустая Python 3 программа с тестами |
| py_quick | Программа на Python 3 из всех .py-файлов в директории |
| ts_library | Шаблон для библиотеки на TypeScript |

##### Генерация проекта по шаблону

Генерация проекта по шаблону не слишком отличается от генерации встроенных проектов, есть лишь небольшое отличие в работе с опциями. Количество общих опций сведено до минимума, из общих опций поддержана только `--set-owner`. Однако, шаблоны могут поддерживать свои опции.

При указании целевой директории шаблонные опции необходимо писать строго после неё. Если опция шаблона совпадает со встроенной, то передать в шаблон опцию можно после `--`.

  **Пример**
  ```
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
  ```
  ya project create docs -- --help 
  usage: Docs project generator [-h] [--name NAME]

  optional arguments:
    -h, --help   show this help message and exit
    --name NAME  Docs project name
  ```

##### Добавление шаблона своего проекта  

Шаблон проекта представляет собой две  функции на `Python 2` (`get_params(context)`, `postprocess(context, env)`)  и набор необязательных [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблонов.
Шаблон должен находиться в поддиректории `devtools/ya/handlers/project/templates` и быть зарегистрирован в файле `devtools/ya/handlers/project/templates/templates.yaml`.

Создать основу для шаблона проекта можно с помощью команды `ya project create project_template`, которая сама является примером использования шаблонной генерации.

Выглядит это примерно так:

```
/devtools/ya/handlers/project/templates
mkdir my_project
ya project create project_template
```

Получится следующая структура директорий:
```
devtools/ya/handlers/project/templates/my_project
├── template
│   └── place-your-files-here
├── README.md
└── template.py
```

`template.py` будет содержать заготовки для двух функций:

```
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

##### Готовим параметры

В функции `get_params` нужно получить данные, необходимые для создания шаблона проекта и сформировать словарь `env`, который будет использоваться для подстановки в [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблоны и передан в `prostprocess`. Параметры можно получить из переданного контекста (параметр `context`), разбором опций, переданных в контексте или интерактивным запросом у пользователя.

Параметр `context` — это `namedtuple` типа `Context` из `template_tools.common`. В нём доступно 5 свойств:
* **`path`** - относительный путь от корня репозитория до создаваемого проекта
* **`root`** - абсолютный путь корня репозитория
* **`owner`** - значение, переданное параметром `--set-owner`
* **`args`** - список неразобранных аргументов, переданных в конце вызова `ya project create <project_name> [path] [args]`
* **`backup`** - объект из `template_tools.common`, позволяющий сохранять оригинальные файлы перед их редактированием. Файлы будут восстановленый в случае исключений во время геренерации проекта, а также будут сохранены в `~/.ya/tmp` на случай если что-то пойдёт не так (сохраняются файлы от последних 5 сейссий).

Код для `get_params` может выглядеть, например так:

```
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

Для аккуратного выхода из генерации во время подготовки параметров вместо `env` можно вернуть объект класса `template_tools.common.ExitSetup`

##### Создание шаблонов

В поддиректории template необходимо разместить [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблоны генерируемых файлов. В этих шаблонах можно использовать подстановки из словаря `env`. Генерируемые файлы будут создаваться на основе этих шаблонов и сохраняться под теми же именами в целевой директории. Обычно среди шаблонов будет, по крайней мере, один `для ya.make` и несколько для исходного кода.

Например, для нашего проекта мы можем создать следующие шаблоны:

**ya.make**

```yamake
OWNER(not_var{{user}})

PROGRAM(not_var{{project_name}})

SRCS(
    main.cpp
)

END()
```

**main.cpp**

```
#include <util/stream/output.h>

int main() {
    Cout << "Hello from not_var{{user}}!" << Endl; 
}
```
Не забудьте удалить файл `place-your-files-here`.

Если проект не использует шаблоны, а, например, генерирует код напрямую в `postprocess`, то в директорию `template` следует добавить файл `.empty.notemplate`. Это демонстрирует, что шаблоны не предусмотрены и не были забыты.

Если у вашего типа проекта есть не только шаблон создания, но и шаблон обновления, то вы можете добавить вот такой шаблон:

**.ya_project.default**

```
name: not_var{{__PROJECT_TYPE__}}
```

Этот файл сделает ваш тип проекта по умолчанию для данной директории, и команду `ya project update` можно будет запускать без явного указания типа проекта.

##### Доработка шаблона

После генерации проекта будет вызвана функция `postprocess`. В этой функции можно выполнить окончательные доработки, внести изменения или вывести сообщения. Например, с помощью вызова `add_recurse(context)` из модуля `template_tools.common` можно добавить новый проект в родительскую директорию.

```
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
В этой же функции можно разместить вообще всю генерацию если не хочется использовать шаблоны.

##### Подключение и тестирование

Чтобы шаблон стал доступен в `ya project create` его нужно зарегистрировать в файле `devtools/ya/handlers/project/templates/templates.yaml`.

Надо добавить в этот файл примерно следующее:

```
yaml
- name: my_project                   # Имя типа проекта, которое надо указывать в команде 
  description: Create test project   # Описание для ya project create --help
  create:                            # Шаблон для `ya project create` (есть ещё update)
    path: my_project                 # Относительный путь до директории с шаблоном
```

Теперь можно протестировать шаблон. Он будет автоматически использован при запуске команды `ya project create`, никаких дополнительных сборок не потребуется.

```
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

Для проверки создадим `ya.make` в родительской директории:

```
cat project/user/tst/ya.make
OWNER(user)
```

Запускаем:

```
ya project create my_project project/user/tst/myprj 
Generating sample project
You are get to go. Build your project and have fun:)
```

Собираем через родительский `ya.make` и запускаем:
```
ya make project/user/tst
project/user/tst/myprj/myprj
Hello from user!
```

**Структура директорий:**
```
project/user/tst
├── myprj
│   ├── main.cpp
│   ├── myprj
│   └── ya.make
└── ya.make
```

**Файлы:**

`project/user/tst/ya.make`

```
OWNER(user)

RECURSE(
    myprj
)
```
`project/user/tst/myprj/ya.make`
```
OWNER(user)

PROGRAM(myprj)

SRCS(
    main.cpp
)

END()
```
`project/user/tst/myprj/main.cpp`
```
#include <util/stream/output.h>

int main() {
    Cout << "Hello from user!" << Endl;
}
```

##### Имена файлов в шаблоне

Обычно файлы из шаблона переносятся в генерируемый проект с сохранением своих имен. Однако есть несколько дополнительных правил, призванных помочь обойти сложные и необычные случаи.

- Файл `.empty.notemplate` позволяет создать пустую директорию. Все файлы в директории, содержащей такой файл, будут проигнорированы.

- Файлы с расширением `.notemplate` игнорируются. Это позволяет иметь в директории шаблона служебные файлы, которые не будут считаться шаблонами и не попадут в проект.

- Файлы с расширением `.template` после подстановки данных шаблона записываются в проект без расширения `.template`. Это позволяет иметь в шаблоне файлы, которые могут быть специально обработаны под своими обычными именами. Например, в шаблоне нельзя иметь файл `a.yaml`, но можно использовать `a.yaml.template`, и после подстановки данных он станет `a.yaml` в целевом проекте.

**Примеры:**

- Чтобы добавить в проект файл `x.template`, положите для него шаблон с именем `x.template.template`.
- Чтобы добавить в проект файл `x.notemplate`, положите для него шаблон с именем `x.notemplate.template`.

