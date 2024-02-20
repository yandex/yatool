# ya project

Создать или изменить файлы ya.make.

`ya project <subcommand>`

## Команды
```
 create            Create project
 fix-peerdirs      Add missing and delete unused peerdirs to the project
 macro             Add or delete macro in ya.make
 owner             Process owners
```

## Примеры
```
  ya project macro add "MACRO_NAME(VALUE1 VALUE2 ...)"                               Insert macro MACRO_NAME in top of the makelist
  ya project macro add --set_after=AFTER_MACRO_NAME "MACRO_NAME(VALUE1 VALUE2 ...)"  Insert macro MACRO_NAME after AFTER_MACRO_NAME in top of the makelist
```

## create

Создать новый типовой проект.

`ya project create <project-type> [dest_path] [options]`

Команда может создавать и модифицировать как файлы описания сборки `ya.make`, так и любые другие файлы включая исходный код и/или документацию.


`ya project create` поддерживает небольшой набор встроенных типов проектов и расширяемую генерацию проектов по шаблонам.
Встроенные типы проектов описаны ниже, полный список доступных типов проектов (включая шаблонные) можно получить командой `ya project create --help`.

### Встроенные типы проектов

Можно создать проекты следующих встроенных типов:

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

### Шаблоны проектов

Кроме описанный выше встроенных типов проектов, поведение `ya project create` может быть расширено за счёт шаблонов.
Шаблоны проектов хранятся в репозитории и зачитываются непосредственно в момент запуска команды. Полный список доступных
типов проектов доступен по команде `ya project create --help`. Список включает как встроенные, так и шаблонные типы.

На момент написания этой документации были доступны следующие шаблонные проекты:

Имя | Описание
:--- | :---
docs | Проект документации
project_template | Шаблон для создания шаблона проекта
py_library | Пустая Python 3 библиотека с тестами
py_program | Пустая Python 3 программа с тестами
py_quick | Программа на Python 3 из всех .py-файлов в директории
ts_library | Шаблон для библиотеки на TypeScript

Генерация проекта по шаблону не слишком отличается от генерации встроенных проектов, есть лишь небольшое отличие в работе с опциями. 
Количество общих опций сведено до минимума, из общих опций поддержана только `--set-owner`. Однако, шаблоны могут поддерживать свои опции.
При указании целевой директории их необходимо писать строго после неё. Если опция шаблона совпадает со встроенной, то передать в шаблон опцию можно после `--`.

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

### Добавление шаблона своего проекта  

