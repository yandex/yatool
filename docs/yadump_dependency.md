## ya dump для анализа зависимостей

Команда `ya dump` представляет собой инструмент для анализа и управления зависимостями в системе сборки проектов. 

В графе зависимостей системы сборки представлены разнообразные зависимости, в том числе между проектами (PEERDIR) и директориями (RECURSE), получаемые из `ya.make-файлов` и исходного кода.
Система сборки использует их, чтобы построить граф команд сборки для разных ситуаций, однако часть информации о зависимостях теряется. `ya dump` собирает информацию обходом графа зависимостей системы сборки. Чтобы смоделировать ту или иную ситуацию, нужно исключать из обхода графа разные виды зависимостей.

#### Виды зависимостей

1. PEERDIR: Основная зависимость между модулями, аналогичная import-зависимости в модульных языках.
2. RECURSE: Зависимость между директориями.
3. TOOL: Инструментальная зависимость, упоминаемая в макросах RUN_PROGRAM или параметре TOOL.
4. BUNDLE: Зависимость от модуля как от файла.
5. DEPENDS: Зависимость для исполнения, результат сборки модуля нужен для запуска теста.

Основные цели и задачи использования `ya dump`:

1. Анализ зависимостей между модулями и программами:
`ya dump` позволяет выявить, какие модули зависят друг от друга, и каким образом эти зависимости влияют на процесс сборки.
2. Фильтрация зависимостей:
С помощью различных опций (`--ignore-recurses`, `--no-tools` и т.д.) можно детализировать или упрощать граф зависимостей.
3. Отображение зависимости файлов:
Позволяет получить информацию о зависимостях на уровне файлов, что важно для понимания, как изменения в одном файле могут повлиять на другие.
4. Поддержка различных языков и платформ:
`ya dump` работает с зависимостями для разных языков программирования (например, C++, Java, Python, Go) и учитывает их особенности, такие как логические и сборочные зависимости.

Запустив команду `ya make <path>` или `ya make -t <path>`, можно узнать, какие модули будут собраны:

1. Целями сборки будут: ваш модуль и все модули, достижимые от вашего модуля только по `RECURSE`.
2. Для всех целей соберутся также все модули, от которых они зависят по PEERDIR, включая программы, используемые для кодогенерации и запускаемые через `RUN_PROGRAM`.
3. `RECURSE` от зависимых модулей в сборке не участвуют.
4. С опцией `-t` или `--force-build-depends` к целям сборки добавятся тесты, достижимые по `RECURSE_FOR_TESTS`, а также `DEPENDS` - зависимости ваших тестов.


#### Основные команды

- ya dump modules:
Позволяет получить список всех зависимостей для заданного модуля или директории проекта. Можно настроить фильтрацию, чтобы отобразить только интересующие зависимости.

- ya dump relation:
Помогает понять, каким образом один модуль зависит от другого, показывая один из возможных путей зависимости.

- ya dump all-relations:
Похожая команда, но имеет более детализированный выход в виде графа, показывающего все возможные пути зависимости.

### Сценарии использования

Использование утилиты ya dump включает несколько типичных сценариев, которые помогут вам эффективно анализировать и управлять зависимостями в ваших проектах. Ниже описаны наиболее распространённые сценарии, начиная с базовых команд и фильтрации зависимостей, и заканчивая более сложными анализами.

#### Анализ зависимостей проекта

1. Список всех зависимостей проекта

Для получения полного списка всех зависимостей проекта можно использовать команду:


   ya dump modules <path_to_project>


   Пример:


   ya dump modules devtools/bmake



2. Фильтрация зависимостей по директории

   Если вам нужно узнать зависимости только для определенной директории в проекте, можно воспользоваться фильтрацией:


   ya dump modules <path_to_project> | grep <dir>


   Пример:


   ya dump modules devtools/bmake | grep mapreduce/



3. Фильтрация зависимости по типам компонентов

   Чтобы получить список всех модулей, включая зависимости тестов, используйте опцию -t или --force-build-depends:


   ya dump modules <path_to_project> -t



#### Анализ зависимостей между модулями

1. Определение зависимости от модуля

   Чтобы узнать, каким образом проект зависит от конкретного модуля, используйте команду:


   ya dump relation <module_name>


   Пример:


   ya dump relation mapreduce/yt/interface



