## ya dump для анализа зависимостей

Многие команды семейства `ya dump` предназначены для поиска и анализа зависимостей проектов в Аркадии. Ниже описаны наиболее распространённые сценарии для управления зависимостями в
проектах с помощью фильтров в командах `ya dump modules` и `ya dump relation`.

{% cut "Зачем фильтровать зависимости?" %}

В _графе зависимостей_ системы сборки представлен разнообразные зависимости, в том числе между проектами (`PEERDIR`) и директориями (`RECURSE`), получаемые из ya.make-файлов и исходного кода.
Ymake использует их, чтобы построить граф команд сборки для разных ситуаций, но при этом часть информаци о зависимостях теряется.
Поэтому `ya dump` собирает информацию обходом _графа зависимостей_ системы сборки, а чтобы смоделировать ту или иную ситуацию, нужно исключать из обхода графа разные виды зависимостей.

{% note info %}

Ymake умеет делать `DEPENDENCY_MANAGAMENT` для Java на основе _графа зависимостей_ поэтому всё описанное далее применимо к Java также как и к остальным языкам в Аркадии.

{% endnote %}

{% endcut %}



### Что участвует в сборке

Узнать, что будет собираться если написать `ya make <path>`/`ya make -t <path>`.

{% cut "Правила обхода" %}

* Целями сборки будут: ваш модуль + все модули, достижимые от вашего только по `RECURSE`.
* Для всех целей соберутся также все модули, от которых они зависят, то есть достижимые от них по `PEERDIR`, включая программы, которые используются для кодогенерации и запускаемые через `RUN_PROGRAM`.
* `RECURSE` от зависимых модулей в сборке не участвуют,
* C опцией `-t` или `--force-build-depends` к целям сборки добавятся тесты, достижимые по `RECURSE_FOR_TESTS`, а также  `DEPENDS` - зависимости ваших тестов.

{% endcut %}

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

   {% note info %}

   * Это может быть полезно если точная зависимость неизвестна.
   * `ya dump modules` включает и модули самого проекта, поэтому подав путь до проекта в `<dir>` можно узнать как называются модули в мультимодуле

   {% endnote %}


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
   {% note info %}

   * Команду надо запускать в директории проекта, от которого ищем путь.
   * module_name - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
   * Будет показан один из возможных путей.

   {% endnote %}

   {% note tip %}

   * В случае, если хочется найти зависимость от конкретного модуля в мультимодуле надо подсмотреть полное имя модуля в `ya dump modules` и передать его в параметре `--from`.
   * В случае, если хочется найти зависимость от неизвестного модуля где-то в директории `<dir>`, можно использовать (2.) чтобы узнать от каких модулей в директори есть зависимости.

   {% endnote %}


4. Каким образом проект зависит от модуля (все пути): `ya dump all-relations '<module_name>'`

   {% note info %}

   * Команду надо запускать в директории проекта, от которого ищем путь.
   * module_name - имя модуля как пишет `ya dump modules` или директория проекта этого модуля.
   * На выходе будет граф в формате dot со всеми путями в интересующий модуль.

   {% endnote %}


### Что соберётся при `PEERDIR`

Узнать, что будет собираться через библиотеку, если поставить на неё `PEERDIR`.

{% cut "Правила обхода" %}

Здесь всё как выше, но надо исключить `RECURSE` совсем - при `PEERDIR` на модуль, его `RECURSE` не попадают в сборку.

{% endcut %}

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

{% cut "Правила обхода" %}

* Целью сборки является ваш модуль (`PROGRAM` или `LIBRARY`) и всё, от чего он зависит по `PEERDIR`
* То, что достижимо по `RECURSE` будет собираться, но линковаться будет в собственные библиотеки и программы и к вам не попадёт.
* Не будут линковаться к вам и сборочные инструменты.

{% endcut %}

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


