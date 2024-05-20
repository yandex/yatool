# ya dump

Команда `ya dump` разработана для извлечения и предоставления детализированной информации о различных аспектах системы сборки и репозитория. 

Эта информация может включать в себя сведения о сборочном графе, зависимостях между модулями, конфигурационных параметрах и многом другом.

### Синтаксис

Общий формат команды выглядит следующим образом:
```
ya dump <subcommand> [OPTION] [TARGET]
```
Где:
- `ya dump` является основным вызовом команды и указывает на то, что вы хотите использовать функциональность извлечения данных о проекте.
- `<subcommand>` обозначает конкретное действие или отчет, который вы хотите получить.
Например, modules, dep-graph, conf-docs и т.д. Каждая подкоманда имеет своё предназначение и набор дополнительных параметров и опций.
- `[OPTION]` (необязательный) — это дополнительные флаги или ключи, которые модифицируют поведение выбранной подкоманды. Опции позволяют настроить вывод команды, уточнить, какие данные необходимо извлечь, или изменить формат вывода.
- `[TARGET]` (необязательный) — это дополнительные параметры, необходимые для выполнения некоторых подкоманд, могут включать пути к файлам, названия модулей или другие специфические данные, необходимые для выполнения команды.

Вызов [справки](helpyadump.md) по всем доступным подкомандам:
```
ya dump --help
```
### Подкоманды `<subcommand>`

Подкоманды ya dump выводят информацию о сборочном графе, а также позволяют получить информацию о системе сборки.

Первая группа подкоманд может быть полезна для анализа зависимостей между модулями, а также для поиска проблем. К ней относятся:

- `ya dump modules` – список зависимых модулей.
- `ya dump relation` – зависимость между двумя модулями.
- `ya dump all-relations` – все зависимости между двумя модулями.
- `ya dump dot-graph` – граф всех межмодульных зависимостей данного проекта.
- `ya dump dep-graph`, `ya dump json-dep-graph` – граф зависимостей системы сборки.
- `ya dump build-plan` – граф сборочных команд.
- `ya dump loops`, `ya dump peerdir-loops` – информация о циклах в графе зависимостей.
- `ya dump compile-commands`, `ya dump compilation-database` – информация о сборочных командах (compilation database).

По умолчанию вывод этих подкоманд основан на графе обычной сборки без тестов.  

Для поиска информации с учётом сборки тестов надо добавить опцию `-t`, например, `ya dump modules -t`.

Вторая группа - это различная информация от системы сборки. К ней относятся:

- ya dump groups – группы владельцев проектов.
- ya dump json-test-list – информация о тестах.
- ya dump recipes – информация о поднимаемых рецептах.
- ya dump conf-docs - документация по макросам и модулям.
- ya dump debug — сборка отладочного bundle.





### ya dump modules {#modules}

Показывает список всех зависимостей для цели *target* (текущий каталог, если не задана явно).

Команда: `ya dump modules [option]... [target]...`