2. Нахождение всех путей зависимости до модуля

   Для поиска всех возможных путей зависимости до модуля можно использовать команду:


   ya dump all-relations <module_name>


   Пример:


   ya dump all-relations mapreduce/yt/interface



#### Что будет собираться через библиотеку (RECURSE)

1. Общий список для проекта без учёта рекурсивных зависимостей

   Чтобы узнать, что будет собираться, исключив зависимости по RECURSE, используйте команду:


   ya dump modules --ignore-recurses <path_to_project>



2. Определение зависимостей без рекурсии

   Для получения пути зависимости, исключая рекурсивные зависимости, используйте:


   ya dump relation --ignore-recurses <module_name>


   Пример:


   ya dump relation --ignore-recurses contrib/tools/python/bootstrap



#### Что попадёт в программу

1. Список всех библиотек, которые попадут в программу

   Для анализа всех библиотек, которые будут включены в программу или модуль через PEERDIR, без учёта инструментов сборки и рекурсивных зависимостей:


   ya dump modules --ignore-recurses --no-tools <path_to_project>



2. Определение конректных путей зависимости без рекурсии и инструментов

   Выяснить конкретные пути зависимости, исключая инструменты сборки:


   ya dump relation --ignore-recurses --no-tools <module_name>


   Пример:


   ya dump relation --ignore-recurses --no-tools contrib/tools/python/lib



#### Практические примеры

1. Пример 1: Анализ зависимостей конкретного проекта

   Допустим, у вас есть проект devtools/bmake, и вам нужно получить список всех его зависимостей:


   ya dump modules devtools/bmake


   Если требуется узнать все зависимости в определенной директории, например mapreduce:


   ya dump modules devtools/bmake | grep mapreduce



2. Пример 2: Использование ya dump для оптимизации сборочного процесса

   Вы можете оптимизировать сборочный процесс, исключив инструменты сборки:


   ya dump modules devtools/bmake --ignore-recurses --no-tools


   Это особенно полезно для сокращения времени сборки, исключая из анализа лишние зависимости.

Эти сценарии позволят вам эффективно использовать утилиту ya dump для анализа и управления зависимостями в проектах различного масштаба и сложности.

### Расширенные функции

Утилита ya dump предоставляет дополнительные возможности для более глубокого анализа зависимостей и понимания сборочного процесса. Эти функции позволяют гибко управлять процессом сборки, исключать из анализа ненужные зависимости и сосредотачиваться на ключевых компонентах проекта.

#### Что будет собираться через RECURSE

При использовании зависимости RECURSE важно понимать, какие модули будут включены в сборочный процесс. Ниже описаны правила и примеры анализа сборки через RECURSE.

1. Правила определения сборки через RECURSE

- Целями сборки будут ваш модуль и все модули, достижимые от вашего только по RECURSE.
- Для всех целей соберутся также все модули, от которых они зависят по PEERDIR, включая программы, используемые для кодогенерации, запускаемые через RUN_PROGRAM.
- RECURSE зависимых модулей в сборке не участвуют.

2. Пример анализа сборки

Чтобы узнать, что будет собираться при использовании RECURSE, можно использовать команду:


   ya dump modules --ignore-recurses <path_to_project>


   Пример:


   ya dump modules devtools/bmake --ignore-recurses



#### Что попадёт в программу

Когда мы говорим о том, что попадёт в программу, важно учитывать не только явные зависимости, но и те, которые будут включены через PEERDIR. В этом разделе рассматривается, как определить, какие библиотеки будут линкованы в программу.

1. Список всех библиотек, которые попадут в программу

   Для анализа всех библиотек, которые будут линкованы в вашу программу или модуль через PEERDIR, без учёта рекурсивных зависимостей и инструментов сборки:


   ya dump modules --ignore-recurses --no-tools <path_to_project>


   Пример:


   ya dump modules devtools/bmake --ignore-recurses --no-tools



2. Определение конкретных путей зависимости

   Чтобы узнать конкретные пути зависимости от проекта до другого модуля, исключая инструменты сборки:


   ya dump relation --ignore-recurses --no-tools <module_name>


   Пример:


   ya dump relation --ignore-recurses --no-tools contrib/tools/python/lib



3. Все пути зависимости

   Если требуется найти все возможные пути зависимости до модуля, можно использовать команду:


   ya dump all-relations --ignore-recurses --no-tools <module_name>



#### Дополнительные опции