Шаблон проекта — это, по сути, две опциональных функции на Python 2 и набор опять же опциональных [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблонов.
Шаблон должен быть размещён в поддиректории в `devtools/ya/handlers/project/templates` и зарегистрирован в `devtools/ya/handlers/project/templates/templates.yaml`

Создать заготовку для шаблона проекта можно командой `ya project create project_template`, которая сама является примером шаблонной генерации.
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
```python
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

#### Готовим параметры

В функции `get_params` нужно получить данные, необходимые для создания шаблона проекта и сформировать словарик `env`, который будет использоваться для подстановки в [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблоны и передан в `prostprocess`.
Параметры можно получить из переданного контекста (параметр `context`), разбором опций, переданных в контексте или интерактивным запросом у пользователя.

Параметр `context` — это `namedtyple` типа `Context` из `template_tools.common`.
В нём доступно 5 свойств:
* **`path`** - относительный путь от корня репозитория до создаваемого проекта
* **`root`** - абсолютный путь корня репозитория
* **`owner`** - значение, переданное параметром `--set-owner`
* **`args`** - список неразобранных аргументов, переданных в конце вызова `ya project create <project_name> [path] [args]`
* **`backup`** - объект из `template_tools.common`, позволяющий сохранять оригинальные файлы перед их редактированием. Файлы будут восстановленый в случае исключений во время геренерации проекта,
  а также будут сохранены в `~/.ya/tmp` на случай если что-то пойдёт не так (сохраняются файлы от последних 5 сейссий).

Код для `get_params` может выглядеть, например так:

```python
from __future__ import absolute_import
from __future__ import print_function
from pathlib2 import Path
from template_tools.common import get_current_user

def get_params(context):
    """
    Calculate all template parameters here and return them as dictionary
    """
    print("Generating sample project")

    path_in_arcadia = Path(context.path)
    env = {
        "user": context.owner or get_current_user(),
        "project_name": str(path_in_arcadia.parts[-1]),
    }
    return env
```

Для аккуратного выхода из генерации во время подготовки параметров вместо `env` можно вернуть объект класса `template_tools.common.ExitSetup`/

#### Пишем шаблоны

В поддиректории `template` нужно разместить [jinja](https://ru.wikipedia.org/wiki/Jinja)-шаблоны генерируемых файлов. В них можно использовать подстановку из `env`.
По шаблонам будут генерироваться одноимённые файлы в целевой директории. Обычно среди шаблонов будет один для `ya.make` и, возможно, ещё несколько для исходного кода.

Например, мы можем сделать для нашего проекта такие шаблоны:

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
```cpp
#include <util/stream/output.h>

int main() {
    Cout << "Hello from not_var{{user}}!" << Endl; 
}
```

Не забудьте стереть файл `place-your-files-here`

Если проект не использует шаблоны, а, например, генерирует код в `postprocess` сам, то в директорию `template` надо положить файл `.empty.notemplate`.
Так сделано чтобы было понятно, что шаблонов именно не предполагается, их не забыли положить.

Если у вашего типа проекта есть не только шаблон создания, но и шаблон обновления, то вы можете добавить вот такой шаблон:

**.ya_project.default**
```
name: not_var{{__PROJECT_TYPE__}}
```

Это сделает ваш тип проекта умолчательным в этой директории и для его обновляния можно будет вызывать `ya project update` без явного указания типа проекта.

#### Финальные штрихи

После генерации будет вызвана функция `postprocess`. В ней можно внести финальные штрихи в сегенрированный проект или просто вывести что-то приятное.
В частности, с помощью вызова `add_recurse(context)` из `template_tools.common` можно добавить наш проект в родительской директории.

```python
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

#### Подключаем и тестируем

Чтобы наш шаблон стал доступен в `ya project create` нужно зарегистрировать его в файле devtools/ya/handlers/project/templates/templates.yaml`.

Надо добавить в этот файл примерно следующее:

```yaml
- name: my_project                   # Имя типа проекта, которое надо указывать в команде 
  description: Create test project   # Описание для ya project create --help
  create:                            # Шаблон для `ya project create` (есть ещё update)
    path: my_project                 # Относительный путь до директории с шаблоном
```

Теперь можно проверить наш шаблон. Он будет взят во время запуска команды `ya project create`, ничего пересобирать не требуется.

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

Попробуем его в деле, чтобы проверить все механики создадим ya.make в родительской директории:

```
cat junk/user/tst/ya.make
OWNER(user)
```

Запускаем:

```
ya project create my_project junk/user/tst/myprj 
Generating sample project
You are get to go. Build your project and have fun:)
```

Собираем через родительский ya.make и запускаем:
```
ya make junk/user/tst
junk/user/tst/myprj/myprj
Hello from user!
```

**Структура директорий:**
```
junk/user/tst
├── myprj
│   ├── main.cpp
│   ├── myprj
│   └── ya.make
└── ya.make
```

**Файлы:**

junk/user/tst/ya.make
```yamake
OWNER(user)

RECURSE(
    myprj
)
```

junk/user/tst/myprj/ya.make
```yamake
OWNER(user)

PROGRAM(myprj)

SRCS(
    main.cpp
)

END()
```

junk/user/tst/myprj/main.cpp
```cpp
#include <util/stream/output.h>