**Пример:**
```
spreis@starship:~/ws/arcadia$ ./ya dump modules devtools/ymake | grep sky
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

### ya dump relation {#relation}

Находит и показывает зависимость между модулем из **текущего каталога** и *target*-модулем. 

Команда: `ya dump relation [option]... [target]...`

Опции:

* `-C` - модуль, от которого будет **собран** граф. По умолчанию берётся модуль из текущего каталога.
* `--from` - стартовый таргет, от которого будет **показана** цепочка зависимостей. По умолчанию проект, от которого собран граф.
* `--to` - имя модуля или директория, до которой будет показана цепочка зависимостей
* `--recursive` - позволяет показать зависимости до какой-то произвольной директории/модуля/файла из *target*-директории
* `-t`, `--force-build-depends` - при вычислении цепочки зависимостей учитывает зависимости тестов (`DEPENDS` и `RECURSE_FOR_TESTS`)
* `--ignore-recurses` - при вычислении зависимостей исключает зависимости по `RECURSE`
* `--no-tools` - при вычислении зависимостей исключает зависимости от тулов
* `--no-addincls` - при вычислении зависимостей исключает зависимости по `ADDINCL`

{% note info %}

* Граф зависимостей строится для проекта в текущей директории. Это можно поменять опцией `-С`, опция `--from` только выбирает стартовую точку в этом графе.
* `target` - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
* Флаг `--recursive` позволяет показать путь до одного произвольного модуля/директории/файла, находящегося в *target*-директории.

{% endnote %}


{% note warning %}

Между модулями путей в графе зависимостей может быть несколько. Утилита находит и показывает один из них (произвольный).

{% endnote %}

**Примеры:**

Найти путь до директории `contrib/libs/libiconv`:

```
~/arcadia/devtools/ymake$ ya dump relation contrib/libs/libiconv
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Library (Include): $B/devtools/ymake/include_parsers/libdevtools-ymake-include_parsers.a ->
Library (Include): $B/library/cpp/xml/document/libcpp-xml-document.a ->
Library (Include): $B/library/cpp/xml/init/libcpp-xml-init.a ->
Library (Include): $B/contrib/libs/libxml/libcontrib-libs-libxml.a ->
Directory (Include): $S/contrib/libs/libiconv

```

Найти путь до произвольной файловой ноды из `contrib/libs`:

```
~/arcadia/devtools/ymake$ ya dump relation contrib/libs --recursive
Directory (Start): $S/devtools/ymake ->
Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
Directory (Include): $S/contrib/libs/linux-headers
```

### ya dump all-relations {#all-relations}

Выводит в формате dot или json все зависимости во внутреннем графе между *source* (по умолчанию – всеми целями из текущего каталога) и *target*.

Команда: `ya dump all-relations [option]... [--from <source>] --to <target>`

Опции:

* `--from` - стартовый таргет, от которого будет показан граф
* `--to` - имя модуля или директория проекта этого модуля
* `--recursive` - позволяет показать зависимости до всех модулей из *target*-директории
* `--show-targets-deps` - при включенной опции `-recursive` также показывает зависимости между всеми модулями из *target*-директории
* `-t`, `--force-build-depends` - при вычислении зависимостей учитывает зависимости тестов (`DEPENDS` и `RECURSE_FOR_TESTS`)
* `--ignore-recurses` - при вычислении зависимостей исключает зависимости по `RECURSE`
* `--no-tools` - при вычислении зависимостей исключает зависимости от тулов
* `--no-addincls` - при вычислении зависимостей исключает зависимости по `ADDINCL`
* `--json` - выводит все зависимости в формате *json*

{% note info %}

* Граф зависимостей строится для проекта в текущей директории. Это можно поменять опцией `-С`, опция `--from` только выбирает стартовую точку в этом графе.
* `target` - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
* Флаг `--recursive` позволяет показать пути до всех модулей, доступных по `RECURSE` из `target`, если `target` - это директория.

{% endnote %}

**Пример:**

```
~/arcadia/devtools/ymake/bin$ ya dump all-relations --to contrib/libs/libiconv | dot -Tpng > graph.png
```
![graph](../assets/all-relations-graph1.png "Граф зависимостей" =558x536)

С помощью опции `--from` можно поменять начальную цель:

```
~/arcadia/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv | dot -Tpng > graph.png
```

![graph](../assets/all-relations-graph2.png "Граф зависимостей" =300x268)

Опции `--from` и `--to` можно указывать по несколько раз. Так можно посмотреть на фрагмент внутреннего графа, а не рисовать его целиком с `ya dump dot-graph`.

С помощью опции `--json` можно изменить формат вывода:

```
~/arcadia/devtools/ymake/bin$ ya dump all-relations --from library/cpp/xml/document/libcpp-xml-document.a --to contrib/libs/libiconv --json > graph.json
```

Получившийся граф: [https://paste.yandex-team.ru/11356389](https://paste.yandex-team.ru/11356389)

С помощью опции `--recursive` можно вывести все зависимости до всех модулей из *target*-директории:

```
~/arcadia/devtools/ymake/symbols$ ya dump all-relations --to library/cpp/on_disk --recursive | dot -Tpng > graph2.png
```

![graph](../assets/all-relations-graph3.png "Граф зависимостей" =657x327)

{% note info %}

Опцию `--recursive` можно использовать, чтобы получить все проекты из arcadia, которые зависят от вашего. Для этого, нужно запустить ya dump all-relations из директории `autocheck`. Эта процедура займет время, так как нужно будет сконфигурировать всю аркадию.

Пример:

```
~/arcadia/autocheck$ ya dump all-relations --to kikimr --recursive --json > graph.json