1. Флаг --force-build-depends
   Опция --force-build-depends используется для включения в анализ зависимостей, которые добавляются опцией RECURSE_FOR_TESTS и DEPENDS, что полезно для тестовых зависимостей:


   ya dump modules <path_to_project> --force-build-depends


   Пример:


   ya dump modules devtools/bmake --force-build-depends



2. Фильтрация по типу файлов и модулей
   Вы можете исключить ненужные типы файлов и модулей, чтобы сосредоточиться на конкретных элементах проекта:


   ya dump modules <path_to_project> --ignore-recurses --no-tools

















Многие команды семейства `ya dump` предназначены для поиска и анализа зависимостей проектов.

Ниже описаны наиболее распространённые сценарии для управления зависимостями в проектах с помощью фильтров в командах `ya dump modules` и `ya dump relation`.

### Зачем фильтровать зависимости

В _графе зависимостей_ системы сборки представлен разнообразные зависимости, в том числе между проектами (`PEERDIR`) и директориями (`RECURSE`), получаемые из ya.make-файлов и исходного кода.
Ymake использует их, чтобы построить граф команд сборки для разных ситуаций, но при этом часть информаци о зависимостях теряется.
Поэтому `ya dump` собирает информацию обходом _графа зависимостей_ системы сборки, а чтобы смоделировать ту или иную ситуацию, нужно исключать из обхода графа разные виды зависимостей.

Ymake умеет делать `DEPENDENCY_MANAGAMENT` для Java на основе _графа зависимостей_ поэтому всё описанное далее применимо к Java также как и к остальным языкам в Аркадии.

### Что участвует в сборке

Узнать, что будет собираться если написать `ya make <path>`/`ya make -t <path>`.

* Целями сборки будут: ваш модуль + все модули, достижимые от вашего только по `RECURSE`.
* Для всех целей соберутся также все модули, от которых они зависят, то есть достижимые от них по `PEERDIR`, включая программы, которые используются для кодогенерации и запускаемые через `RUN_PROGRAM`.
* `RECURSE` от зависимых модулей в сборке не участвуют,
* C опцией `-t` или `--force-build-depends` к целям сборки добавятся тесты, достижимые по `RECURSE_FOR_TESTS`, а также  `DEPENDS` - зависимости ваших тестов.

1. Список всех зависимостей для проекта: `ya dump modules`
   ```
   ~/ws/arcadia$ ya dump modules devtools/ymake
   module: Library devtools/ymake $B/devtools/ymake/libdevtools-ymake.a <++ SELF
   ...
   module: Library util $B/util/libyutil.a <++ PEERDIRs
   ...
   module: Program devtools/ymake/bin $B/devtools/ymake/bin/ymake <++ RECURSEs
   ...
   module: Program contrib/tools/py3cc $B/tools/py3cc/py3cc   <++ TOOLs
   ...
   ```
   {% note tip %}

   Если хочется видеть и зависимости тестов тоже, используйте `-t` или `--force-build-depends`

   ```
   ~/ws/arcadia$ ya dump modules devtools/ymake | wc -l
   861
   ~/ws/arcadia$ ya dump modules devtools/ymake -t | wc -l
   1040
   ```

   {% endnote %}

2. Список всех зависимостей для проекта в директории `<dir>`: `ya dump modules | grep <dir> `
   ```
   ~/ws/arcadia$ ya dump modules devtools/ymake | grep mapreduce/
   module: Library mapreduce/yt/unwrapper $B/mapreduce/yt/unwrapper/libpymapreduce-yt-unwrapper.a
   module: Library mapreduce/yt/interface $B/mapreduce/yt/interface/libmapreduce-yt-interface.a
   module: Library mapreduce/yt/interface/protos $B/mapreduce/yt/interface/protos/libyt-interface-protos.a
   module: Library mapreduce/yt/interface/logging $B/mapreduce/yt/interface/logging/libyt-interface-logging.a
   module: Library mapreduce/yt/client $B/mapreduce/yt/client/libmapreduce-yt-client.a
   module: Library mapreduce/yt/http $B/mapreduce/yt/http/libmapreduce-yt-http.a
   module: Library mapreduce/yt/common $B/mapreduce/yt/common/libmapreduce-yt-common.a
   module: Library mapreduce/yt/io $B/mapreduce/yt/io/libmapreduce-yt-io.a
   module: Library mapreduce/yt/raw_client $B/mapreduce/yt/raw_client/libmapreduce-yt-raw_client.a
   module: Library mapreduce/yt/skiff $B/mapreduce/yt/skiff/libmapreduce-yt-skiff.a
   module: Library mapreduce/yt/library/table_schema $B/mapreduce/yt/library/table_schema/libyt-library-table_schema.a
   ```
   * Это может быть полезно если точная зависимость неизвестна.
   * `ya dump modules` включает и модули самого проекта, поэтому подав путь до проекта в `<dir>` можно узнать как называются модули в мультимодуле

