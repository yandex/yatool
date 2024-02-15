# ya dump

Получить различную информацию о репозитории и сборке.

`ya dump <subcommand> [OPTIONS]`

Команды `ya dump` выводят информацию о сборочном графе, а также позволяют получить информацию о системе сборки.

Первая группа команд может быть полезна для анализа зависимостей между модулями, а также для поиска проблем. К ней относятся

* ya dump modules – список зависимых модулей
* ya dump relation](#relation)  – зависимость между двумя модулями
* ya dump all-relations](#all-relations)  – все зависимости между двумя модулями
* ya dump dot-graph](#dot-graph) – граф всех межмодульных зависимостей данного проекта
* ya dump dep-graph, ya dump json-dep-graph](#dep-graph)  – граф зависимостей системы сборки
* ya dump build-plan](#build-plan) – граф сборочных команд
* ya dump loops, ya dump peerdir-loops](#loops) – информация о циклах в графе зависимостей
* ya dump compile-commands, ya dump compilation-database](#compile) – информация о сборочных командах (compilation database).

По умолчанию вывод этих команд основан на графе **обычной сборки** (ака `ya make`, т.е. без тестов). Для поиска информации с учётом сборки тестов (ака `ya make -t`)
надо добавить аналогичную опцию, например `ya dump modules -t`.

Вторая группа - это различная информация от системы сборки. К ней относятся:

* ya dump groups – группы владельцев проектов
* ya dump json-test-list – информация о тестах
* ya dump recipes – информация о поднимаемых рецептах
* ya dump conf-docs - документация по макросам и модулям
* ya dump debug — сборка отладочного bundle
* ya dump --help - справка по командам `ya dump`

## Доступные команды
```
 all-relations             All relations between internal graph nodes in dot format
 atd-revisions             Dump revisions of trunk/arcadia_tests_data
 build-plan                Build plan
 compilation-database      Alias for compile-commands
 compile-commands          JSON compilation database
 conf-docs                 Print descriptions of entities (modules, macros, multimodules, etc.)
 dep-graph                 Dependency internal graph
 dir-graph                 Dependencies between directories
 dot-graph                 Dependency between directories in dot format
 files                     File list
 groups                    Owner groups (from arcadia/groups)
 imprint                   Directory imprint
 json-dep-graph            Dependency graph as json
 json-test-list            All test entries as json
 loops                     All loops in arcadia
 module-info               Modules info
 modules                   All modules
 peerdir-loops             Loops by peerdirs
 relation                  PEERDIR relations
 root                      Print Arcadia root
 test-list                 All test entries
```