```

{% endnote %}


### ya dump dot-graph {#dot-graph}

Выводит в формате dot все зависимости данного проекта. Это аналог `ya dump modules` c нарисованными зависимости между модулями.

Команда: `ya dump dot-graph [OPTION]... [TARGET]...`


### ya dump dep-graph/json-dep-graph {#dep-graph}

Выводит во внутреннем формате (с отступами) или в форматированный JSON граф зависимостей ymake.

Команда:
`ya dump dep-graph [OPTION]... [TARGET]...`
`ya dump json-dep-graph [OPTION]... [TARGET]...`

Для `ya dump dep-graph` доступны опции `--flat-json` и `--flat-json-files`. С помощью этих опций можно получить json-формат dep графа. Он выглядит как плоский список дуг и список вершин.
Граф зависимостей обычно содержит файловые ноды и ноды команд. Опция `--flat-json-files` позволяет вывести только файловые ноды и зависимости между ними.

### ya dump build-plan {#build-plan}

Выводит в форматированный JSON граф сборочных команд примерно соответствующий тому, что будет исполняться при запуске команды [ya make](https://wiki.yandex-team.ru/yatool/make/).
Более точный граф можно получить запустив `ya make -j0 -k -G`

Команда:
`ya dump build-plan [OPTION]... [TARGET]...`

{% note warning %}

Многие опции фильтрации не имеют смысла для графа сборочных команд и тут не поддерживаются.

{% endnote %}

### ya dump loops/peerdir-loops {#loops}

Выводит циклы по зависимостям между файлами или проектами. ya dump peerdir-loops - только зависимости по PEERDIR между проектами, ya dump loops - любые зависимости включая циклы по инклудам между хедерами.

{% note alert %}

Циклы по PEERDIR в Аркадии запрещены и в здоровом репозитории их быть не должно.

{% endnote %}

Команда:
`ya dump loops [OPTION]... [TARGET]...`
`ya dump peerdir-loops [OPTION]... [TARGET]...`

### ya dump compile-commands/compilation-database {#compile}

Выводит через запятую список JSON-описаний сборочных команд. Каждая команда состоит из 3х свойств: `"command"`, `"directory"`, `"file"`.

Команда:
`ya dump compile-commands [OPTION]... [TARGET]...`

Опции:
* `-q, --quiet` - ничего не выводить. Можно использовать для селективного чекаута в svn.
* `--files-in=FILE_PREFIXES` - выдать только команды с подходящими префиксом относительно корня Аркадии у `"file"`
* `--files-in-targets=PREFIX` - фильтровать по префиксу `"directory"`
* `--no-generated` - исключить команды обработки генерированных файлов
* `--cmd-build-root=CMD_BUILD_ROOT` - использьзовать путь как сборочную директорию в командах
* `--cmd-extra-args=CMD_EXTRA_ARGS` - добавить опции в команды
* Поддержано большинство сборочных опций [`ya make`](./ya_make/index.md)

**Пример:**
```
~/ws/arcadia$ ya dump compilation-database devtools/ymake/bin
...
{
    "command": "clang++ --target=x86_64-linux-gnu --sysroot=/home/spreis/.ya/tools/v4/244387436 -B/home/spreis/.ya/tools/v4/244387436/usr/bin -c -o /home/spreis/ws/arcadia/library/cpp/json/fast_sax/parser.rl6.cpp.o /home/spreis/ws/arcadia/library/cpp/json/fast_sax/parser.rl6.cpp -I/home/spreis/ws/arcadia -I/home/spreis/ws/arcadia -I/home/spreis/ws/arcadia/contrib/libs/linux-headers -I/home/spreis/ws/arcadia/contrib/libs/linux-headers/_nf -I/home/spreis/ws/arcadia/contrib/libs/cxxsupp/libcxx/include -I/home/spreis/ws/arcadia/contrib/libs/cxxsupp/libcxxrt -I/home/spreis/ws/arcadia/contrib/libs/zlib/include -I/home/spreis/ws/arcadia/contrib/libs/double-conversion/include -I/home/spreis/ws/arcadia/contrib/libs/libc_compat/include/uchar -fdebug-prefix-map=/home/spreis/ws/arcadia=/-B -Xclang -fdebug-compilation-dir -Xclang /tmp -pipe -m64 -g -ggnu-pubnames -fexceptions -fstack-protector -fuse-init-array -faligned-allocation -W -Wall -Wno-parentheses -Werror -DFAKEID=5020880 -DARCADIA_ROOT=/home/spreis/ws/arcadia -DARCADIA_BUILD_ROOT=/home/spreis/ws/arcadia -D_THREAD_SAFE -D_PTHREADS -D_REENTRANT -D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES -D_LARGEFILE_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -UNDEBUG -D__LONG_LONG_SUPPORTED -DSSE_ENABLED=1 -DSSE3_ENABLED=1 -DSSSE3_ENABLED=1 -DSSE41_ENABLED=1 -DSSE42_ENABLED=1 -DPOPCNT_ENABLED=1 -DCX16_ENABLED=1 -D_libunwind_ -nostdinc++ -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt -mcx16 -std=c++17 -Woverloaded-virtual -Wno-invalid-offsetof -Wno-attributes -Wno-dynamic-exception-spec -Wno-register -Wimport-preprocessor-directive-pedantic -Wno-c++17-extensions -Wno-exceptions -Wno-inconsistent-missing-override -Wno-undefined-var-template -Wno-return-std-move -nostdinc++",
    "directory": "/home/spreis/ws/arcadia",
    "file": "/home/spreis/ws/arcadia/library/cpp/json/fast_sax/parser.rl6.cpp"
},
...
```

### ya dump groups {#groups}

Выводит в виде JSON информацию о всех или выбранных группах (имя, участников, почтовый список рассылки)

Команда: `ya dump groups [OPTION]... [GROUPS]...`

Опции:
* `--all_groups` - вся информация о группах (default)
* `--groups_with_users` - только информация об участниках
* `--mailing_lists` - только списки рассылки

### ya dump json-test-list – информация о тестах {#test-list}

Выводит форматированный JSON с информацией о тестах.

Команда: `ya dump json-test-list [OPTION]... [TARGET]...`

Опции:
* `--help` - список всех опций

### ya dump recipes – информация о поднимаемых рецептах {#recipes}

Выводит форматированный JSON с информацией о рецептах (тестовых окружениях), поднимаемых в тестах

Команда: `ya dump recipes [OPTION]... [TARGET]...`

Опции:
* `--json` - выдать информацию о рецептах в формате JSON
* `--skip-deps` - только рецепты заданного проекта, без рецептов зависимых
* `--help` - список всех опций

### ya dump conf-docs {#docs}

Генерирует документацию по модулям и макросам в формате Markdown (для чтения) или JSON (для автоматической обработки).

Документация автоматически генерируется и коммитится раз в день в Аркадию по адресам:

* [arcadia/build/docs/readme.md](https://a.yandex-team.ru/arc/trunk/arcadia/build/docs/readme.md) - публичные макросы и модули
* [arcadia/build/docs/all.md](https://a.yandex-team.ru/arc/trunk/arcadia/build/docs/all.md) - все макросы и модули

Команда: `ya dump conf-docs [OPTIONS]... [TARGET]...`

Опции:
* `--dump-all` - включая внутренние модули и макросы, которые нельзя использовать в `ya.make`
* `--json` - информация обо всех модулях и макросах, включая внутренние, в формате JSON


### ya dump debug {#debug}
Собирает отладочную информацию о последнем запуске `ya make` и загружает её на sandbox

Команда: `ya dump debug [last|N] [OPTION]`

`ya dump debug` — посмотреть все доступные для загрузки bundle
`ya dump debug last --dry-run` — cобрать bundle от последнего запуска `ya make`, но не загружать его на sandbox
`ya dump debug 2` — cобрать **пред**последний bundle и загрузить его на sandbox
`ya dump debug 1` — cобрать последний bundle и загрузить его на sandbox

Опции:
* `--dry-run` — собрать отладочный bundle от последнего запуска, но не загружать его на sandbox

**Пример:**
```
┬─[v-korovin@v-korovin-osx:~/a/arcadia]─[11:50:28]
╰─>$ ./ya dump debug