3. Каким образом проект зависит от модуля: `ya dump relation '<module_name>'`
   ```
   ~/ws/arcadia$ cd devtools/ymake
   ~/ws/arcadia/devtools/ymake$ ya dump relation mapreduce/yt/interface
   Directory (Start): $S/devtools/ymake/tests/dep_mng ->
   Program (Include): $B/devtools/ymake/tests/dep_mng/devtools-ymake-tests-dep_mng ->
   Library (BuildFrom): $B/devtools/ya/test/tests/lib/libpytest-tests-lib.a ->
   Library (Include): $B/devtools/ya/lib/libya-lib.a ->
   Library (Include): $B/devtools/ya/yalibrary/store/yt_store/libpyyalibrary-store-yt_store.a ->
   Library (Include): $B/mapreduce/yt/unwrapper/libpymapreduce-yt-unwrapper.a ->
   Directory (Include): $S/mapreduce/yt/interface
   ```
   * Команду надо запускать в директории проекта, от которого ищем путь.
   * module_name - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
   * Будет показан один из возможных путей.

   * В случае, если хочется найти зависимость от конкретного модуля в мультимодуле надо подсмотреть полное имя модуля в `ya dump modules` и передать его в параметре `--from`.
   * В случае, если хочется найти зависимость от неизвестного модуля где-то в директории `<dir>`, можно использовать (2.) чтобы узнать от каких модулей в директори есть зависимости.

4. Каким образом проект зависит от модуля (все пути): `ya dump all-relations '<module_name>'`

     * Команду надо запускать в директории проекта, от которого ищем путь.
   * module_name - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
   * На выходе будет граф в формате dot со всеми путями в интересующий модуль.


### Что соберётся при `PEERDIR`

Узнать, что будет собираться через библиотеку, если поставить на неё `PEERDIR`.

Правила обхода

Здесь всё как выше, но надо исключить `RECURSE` совсем - при `PEERDIR` на модуль, его `RECURSE` не попадают в сборку.

1. список для проекта: `ya dump modules --ignore-recurses`
   ```
   ~/ws/arcadia$ ya dump modules devtools/ymake | wc -l
   861
   ~/ws/arcadia$ ya dump modules devtools/ymake --ignore-recurses | wc -l
   222
   ~/ws/arcadia$ ./ya dump modules devtools/ymake --ignore-recurses | grep mapreduce
   ~/ws/arcadia$ ./ya dump modules devtools/ymake --ignore-recurses | grep python
   module: Library contrib/libs/python $B/contrib/libs/python/libpycontrib-libs-python.a
   module: Library contrib/libs/python/Include $B/contrib/libs/python/Include/libpylibs-python-Include.a
   module: Library contrib/tools/python/lib $B/contrib/tools/python/lib/libtools-python-lib.a
   module: Library contrib/tools/python/base $B/contrib/tools/python/base/libtools-python-base.a
   module: Library contrib/tools/python/include $B/contrib/tools/python/include/libtools-python-include.a
   module: Program contrib/tools/python/bootstrap $B/contrib/tools/python/bootstrap/bootstrap
   module: Library library/python/symbols/module $B/library/python/symbols/module/libpypython-symbols-module.a
   module: Library library/python/symbols/libc $B/library/python/symbols/libc/libpython-symbols-libc.a
   module: Library library/python/symbols/registry $B/library/python/symbols/registry/libpython-symbols-registry.a
   module: Library library/python/symbols/python $B/library/python/symbols/python/libpython-symbols-python.a
   module: Library library/python/symbols/uuid $B/library/python/symbols/uuid/libpython-symbols-uuid.a
   module: Library library/python/runtime $B/library/python/runtime/libpylibrary-python-runtime.a
   module: Library contrib/python/six $B/contrib/python/six/libpycontrib-python-six.a
   ```