int main() {
    Cout << "Hello from user!" << Endl;
}
```

#### Об именовании файлов в шаблонне

Обычно файлы в шаблоне переносятся в генерированный проект под своими именами. Однако есть несколько дополнительных правил, призванных помочь обойти странные и сложные случаи.

- Файл `.empty.notemplate` позоляет создать пустую директорию. Все файлы в директории, содержащие такой файл, будут проигнорированы.

- Файлы с расширением `.notemplate` игнорируются. Это позволяет иметь в директории шаблона служебные файлы, которые не будут считаться шаблонами и попадать в проект.

- Файлы с расширением `.template` после подстановки шаблонов записываются в проект без расширения `.template`. Это позволяет иметь в шаблоне файлы, которые могут по своим обычным именам специально обрабатываться. Например, `a.yaml` иметь в шаблоне нельзя, но можно положить `a.yaml.template` и в целевом проекте после подставновки шаблона он станет `a.yaml`. 


* Чтобы добавить в проект файл `x.template`, положите для него шаблон `x.template.template`
* Чтобы добавить в проект файл `x.notemplate`, положите для него шаблон `x.notemplate.template`


## update

Обновить типовой проект.

`ya project update [project-type] [dest_path] [options]`

Команда может создавать и модифицировать как файлы описания сборки `ya.make`, так и любые другие файлы включая исходный код и/или документацию. В отличие от `ya project create` эта команда предназначена для догенерации или актуализации существующего проекта.

`ya project update` поддерживает небольшой набор встроенных типов проектов и расширяемое обновление проектов по шаблонам.
Встроенные типы проектов описаны ниже, полный список доступных типов проектов (включая шаблонные) можно получить командой `ya project update --help`.

Некоторые типы проектов могут быть использованы как в `ya project create`, так и в `ya project update`, другие могут работать только в одной из этих команд.

`ya project update` без указания типа попробует найти файл с именем `.ya_project.default`. Это файл в формате yaml в котором команда будет искать ключ `name`. Значение этого ключа и будет использовано как тип проекта.
Такой файл может, например, создавать команда `ya project create`.


### Встроенные типы проектов

Доступно обновление для следующих встроенных типов проектов:

Имя | Описание
:--- | :---
recurce | Дописать в ya.make недостающие `RECURSE` на дочерние проекты
resources | Обновить информацию об автообновляемых ресурсах

Встроенные проектов поддерживают следующие опции:

```
-h, --help        справка по опциям для конкретного проекта
-r, --recursive   обновить проекты рекурсивно по дереву директорий
--dry-run         для `resources` не делать обновление, а лишь показать, что будет обновлено
```

**Пример:**

```
junk/user/libX
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

Допустим не хватает `RECURSE` в двух файлах.

junk/user/libX/ya.make

```yamake
OWNER(user)

RECURSE(
    bin
)
```

junk/user/libX/lib/ya.make
```
OWNER(user)

PY3_LIBRARY()

PY_SRCS(app.py)

END()
```

Запускаем

```
ya project update recurse junk/user/libX  --recursive

Warn: No suitable directories in junk/user/libX/dummy
Info: junk/user/libX/lib/ya.make, RECURSE was updated
Info: junk/user/libX/ya.make, RECURSE was updated
```

Проверяем:

junk/user/libX/ya.make (заметим, что `RECURSE` на `dummy` не нужен, там нет `ya.make`)
```yamake
OWNER(user)

RECURSE(
    bin
    lib
)
```

junk/user/libX/lib/ya.make
```
OWNER(user)

PY3_LIBRARY()

PY_SRCS(app.py)

END()

RECURSE(
    tests
)
```

### Обновление по шаблону

Кроме описанный выше встроенных типов проектов, поведение `ya project update` может быть расширено за счёт шаблонов.
Шаблоны проектов храняться в репозитории и зачитываются непосредственно в момент запуска команды. Полный список доступных типов проектов доступен по команде `ya project update --help`. Список включает как встроенные, так и шаблонные типы.

На момент написания этой документации для обновления были доступны следующие шаблонные проекты:

Имя | Описание
:--- | :---
py_library | Добавить отсутствующие .py-файлы в `PY_SCRS` проекта
py_program | Добавить отсутствующие .py-файлы в `PY_SCRS` проекта
py_quick | Добавить отсутствующие .py-файлы в `PY_SCRS` проекта

Все они используют один и тот же шаблон, просто добавляющий отсутствующие файлы.