10: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-17 20:16:24 (v1)
9: `ya-bin make devtools/dummy_arcadia/hello_world/ --stat`: 2021-06-17 20:17:06 (v1)
8: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-17 20:17:32 (v1)
7: `ya-bin make devtools/dummy_arcadia/hello_world/ --stat`: 2021-06-17 20:18:14 (v1)
6: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-18 12:28:15 (v1)
5: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/test/programs/test_tool/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-18 12:35:17 (v1)
4: `ya-bin make -A devtools/ya/yalibrary/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 12:51:51 (v1)
3: `ya-bin make -A devtools/ya/yalibrary/ggaas/tests/test_like_autocheck -F test_subtract.py::test_subtract_full[linux-full]`: 2021-06-18 13:04:08 (v1)
2: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-21 10:26:31 (v1)
1: `ya-bin make -r /Users/v-korovin/arc/arcadia/devtools/ya/test/programs/test_tool/bin -o /Users/v-korovin/arc/build/ya --use-clonefile`: 2021-06-21 10:36:21 (v1)
```


### ya dump --help  {#help}

Выводит список всех доступных команд - есть ещё ряд команд кроме описанных выше, но большинство из них используются внутри сборочной инфраструктуры и мало интересны.
Про каждую можно спросить `--help` отдельно.

Команда: `ya dump --help`






Чтобы просмотреть список всех доступных опций для конкретной подкоманды утилиты `ya dump`, можно использовать параметр `--help` сразу после указания интересующей подкоманды.

Это выведет подробную справку по опциям и их использованию для выбранной подкоманды.

Пример команды для просмотра опций:
```
ya dump <subcommand> --help
```
Где <subcommand> нужно заменить на конкретное имя подкоманды, для которой вы хотите получить дополнительную информацию. 

Например, если вы хотите узнать все доступные опции для подкоманды modules, команда будет выглядеть так:
```
ya dump modules --help
```




#### Дополнительные параметры 

В контексте использования команды ya dump и её подкоманд, дополнительные параметры `[TARGET]` представляют собой специфические значения или данные, которые необходимо предоставить вместе с командой для её корректного выполнения. 

В отличие от опций, которые модифицируют поведение команды, дополнительные параметры обычно указывают на конкретные объекты действия (например, файлы, модули, директории), над которыми будет выполняться операция.

- `--target=<path>`: Указание конкретного модуля или директории, для которой следует выполнить операцию. Актуально для подкоманд, работающих с конкретными целями сборки.






