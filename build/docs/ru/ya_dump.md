## ya dump

Команда `ya dump` разработана для извлечения и предоставления детализированной информации о различных аспектах системы сборки и репозитория.

Эта информация может включать в себя сведения о сборочном графе, [зависимостях между модулями](#Анализ-зависимостей), конфигурационных параметрах и многом другом.

### Синтаксис

Общий формат команды выглядит следующим образом:
```bash
ya dump <subcommand> [OPTION] [TARGET]
```
Где:
- `ya dump` — основной вызов команды, указывает на то, что вы хотите использовать функциональность извлечения данных о проекте.
- `<subcommand> —` обозначает конкретное действие или отчет, который вы хотите получить.
Например, `modules`, `dep-graph`, `conf-docs` и т.д. Каждая подкоманда имеет свое предназначение и набор дополнительных параметров и опций.
- `[OPTION]` (необязательный) — дополнительные флаги или ключи, которые модифицируют поведение выбранной подкоманды. Опции позволяют настроить вывод команды, уточнить, какие данные необходимо извлечь, или изменить формат вывода.
- `[TARGET]` (необязательный) — дополнительные параметры, необходимые для выполнения некоторых подкоманд, могут включать директорию, названия модулей или другие специфические данные, необходимые для выполнения команды.

### Общие опции
Актуальный список всех доступных команд можно посмотреть с помощью:
```bash
ya dump --help
```

Чтобы просмотреть список всех доступных опций для конкретной подкоманды утилиты `ya dump`, можно использовать параметр `--help` сразу после указания интересующей подкоманды.

Это выведет подробную справку по опциям и их использованию для выбранной подкоманды.

Например, для вывода всех доступных опций для подкоманды `modules` команда будет выглядеть так:
```bash
ya dump modules --help
```
Опции команд `ya dump` во многом аналогичны опциям `ya make`, например:
* `-xx` — перестроить граф полностью (не использовать кеши).
* `-d`, `-r` — граф для отладочного или релизного режима сборки.
* `-k` — игнорировать ошибки кофигурации.

Опции для задания параметров сборки и платформ:

- `-D=FLAGS` — установить переменные сборки.
- `--host-platform-flag=HOST_PLATFORM_FLAGS` — флаг для хостовой платформы.
- `--target-platform=TARGET_PLATFORMS` — указать целевую платформу.
- `--target-platform-flag=TARGET_PLATFORM_FLAG` — установить флаги для целевой платформы.
- `--build=BUILD_TYPE` — тип сборки (по умолчанию `release`).
- `--sanitize=SANITIZE` — тип санитайзера.
- `--race` — сборка Go-проектов с детектором гонок.

Дополнительные опции для настройки сборочного процесса:

- `--host-build-type=HOST_BUILD_TYPE` — указать тип сборки для хостовой платформы.
- `--host-platform=HOST_PLATFORM` — указать хостовую платформу.
- `--c-compiler=C_COMPILER` — указать компилятор C.
- `--cxx-compiler=CXX_COMPILER` — указать компилятор C++.
- `--lto` — включить `Link Time Optimization` (`LTO`).
- `--thinlto` — включить `ThinLTO`.
- `--afl` — использовать `American Fuzzy Lop` для фаззинга.
- `--hardening` — включить дополнительные более строгие проверки кода.
- `--cuda=CUDA_PLATFORM` — включить интеграцию `NVIDIA CUDA`.
- `--tools-cache-size=TOOLS_CACHE_SIZE` — задать ограничения на размер кеша инструментов.

Опции фильтрации:

Эти опции используются для фильтрации и уточнения данных, которые должны быть извлечены или представлены в результате выполнения подкоманды и влияют на генерируемый сборочный граф.
- `--force-build-depends` (или `-t`, или `-A`) — показывает зависимости.
- `--ignore-recurses` — игнорирование `RECURSE` тегов в `ya.make` файлах.
- `--no-tools` — исключение зависимостей, связанных с инструментами сборки из вывода.
- `--from=<module>`, `--to=<module>` — указание начальной и конечной точки для отображения пути зависимостей. Опции `--from` и `--to` можно указывать по несколько раз.

Опции форматирования:

Предоставляют дополнительные возможности для настройки формата вывода результатов выполнения подкоманд.

- `--json` — выводит информацию в формате JSON. Подходит, когда данные предполагается дальше обрабатывать программным путем.
- `--flat-json`, `--flat-json-files` (для подкоманды dep-graph) — форматирует вывод графа зависимостей в плоский JSON-список, фокусируясь на файловых зависимостях или дугах графа. Граф зависимостей обычно содержит файловые ноды и ноды команд. Опция `--flat-json-files` позволяет вывести только файловые ноды и зависимости между ними.
- `-q`, `--quiet` — ничего не выводить.

### Анализ зависимостей

Управление зависимостями является одной из ключевых задач в процессе разработки проектов. Правильное определение и анализ зависимостей позволяет обеспечивать корректную сборку проекта, избегая проблем, связанных с несовместимостями библиотек, повторяющимися зависимостями и другими распространенными проблемами.

Основные цели и задачи использования `ya dump` при анализе зависимостей:

1. Анализ зависимостей для модуля и между модулями:
`ya dump` позволяет выявить, какие модули зависят друг от друга, и каким образом эти зависимости влияют на процесс сборки.
2. Фильтрация зависимостей:
С помощью различных опций (`--ignore-recurses`, `--no-tools` и т.д.) можно детализировать граф зависимостей. Исключать определенные типы зависимостей (например, зависимые модули, тестовые зависимости и сборочные инструменты) для более точного анализа.
3. Определение путей зависимостей от одного модуля к другому, включая все возможные пути.
4. Поддержка различных языков и платформ:
`ya dump` работает с зависимостями для разных языков программирования (например, C++, Java, Python, Go) и учитывает их особенности, такие как логические и сборочные зависимости.

### Подкоманды `<subcommand>`

Подкоманды `ya dump` выводят информацию о сборочном графе, а также позволяют получить информацию о системе сборки.

**Первая группа** подкоманд может быть полезна для анализа зависимостей между модулями, а также для поиска проблем. К ней относятся:

- [`ya dump modules`](#ya-dump-modules) — список зависимых модулей.
- [`ya dump relation`](#ya-dump-relation) — зависимость между двумя модулями.
- [`ya dump all-relations`](#ya-dump-all-relations) — все зависимости между двумя модулями.
- [`ya dump dot-graph`](#ya-dump-dot-graph) — граф всех межмодульных зависимостей данного проекта.
- [`ya dump dep-graph`](#dep-graph-и-json-dep-graph), `ya dump json-dep-graph` — граф зависимостей системы сборки.
- [`ya dump build-plan`](#ya-dump-build-plan) — граф сборочных команд.
- [`ya dump loops`, `ya dump peerdir-loops`](#ya-dump-loops-и-peerdir-loops) — информация о циклах в графе зависимостей.
- [`ya dump compile-commands`, `ya dump compilation-database`](#ya-dump-compile-commands-и-compilation-database) — информация о сборочных командах (`compilation database`).

По умолчанию вывод этих подкоманд основан на графе обычной сборки без тестов.

Для поиска информации с учетом сборки тестов надо добавить опцию `-t`, например, `ya dump modules -t`.

**Вторая группа** — это различная информация от системы сборки. К ней относятся:

- [`ya dump groups`](#ya-dump-groups) — группы владельцев проектов.
- [`ya dump json-test-list`](#ya-dump-json-test-list) — информация о тестах.
- [`ya dump recipes`](#ya-dump-recipes) — информация о рецептах.
- [`ya dump conf-docs`](#ya-dump-conf-docs) — документация по макросам и модулям.
- [`ya dump debug`](#ya-dump-debug) — сборка отладочного `bundle`.

#### ya dump modules

Показывает список всех зависимостей для цели *target* (текущий каталог, если не задана явно).

Команда: `ya dump modules [option]... [target]...`

**Пример:**
```bash
spreis@starship:~/yatool$ ./ya dump modules devtools/ymake | grep sky
module: Library devtools/ya/yalibrary/yandex/skynet $B/devtools/ya/yalibrary/yandex/skynet/libpyyalibrary-yandex-skynet.a
module: Library infra/skyboned/api $B/infra/skyboned/api/libpyinfra-skyboned-api.a
module: Library skynet/kernel $B/skynet/kernel/libpyskynet-kernel.a
module: Library skynet/api/copier $B/skynet/api/copier/libpyskynet-api-copier.a
module: Library skynet/api $B/skynet/api/libpyskynet-api.a
module: Library skynet/api/heartbeat $B/skynet/api/heartbeat/libpyskynet-api-heartbeat.a
module: Library skynet/library $B/skynet/library/libpyskynet-library.a
module: Library skynet/api/logger $B/skynet/api/logger/libpyskynet-api-logger.a
module: Library skynet/api/skycore $B/skynet/api/skycore/libpyskynet-api-skycore.a
module: Library skynet/api/srvmngr $B/skynet/api/srvmngr/libpyskynet-api-srvmngr.a
module: Library skynet/library/sky/hostresolver $B/skynet/library/sky/hostresolver/libpylibrary-sky-hostresolver.a
module: Library skynet/api/conductor $B/skynet/api/conductor/libpyskynet-api-conductor.a
module: Library skynet/api/gencfg $B/skynet/api/gencfg/libpyskynet-api-gencfg.a
module: Library skynet/api/hq $B/skynet/api/hq/libpyskynet-api-hq.a
module: Library skynet/api/netmon $B/skynet/api/netmon/libpyskynet-api-netmon.a
module: Library skynet/api/qloud_dns $B/skynet/api/qloud_dns/libpyskynet-api-qloud_dns.a
module: Library skynet/api/samogon $B/skynet/api/samogon/libpyskynet-api-samogon.a
module: Library skynet/api/walle $B/skynet/api/walle/libpyskynet-api-walle.a
module: Library skynet/api/yp $B/skynet/api/yp/libpyskynet-api-yp.a
module: Library skynet/library/auth $B/skynet/library/auth/libpyskynet-library-auth.a
module: Library skynet/api/config $B/skynet/api/config/libpyskynet-api-config.a
```
### ya dump relation

Находит и показывает зависимость между модулем из **текущего каталога** и *target*-модулем.

Команда: `ya dump relation [option]... [target]...`

Опции:

- `-C` — модуль, от которого будет **собран** граф. По умолчанию берется модуль из текущего каталога.
- `--from` — стартовый таргет, от которого будет **показана** цепочка зависимостей. По умолчанию проект, от которого собран граф.
- `--to` — имя модуля или директория, **до которой** будет показана цепочка зависимостей.
- `--recursive` — позволяет показать зависимости до какой-то произвольной директории/модуля/файла из *target*-директории.
- `-t`, `--force-build-depends` — при вычислении цепочки зависимостей учитывает зависимости тестов (`DEPENDS` и `RECURSE_FOR_TESTS`).
- `--ignore-recurses` — при вычислении зависимостей исключает зависимости по `RECURSE`.
- `--no-tools` — при вычислении зависимостей исключает зависимости от инструментов сборки.
- `--no-addincls` — при вычислении зависимостей исключает зависимости по `ADDINCL`.

Стоит отметить, что:
- Граф зависимостей строится для проекта в текущей директории. Это можно поменять опцией `-С`, опция `--from` только выбирает стартовую точку в этом графе.
- `target` — имя модуля как пишет `ya dump modules`, или директория проекта этого модуля.
- Флаг `--recursive` позволяет показать путь до одного произвольного модуля/директории/файла, находящегося в *target*-директории.

Стоит учитывать, что между модулями путей в графе зависимостей может быть несколько. Утилита находит и показывает один из них (произвольный).

**Примеры:**

Найти путь до директории `contrib/libs/libiconv`:
```bash
~/yatool/devtools/ymake$ ya dump relation contrib/libs/libiconv
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Library (Include): $B/devtools/ymake/include_parsers/libdevtools-ymake-include_parsers.a ->
Library (Include): $B/library/cpp/xml/document/libcpp-xml-document.a ->
Library (Include): $B/library/cpp/xml/init/libcpp-xml-init.a ->
Library (Include): $B/contrib/libs/libxml/libcontrib-libs-libxml.a ->
Directory (Include): $S/contrib/libs/libiconv
```

Найти путь до произвольной файловой ноды из `contrib/libs`:
```bash
~/yatool/devtools/ymake$ ya dump relation contrib/libs --recursive
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Directory (Include): $S/contrib/libs/linux-headers
```

### ya dump all-relations

Выводит в формате `dot` или `json` все зависимости во внутреннем графе между *source* (по умолчанию — всеми целями из текущего каталога) и *target*.

Команда: `ya dump all-relations [option]... [--from <source>] --to <target>`

Опции:

- `--from` — стартовый таргет, от которого будет показан граф.
- `--to` — имя модуля или директория проекта этого модуля.
- `--recursive` — позволяет показать зависимости до всех модулей из *target*-директории. Показывает пути до всех модулей, доступных по `RECURSE` из `target`, если `target` — это директория.
- `--show-targets-deps` — при включенной опции `-recursive` также показывает зависимости между всеми модулями из *target*-директории.
- `-t`, `--force-build-depends` — при вычислении зависимостей учитывает зависимости тестов (`DEPENDS` и `RECURSE_FOR_TESTS`).
- `--ignore-recurses` — при вычислении зависимостей исключает зависимости по `RECURSE.`
- `--no-tools` — при вычислении зависимостей исключает зависимости от тулов.
- `--no-addincls` — при вычислении зависимостей исключает зависимости по `ADDINCL`.
- `--json` — выводит все зависимости в формате JSON.

**Пример:**

```bash
~/yatool/devtools/ymake/bin$ ya dump all-relations --to contrib/libs/libiconv | dot -Tpng > graph.png
```

С помощью опции `--from` можно поменять начальную цель:

```bash
~/yatool/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv | dot -Tpng > graph.png
```

Опции `--from` и `--to` можно указывать по несколько раз. Так можно посмотреть на фрагмент внутреннего графа, а не рисовать его целиком с `ya dump dot-graph`.

С помощью опции `--json` можно изменить формат вывода:

```bash
~/yatool/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv --json > graph.json
```

С помощью опции `--recursive` можно вывести все зависимости до всех модулей из *target*-директории:

```bash
~/yatool/devtools/ymake/symbols$ ya dump all-relations --to library/cpp/on_disk --recursive | dot -Tpng > graph2.png
```

### ya dump dot-graph

Выводит в формате `dot` все зависимости данного проекта. Это аналог `ya dump modules` c нарисованными зависимости между модулями.

Команда: `ya dump dot-graph [OPTION]... [TARGET]...`
``` bash
ya dump dot-graph
...
"ydb/library/ydb_issue/proto/libpy3library-ydb_issue-proto.a" -> "contrib/python/grpcio/libpy3contrib-python-grpcio.a";
    "ydb/library/ydb_issue/proto/libpy3library-ydb_issue-proto.a" -> "library/cpp/resource/liblibrary-cpp-resource.a";
    "ydb/core/protos/libpy3ydb-core-protos.a" -> "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a";
    "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a" -> "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libscheme-statistics-protos.a";
    "ydb/core/tx/columnshard/engines/scheme/statistics/protos/libpy3scheme-statistics-protos.a" -> "build/platform/python/ymake_python3/platform-python-ymake_python3.pkg.fake";
...
```
### ya dump dep-graph и json-dep-graph

Выводит граф зависимостей во внутреннем формате (с отступами) или в формате JSON соответственно.

Команда:
- `ya dump dep-graph [OPTION]... [TARGET]...`
- `ya dump json-dep-graph [OPTION]... [TARGET]...`

Для `ya dump dep-graph` доступны опции `--flat-json` и `--flat-json-files`. С помощью этих опций можно получить JSON-формат `dep` графа. Он выглядит как плоский список дуг и список вершин.

Граф зависимостей обычно содержит файловые ноды и ноды команд. Опция `--flat-json-files` позволяет вывести только файловые ноды и зависимости между ними.

### ya dump build-plan

Выводит в форматированный JSON граф сборочных команд примерно соответствующий тому, что будет исполняться при запуске команды `ya make`.
Более точный граф можно получить запустив `ya make -j0 -k -G`

Команда:
`ya dump build-plan [OPTION]... [TARGET]...`

Многие опции фильтрации не применимы к графу сборочных команд и поэтому не поддерживаются.
```bash
ya dump build-plan
Traceback (most recent call last):
  File "devtools/ya/app/__init__.py", line 733, in configure_exit_interceptor
    yield
  File "devtools/ya/app/__init__.py", line 107, in helper
    return action(args, **kwargs)
           ^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/entry/entry.py", line 48, in do_main
    res = handler.handle(handler, args, prefix=['ya'])
          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/handler.py", line 222, in handle
    return handler.handle(self, args[1:], prefix + [name])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/dispatch.py", line 38, in handle
    return self.command().handle(root_handler, args, prefix)
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "devtools/ya/core/yarg/handler.py", line 222, in handle
    return handler.handle(self, args[1:], prefix + [name])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```
### ya dump loops и peerdir-loops

Команды выводят циклы по зависимостям между файлами или проектами.

Команда:

- `ya dump loops [OPTION]... [TARGET]...` — отображает любые зависимости, включая циклы по включениям (инклудам) между заголовочными файлами.
- `ya dump peerdir-loops [OPTION]... [TARGET]...` — отображает только зависимости по `PEERDIR` между проектами.

### ya dump compile-commands и compilation-database

Выводит список JSON-описаний сборочных команд через запятую. Каждая команда включает три свойства: `"command"`, `"directory"` и `"file"`.
В параметре `command` указаны используемый компилятор, целевая платформа, зависимости и другая информация. В `directory` папка проекта и в `file` сам файл.

Команда:
`ya dump compile-commands [OPTION]... [TARGET]...`

Часто используемые опции:
- `-q, --quiet` — ничего не выводить.
- `--files-in=FILE_PREFIXES` — выдать только команды с подходящими префиксом относительно корня репозитория у `"file"`.
- `--files-in-targets=PREFIX` — фильтровать по префиксу `"directory"`.
- `--no-generated` — исключить команды обработки генерированных файлов.
- `--cmd-build-root=CMD_BUILD_ROOT` — использьзовать путь как сборочную директорию в командах.
- `--cmd-extra-args=CMD_EXTRA_ARGS` — добавить опции в команды.

Также поддержано большинство сборочных опций `ya make`:
- `--rebuild` — применяется для выполнения полной пересборки проекта, игнорируя все ранее сгенерированные результаты и выводит список описаний сборочных команд. Удаляются все промежуточные и итоговые файлы, созданные во время прошлых процессов сборки, чтобы исключить влияние устаревших или некорректных данных на новую сборку.
- `-C=BUILD_TARGETS`, `--target=BUILD_TARGETS` — применяются для установки конкретных целей сборки, задаваемых значением `BUILD_TARGETS`, и выводят список описаний сборочных команд в формате JSON.
- `-j=BUILD_THREADS`, `--threads=BUILD_THREADS` — используются для указания число рабочих потоков сборки, которые должны быть задействованы в процессе сборки проекта.
- `--clear` — используется для удаления временных данных и результатов предыдущих сборок, которые находятся в директории проекта, и выводит список описаний сборочных команд.
- `--add-result=ADD_RESULT` — предназначена для управления результатами, выдаваемыми системой сборки. Дает возможность точно определить, какие файлы следует включать в итоговый набор результатов.
- `--add-protobuf-result` — позволяет системе сборки явно получить сгенерированный исходный код Protobuf для соответствующих языков программирования.
- `--add-flatbuf-result` — предназначена для того, чтобы система сборки могла автоматически получить сгенерированный исходный код выходных файлов FlatBuffers (flatc) для соответствующих языков программирования.
- `--replace-result` — позволяет собирать только цели, указанные в `--add-result` и вывести список описаний сборочных команд.
- `--force-build-depends` — обеспечивает комплексную подготовку к тестированию за счет принудительной сборки всех зависимостей, объявленных в `DEPENDS`.
- `-R` и `--ignore-recurses` — предотвращает автоматическую сборку проектов, объявленных с использованием макроса `RECURSE` в файлах `ya.make`.
- `--no-src-links` — позволяет не создавать символические ссылки в исходной директории и вывести список описаний сборочных команд.
- `--stat` — используется для отображения статистики выполнения сборки.
- `-v` или `--verbose` —  служит для активации режима подробного вывода.
- `-T` — служит для вывода статуса сборки в построчном режиме.
- `-D=FLAGS` — позволяет определять или переопределять переменные среды сборки (сборочные флаги), задавая их имя и значение.
- `--cache-stat` — позволяет перед сборкой выдать статистику по наполнению локального кеша и вывести список описаний сборочных команд.
- `--gc` — дает указание системе пройтись по локальному кешу, очистить его от ненужных данных и только после этого начать выполнение сборки c выводом описаний сборочных команд.
- `--gc-symlinks` — предназначена для очистки кеша сборки, фокусируясь на удалении символических ссылок.

**Пример:**
```bash
~/yatool$ ya dump compilation-database devtools/ymake/bin
...
{
    "command": "clang++ --target=x86_64-linux-gnu --sysroot=/home/spreis/.ya/tools/v4/244387436 -B/home/spreis/.ya/tools/v4/244387436/usr/bin -c -o /home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp.o /home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp -I/home/spreis/yatool -I/home/spreis/yatool -I/home/spreis/yatool/contrib/libs/linux-headers -I/home/spreis/yatool/contrib/libs/linux-headers/_nf -I/home/spreis/yatool/contrib/libs/cxxsupp/libcxx/include -I/home/spreis/yatool/contrib/libs/cxxsupp/libcxxrt -I/home/spreis/yatool/contrib/libs/zlib/include -I/home/spreis/yatool/contrib/libs/double-conversion/include -I/home/spreis/yatool/contrib/libs/libc_compat/include/uchar -fdebug-prefix-map=/home/spreis/yatool=/-B -Xclang -fdebug-compilation-dir -Xclang /tmp -pipe -m64 -g -ggnu-pubnames -fexceptions -fstack-protector -fuse-init-array -faligned-allocation -W -Wall -Wno-parentheses -Werror -DFAKEID=5020880 -Dyatool_ROOT=/home/spreis/yatool -Dyatool_BUILD_ROOT=/home/spreis/yatool -D_THREAD_SAFE -D_PTHREADS -D_REENTRANT -D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES -D_LARGEFILE_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -UNDEBUG -D__LONG_LONG_SUPPORTED -DSSE_ENABLED=1 -DSSE3_ENABLED=1 -DSSSE3_ENABLED=1 -DSSE41_ENABLED=1 -DSSE42_ENABLED=1 -DPOPCNT_ENABLED=1 -DCX16_ENABLED=1 -D_libunwind_ -nostdinc++ -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt -mcx16 -std=c++17 -Woverloaded-virtual -Wno-invalid-offsetof -Wno-attributes -Wno-dynamic-exception-spec -Wno-register -Wimport-preprocessor-directive-pedantic -Wno-c++17-extensions -Wno-exceptions -Wno-inconsistent-missing-override -Wno-undefined-var-template -Wno-return-std-move -nostdinc++",
    "directory": "/home/spreis/yatool",
    "file": "/home/spreis/yatool/library/cpp/json/fast_sax/parser.rl6.cpp"
},
...
```
### ya dump groups

Выводит информацию в формате JSON обо всех участников или выбранных группах, включая имя, участников и почтовый список рассылки.

Команда: `ya dump groups [OPTION]... [GROUPS]...`

Опции:
- `--all_groups` — вся информация о группах (default).
- `--groups_with_users` — только информация об участниках.
- `--mailing_lists` — только списки рассылки.

### ya dump json-test-list

Выводит форматированный JSON с информацией о тестах.

Команда: `ya dump json-test-list [OPTION]... [TARGET]...`

### ya dump recipes

Выводит форматированный JSON с информацией о рецептах, используемых в тестах.

Команда: `ya dump recipes [OPTION]... [TARGET]...`

Опции:
- `--json` — выдать информацию о рецептах в формате JSON.
- `--skip-deps` — только рецепты заданного проекта, без рецептов зависимых.
- `--help` — список всех опций.

### ya dump conf-docs

Генерирует документацию по модулям и макросам в формате Markdown (для чтения) или JSON (для автоматической обработки).

Команда: `ya dump conf-docs [OPTIONS]... [TARGET]...`

Опции:
- `--dump-all` — выводит информацию, включающую внутренние модули и макросы, которые нельзя использовать в `ya.make`.
- `--json` — выводит информацию обо всех модулях и макросах, включая внутренние, в формате JSON.

### ya dump debug

Собирает отладочную информацию о последнем запуске `ya make`. При локальной работе используется с опцией `--dry-run`.

Команда: `ya dump debug [last|N] --dry-run`

- `ya dump debug` — посмотреть все доступные `bundle`.
- `ya dump debug last` — cобрать `bundle` от последнего запуска `ya make`.
- `ya dump debug 2` — cобрать **пред**последний `bundle`.
- `ya dump debug 1` — cобрать последний `bundle`.

**Пример:**
```bash
┬─[user@linux:~/a/yatool]─[11:50:28]
╰─>$ ./ya dump debug

10: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-17 20:16:24 (v1)
9: `ya-bin make devtools/dummy_yatool/hello_world/ --stat`: 2021-06-17 20:17:06 (v1)
8: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-17 20:17:32 (v1)
7: `ya-bin make devtools/dummy_yatool/hello_world/ --stat`: 2021-06-17 20:18:14 (v1)
6: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-18 12:28:15 (v1)
5: `ya-bin make -r /Users/yatool/devtools/ya/test/programs/test_tool/bin -o /Users/build/ya --use-clonefile`: 2021-06-18 12:35:17 (v1)
4: `ya-bin make -A devtools/autocheck/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 12:51:51 (v1)
3: `ya-bin make -A devtools/autocheck/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 13:04:08 (v1)
2: `ya-bin make -r /Users/yatool/devtools/ya/bin -o /Users/build/ya --use-clonefile`: 2021-06-21 10:26:31 (v1)
1: `ya-bin make -r /Users/yatool/devtools/ya/test/programs/test_tool/bin -o /Users/build/ya --use-clonefile`: 2021-06-21 10:36:21 (v1)
```
#### Дополнительные параметры

В контексте использования команды `ya dump` и ее подкоманд дополнительные параметры `[TARGET]` представляют собой специфические значения или данные, которые необходимо предоставить вместе с командой для ее корректного выполнения.

В отличие от опций, изменяющих поведение команды, дополнительные параметры указывают на конкретные объекты, такие как модули или директории, над которыми выполняется операция, например, `--target=<path>`.