2. путь непосредственно от проекта в директории, до другого модуля  `ya dump relation --ignore-recurses <module_name>`
   ```
   ~/ws/arcadia$ cd devtools/ymake
   ~/ws/arcadia/devtools/ymake$ ya dump relation --ignore-recurses mapreduce/yt/interface
   Target 'mapreduce/yt/interface' is not found in build graph.
   ~/ws/arcadia/devtools/ymake$ ya dump relation --ignore-recurses contrib/tools/python/bootstrap
   Directory (Start): $S/devtools/ymake ->
   Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
   Library (Include): $B/contrib/libs/python/libpycontrib-libs-python.a ->
   Library (Include): $B/contrib/libs/python/Include/libpylibs-python-Include.a ->
   Library (Include): $B/contrib/tools/python/lib/libtools-python-lib.a ->
   NonParsedFile (BuildFrom): $B/contrib/tools/python/lib/python_frozen_modules.o ->
   NonParsedFile (BuildFrom): $B/contrib/tools/python/lib/python_frozen_modules.rodata ->
   BuildCommand (BuildCommand): 2964:RUN_PROGRAM=([ ] [ PYTHONPATH=contrib/tools/python/src/Lib ] [ bootstrap.py python-libs.txt  ... ->
   Directory (Include): $S/contrib/tools/python/bootstrap
   ```

3. все пути непосредственно от проекта в директории, до другого модуля  `ya dump all-relations --ignore-recurses <module_name>`


### Что попадёт в программу

Здесь нас интересуют именно библиотеки. Либо все, которые попадут в программу (если анализируем `PROGRAM` и т.п.), либо те, которые попадут при `PEERDIR` на `LIBRARY`.

Правила обхода

* Целью сборки является ваш модуль (`PROGRAM` или `LIBRARY`) и всё, от чего он зависит по `PEERDIR`
* То, что достижимо по `RECURSE` будет собираться, но линковаться будет в собственные библиотеки и программы и к вам не попадёт.
* Не будут линковаться к вам и сборочные инструменты.

1. список для проекта: `ya dump modules --ignore-recurses --no-tools`
   ```
   ~/ws/arcadia$ ya dump modules devtools/ymake --ignore-recurses | wc -l
   222
   ~/ws/arcadia$ ya dump modules devtools/ymake --ignore-recurses --no-tools | wc -l
   198
   ~/ws/arcadia$ ya dump modules devtools/ymake --ignore-recurses --no-tools | grep python
   module: Library contrib/libs/python $B/contrib/libs/python/libpycontrib-libs-python.a
   module: Library contrib/libs/python/Include $B/contrib/libs/python/Include/libpylibs-python-Include.a
   module: Library contrib/tools/python/lib $B/contrib/tools/python/lib/libtools-python-lib.a
   module: Library contrib/tools/python/base $B/contrib/tools/python/base/libtools-python-base.a
   module: Library contrib/tools/python/include $B/contrib/tools/python/include/libtools-python-include.a
   module: Library library/python/symbols/module $B/library/python/symbols/module/libpypython-symbols-module.a
   module: Library library/python/symbols/libc $B/library/python/symbols/libc/libpython-symbols-libc.a
   module: Library library/python/symbols/registry $B/library/python/symbols/registry/libpython-symbols-registry.a
   module: Library library/python/symbols/python $B/library/python/symbols/python/libpython-symbols-python.a
   module: Library library/python/symbols/uuid $B/library/python/symbols/uuid/libpython-symbols-uuid.a
   module: Library library/python/runtime $B/library/python/runtime/libpylibrary-python-runtime.a
   module: Library contrib/python/six $B/contrib/python/six/libpycontrib-python-six.a
   ```

2. путь непосредственно от проекта в директории, до другого модуля: `ya dump relation --no-all-recurses --no-tools <module_name>`
   ```
   ~/ws/arcadia$ cd devtools/ymake
   ~/ws/arcadia/devtools/ymake$ ya dump relation --ignore-recurses --no-tools contrib/tools/python/bootstrap
   Sorry, path not found
   ~/ws/arcadia/devtools/ymake$ ya dump relation --ignore-recurses --no-tools contrib/tools/python/lib
   Directory (Start): $S/devtools/ymake ->
   Library (Include): $B/devtools/ymake/libdevtools-ymake.a ->
   Library (Include): $B/contrib/libs/python/libpycontrib-libs-python.a ->
   Directory (Include): $S/contrib/tools/python/lib
   ```

3. все пути непосредственно от проекта в директории, до другого модуля  `ya dump all-relations --ignore-recurses --no-tools <module_name>`