Обновление проекта по шаблону не слишком отличается от генерации встроенных проектов, есть лишь небольшое отличие в работе с опциями. 
Количество общих опций сведено до минимума. Однако, шаблоны могут поддерживать свои опции. При указании целевой директории их необходимо писать строго после неё. Если опция шаблона совпадает со встроенной, то передать в шаблон опцию можно после `--`.

Например, обновление проектов для Python поддерживает следующие опции:

```
project update py_quick -- --help
usage: Python project updater [-h] [--recursive]

optional arguments:
  -h, --help   Показать справку
  --recursive  Добавить файлы из поддиректорий тоже
```

Заметим, что `--recursive` здесь и во встроенных проектах имеет разный смысл. Для обновления по шаблону рекурсивная
работа не поддерживается и в данном случае это опция самого шаблона, говорящая, что `.py`-файлы надо собрать рекурсивно и
добавить их все в обновляемый проект.

### Добавление шаблона для обновления

Добавление шаблона для обновления практически не отличается от добавления шаблона для создания проекта.
Есть лишь следующие особенности:

* При созданнии файла по шаблону если целевой файл уже существует, то `ya project create` закончится с ошибкой.
  Обновление `ya project update` файл перезапишет, оригинальный файл будет сохранён и в случае исключения воостановлен.
  Кроме того, он останется в `.ya/tmp` на случай если что-то пойдён не так (хранятся данные от последних 5 запусков).
  При модификации файлов из кода шаблона рекомендуется также использовать объект `backup` передаваемый в контексте.

* Для регистрации шаблона обновления путь надо указывать в секции `update`
  ```yaml
  - name: my_project                   # Имя типа проекта, которое надо указывать в команде 
    description: Create test project   # Описание для ya project create --help
    create:                            # Шаблон для `ya project create`
      path: my_project_up              # Относительный путь до директории с шаблоном создания
    update:                            # <-- Шаблон для `ya project update`
      path: my_project_up              # <-- Относительный путь до директории с шаблоном обновления
  ```
  Секция `create` не является обязательной, могут быть проекты только для обновления.


## fix-peerdirs

Добавить недостающие и удалить ненужные `PEERDIR` в проекте. Нужность определяется анализом зависимостей исходных файлов проекта.

```
ya project fix-peerdirs [OPTION]... [TARGET]...
```

**Опции**
```
    -h, --help          Справка
    -a                  Только добавить недостающие PEERDIR
    -d                  Только удалить лишние PEERDIR
    -v                  Подробная диагностика
    -l                  Только диагностика
    --sort              Отсортировать PEERDIR
    -c, --cycle         Обнаруживать циклические зависимости
```

## macro

Добавить или удалить макрос в ya.make

```
ya project macro add <macro_string> [OPTIONS]

```

```
ya project macro remove <macro_name>
```

**Опции (только для add):**
```
    --after=SET_AFTER   Добавить после макроса 
    --append            Добавить аргументы к существующему макросу
```

**Доступные команды:**

Подкоманда | Описание
:--- | :---
<без команды> | Показать владельцев для проекта
`add <owners>` | Добавить владельцев
`check_logins` | Проверить актуальность владельцев. `--is-maternity` и `--is-dismissed` на отпуск по уходу за ребёнком и уволенность соотвественно
`optimize` | Оптимизировать список владельцев: удалить владельцев-пользовалетей если они принадлежат владельцу-группе
`remove <owners>` | Удалить владельцев из ya.make. `--default-owner=<owner>` позволяет проставить владельца, если после удаления владельцев не осталось.
`replace <old^new>` | Заменить в списке владельцев старного на нового.
`set <owners>` | Сделать владельцами `owners`. Все прежние владельцы будут удалены.

**Пример:**

```
cat ya.make
LIBRARY()

OWNER(g:group)

SRCS(
    a.cpp
)
END()

ya project owners set user

cat ya.make
LIBRARY()

OWNER(user)

SRCS(
    a.cpp
)
END()

ya project owners replace user^g:users

cat ya.make
LIBRARY()

OWNER(g:users)

SRCS(
    a.cpp
)
END()
```
