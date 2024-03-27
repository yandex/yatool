# Справка `ya make`
```
Чтобы увидеть больше справки, используйте -hh/-hhh

Использование:
ya make [ОПЦИЯ]… [ЦЕЛЬ]…

Примеры:
ya make -r Собирает текущую директорию в режиме выпуска
ya make -t -j16 library Собирает и тестирует библиотеку с 16 потоками
ya make --checkout -j0 Выполняет чекаут отсутствующих директорий без сборки

Опции:
Управление операциями Ya
-h, --help Печатает справку. Используйте -hh для большего количества опций и -hhh для ещё большего.
--rebuild Пересобрать все
-C=BUILD_TARGETS, --target=BUILD_TARGETS
Цели для сборки
-k, --keep-going Продолжать сборку насколько это возможно
-j=BUILD_THREADS, --threads=BUILD_THREADS
Количество потоков сборки (по умолчанию: 2)
--clear Очистить временные данные
Продвинутые опции
--sandboxing Запуск команды в изолированном корневом исходнике
--link-threads=LINK_THREADS
Количество потоков линковки (по умолчанию: 0)
--no-clonefile Отключить опцию clonefile
--nice=SET_NICE_VALUE
Задать значение nice для процессов сборки (по умолчанию: 10)
--warning-mode=WARN_MODE
Режим предупреждений
Экспертные опции
--fetcher-params=FETCHER_PARAMS_STR
Приоритеты и параметры фетчеров
-B=CUSTOM_BUILD_DIRECTORY, --build-dir=CUSTOM_BUILD_DIRECTORY
Пользовательская директория сборки (определяется автоматически по умолчанию)
--force-use-copy-instead-hardlink-macos-arm64
Использовать копирование вместо hardlink, когда clonefile недоступен
--no-content-uids Отключить дополнительный кеш на основе динамических uid только по содержанию
--keep-temps Не удалять временные корни сборки. Выводить рабочую директорию теста в stderr (используйте --test-stderr, чтобы убедиться, что она выводится в начале теста)
Селективный чекаут
Продвинутые опции
--prefetch Предварительно загружать директории, необходимые для сборки
--no-prefetch Не предварительно загружать директории, необходимые для сборки
Экспертные опции
--thin Чекаут минимального скелета
Выход сборки
--add-result=ADD_RESULT
Обрабатывать выбранный выход сборки как результат
--add-protobuf-result
Обрабатывать выход protobuf как результат
--add-flatbuf-result
Обрабатывать выход flatbuf как результат
--replace-result Собирать только цели --add-result
--force-build-depends
Собирать в любом случае по DEPENDS
--ignore-recurses Не собирать по RECURSES
--no-src-links Не создавать символические ссылки в исходной директории
-o=OUTPUT_ROOT, --output=OUTPUT_ROOT
Директория с результатами сборки
Продвинутые опции
-I=INSTALL_DIR, --install=INSTALL_DIR
Путь для накопления результирующих бинарников и библиотек
Экспертные опции
--add-host-result=ADD_HOST_RESULT
Обрабатывать выбранный выход сборки хоста как результат
--all-outputs-to-result
Обрабатывать все выходы ноды вместе с выбранным выходом сборки как результат
--add-modules-to-results
Обрабатывать все модули как результаты
--strip-packages-from-results
Удалять все пакеты из результатов
--no-output-for=SUPPRESS_OUTPUTS
Не создавать символические ссылки/копировать выход для файлов с данным суффиксом, они могут все равно быть сохранены в кеше как результат
--with-credits Включить генерацию файла CREDITS
Вывод
--stat Показать статистику выполнения сборки
-v, --verbose Выводить подробную информацию
-T Не перезаписывать информацию о выводе (ninja/make)
Продвинутые опции
--show-command=SHOW_COMMAND
Печатать команду для выбранного выхода сборки
--show-timings Печатать время выполнения команд
--show-extra-progress
Печатать дополнительную информацию о прогрессе
--log-file=LOG_FILE Добавить подробный журнал в указанный файл
Экспертные опции
--stat-dir=STATISTICS_OUT_DIR
Дополнительная директория вывода статистики
--no-emit-status Не выводить статус
--do-not-output-stderrs
Не выводить stderr
--mask-roots Маскировать пути к исходному и сборочному корню в stderr
--no-mask-roots Не маскировать пути к исходному и сборочному корню в stderr
--html-display=HTML_DISPLAY
Альтернативный вывод в формате html
--teamcity Генерировать дополнительную информацию для teamcity
Конфигурация платформы/сборки
-d Отладочная сборка
-r Сборка выпуска
--build=BUILD_TYPE Тип сборки (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type (по умолчанию: debug)
--sanitize=SANITIZE Тип санитайзера (address, memory, thread, undefined, leak)
--race Сборка проектов Go с детектором гонок
-D=FLAGS Установить переменные (имя[=значение], “yes” если значение опущено)
--host-platform-flag=HOST_PLATFORM_FLAGS
Флаг платформы хоста
--target-platform=TARGET_PLATFORMS
Целевая платформа
--target-platform-flag=TARGET_PLATFORM_FLAG
Установить флаг сборки для последней целевой платформы
Продвинутые опции
--sanitizer-flag=SANITIZER_FLAGS
Дополнительный флаг для санитайзера
--lto Сборка с LTO
--thinlto Сборка с ThinLTO
--afl Использовать AFL вместо libFuzzer
--musl Сборка с musl-libc
--hardening Сборка с усилением защиты
--cuda=CUDA_PLATFORM
Платформа CUDA (optional, required, disabled) (по умолчанию: optional)
--host-build-type=HOST_BUILD_TYPE
Тип сборки платформы хоста (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type (по умолчанию: release)
--host-platform=HOST_PLATFORM
Платформа хоста
--c-compiler=C_COMPILER
Указывает путь к пользовательскому компилятору для платформ хоста и цели
--cxx-compiler=CXX_COMPILER
Указывает путь к пользовательскому компилятору для платформ хоста и цели
--pgo-add Создать PGO профиль
--pgo-use=PGO_USER_PATH
Путь к PGO профилям
--pic Принудительный режим PIC
--maps-mobile Включить конфигурационный пресет mapsmobi
Экспертные опции
--sanitize-coverage=SANITIZE_COVERAGE
Включить покрытие санитайзером
--target-platform-build-type=TARGET_PLATFORM_BUILD_TYPE
Установить тип сборки для последней целевой платформы
--target-platform-release
Установить тип сборки выпуска для последней целевой платформы
--target-platform-debug
Установить отладочный тип сборки для последней целевой платформы
--target-platform-tests
Запустить тесты для последней целевой платформы
--target-platform-test-size=TARGET_PLATFORM_TEST_SIZE
Запустить тесты только с заданным размером для последней целевой платформы
--target-platform-test-type=TARGET_PLATFORM_TEST_TYPE
Запустить тесты только с заданным типом для последней целевой платформы
--target-platform-regular-tests
Запустить только тесты типов “benchmark boost_test exectest fuzz g_benchmark go_bench go_test gtest hermione java jest py2test py3test pytest unittest” для последней целевой платформы
--target-platform-c-compiler=TARGET_PLATFORM_COMPILER
Указывает путь к пользовательскому компилятору для последней целевой платформы
--target-platform-cxx-compiler=TARGET_PLATFORM_COMPILER
Указывает путь к пользовательскому компилятору для последней целевой платформы
--target-platform-target=TARGET_PLATFORM_TARGET
Цели сборки относительно корня исходного кода для последней целевой платформы
--target-platform-ignore-recurses
Не собирать по RECURSES
Локальный кеш
--cache-stat Показать статистику кеша
--gc Удалить весь кеш, кроме uid из текущего графа
--gc-symlinks Удалить все результаты символических ссылок, кроме файлов из текущего графа
Продвинутые опции
--tools-cache-size=TOOLS_CACHE_SIZE
Максимальный размер кеша инструментов (по умолчанию: 30.0GiB)
--symlinks-ttl=SYMLINKS_TTL
Срок хранения кеша результатов (по умолчанию: 168.0ч)
--cache-size=CACHE_SIZE
Максимальный размер кеша (по умолчанию: 140.04687070846558GiB)
--cache-codec=CACHE_CODEC
Кодек кеша (по умолчанию: )
--auto-clean=AUTO_CLEAN_RESULTS_CACHE
Автоматическая очистка кеша результатов (по умолчанию: True)
YT кеш
--no-yt-store Отключить хранилище YT
Продвинутые опции
--dist-cache-evict-bins
Удалить все нерабочие бинарные файлы из результатов сбоки. Работает только в режиме --bazel-remote-put
--dist-cache-evict-cached
Не собирать или не загружать результаты сборки, если они присутствуют в dist кеше
--dist-store-threads=DIST_STORE_THREADS
Максимальное количество потоков dist store (по умолчанию: 4)
--bazel-remote-store
Использовать хранилище Bazel-remote
--no-bazel-remote-store
Отключить хранилище Bazel-remote
--bazel-remote-base-uri=BAZEL_REMOTE_BASEURI
Базовый URI Bazel-remote (по умолчанию: http://[::1]:8080/)
--bazel-remote-username=BAZEL_REMOTE_USERNAME
Имя пользователя Bazel-remote
--bazel-remote-password=BAZEL_REMOTE_PASSWORD
Пароль Bazel-remote
--bazel-remote-password-file=BAZEL_REMOTE_PASSWORD_FILE
Файл с паролем Bazel-remote
--yt-store Использовать хранилище YT
--yt-store-threads=YT_STORE_THREADS
Максимальное количество потоков хранилища YT (по умолчанию: 1)
Экспертные опции
--yt-token-path=YT_TOKEN_PATH
Путь к токену YT (по умолчанию: /home/mtv2000/.yt/token)
YT кеш загрузки
Экспертные опции
--yt-put Загрузить в хранилище YT
--yt-max-store-size=YT_MAX_CACHE_SIZE
Максимальный размер хранилища YT
--yt-store-ttl=YT_STORE_TTL
Время жизни хранилища YT в часах (0 для бесконечности) (по умолчанию: 24)
--bazel-remote-put Загрузить в хранилище Bazel-remote
--yt-write-through=YT_STORE_WT
Обновлять локальный кеш при обновлении хранилища YT (по умолчанию: True)
--yt-create-tables Создать таблицы хранилища YT
--yt-store-filter=YT_CACHE_FILTER
Фильтр хранилища YT
--yt-store-codec=YT_STORE_CODEC
Кодек хранилища YT
--yt-store-exclusive
Использовать хранилище YT исключительно (прервать сборку, если требуемые данные не представлены в хранилище YT)
--yt-replace-result Собирать только цели, которые нужно загружать в хранилище YT
--yt-replace-result-add-objects
Настроить опцию yt-replace-result: добавить объектные (.o) файлы к результатам сборки. Бесполезно без --yt-replace-result
--yt-replace-result-rm-binaries
Настроить опцию yt-replace-result: удалить все нерабочие бинарные файлы из результатов сборки. Бесполезно без --yt-replace-result
--yt-replace-result-yt-upload-only
Настроить опцию yt-replace-result: добавить в результаты только узлы загрузки в YT. Бесполезно без --yt-replace-result
Функциональные флаги
Экспертные опции
--no-local-executor Использовать Popen вместо локального исполнителя
--dir-outputs-test-mode
Включить новые функции dir outputs
--disable-runner-dir-outputs
Отключить поддержку dir_outputs в исполнителе
--no-dump-debug Отключить режим дампа отладки
Тестирование
Запуск тестов
-t, --run
-tests Запустить тесты (-t запускает только SMALL тесты, -tt запускает SMALL и MEDIUM тесты, -ttt запускает SMALL, MEDIUM и FAT тесты)
-A, --run-all-tests Запустить наборы тестов всех размеров
-L, --list-tests Перечислить тесты
Продвинутые опции
--test-threads=TEST_THREADS
Ограничение на одновременные тесты (без ограничений по умолчанию) (по умолчанию: 0)
--fail-fast Прекратить после первого сбоя теста
Экспертные опции
--add-peerdirs-tests=PEERDIRS_TEST_TYPE
Типы тестов Peerdirs (none, gen, all) (по умолчанию: none)
--split-factor=TESTING_SPLIT_FACTOR
Переопределяет SPLIT_FACTOR(X) (по умолчанию: 0)
--test-prepare Не запускать тесты, только подготовить зависимости и среду для тестов
--no-src-changes Не изменять исходный код
Фильтрация
-X, --last-failed-tests
Перезапустить тесты, которые не прошли при последнем запуске для выбранной цели
-F=TESTS_FILTERS, --test-filter=TESTS_FILTERS
Запустить только тест, соответствующий <tests-filter>. Звездочка ‘’ может быть использована в фильтре для соответствия подмножествам тестов. Чанки также могут быть отфильтрованы с использованием шаблона, соответствующего '[] chunk’
--style Запустить только стилевые тесты и подразумевает --strip-skipped-test-deps (classpath.clash clang_tidy eslint gofmt govet java.style ktlint py2_flake8 flake8 black). Противоположность --regular-tests
--regular-tests Запустить только обычные тесты (benchmark boost_test exectest fuzz g_benchmark go_bench go_test gtest hermione java jest py2test py3test pytest unittest). Противоположность --style
Продвинутые опции
--test-size=TEST_SIZE_FILTERS
Запустить только определенный набор тестов
--test-type=TEST_TYPE_FILTERS
Запустить только определенные типы тестов
--test-tag=TEST_TAGS_FILTER
Запустить тесты с указанным тегом
--test-filename=TEST_FILES_FILTER
Запустить только тесты с указанными именами файлов (только для pytest и hermione)
--test-size-timeout=TEST_SIZE_TIMEOUTS
Установить тайм-аут теста для каждого размера (small=60, medium=600, large=3600)
Отчет в консоли
-P, --show-passed-tests
Показать пройденные тесты
Продвинутые опции
--inline-diff Отключить обрезку комментариев и печатать diff в терминале
--show-metrics Показывать метрики в консоли (Вам нужно добавить опцию “-P”, чтобы видеть метрики для пройденных тестов)
Linters
Продвинутые опции
--disable-flake8-migrations
Включить все проверки flake8
--disable-jstyle-migrations
Включить все проверки стиля java
Канонизация
-Z, --canonize-tests
Канонизировать выбранные тесты
Продвинутые опции
--canon-diff=TEST_DIFF
Показать различия канонических данных теста, допустимые значения: r<revision>, rev1:rev2, HEAD, PREV
Экспертные опции
--canonize-via-skynet
использовать skynet для загрузки больших канонических данных
--canonize-via-http использовать http для загрузки больших канонических данных
Отладка
--pdb Запустить pdb при ошибках
--gdb Запустить c++ unittests в gdb
--dlv Запустить go unittests в dlv
--test-debug Режим отладки теста (печатает pid теста после запуска и подразумевает --test-threads=1 --test-disable-timeout --retest --test-stderr)
Продвинутые опции
--dlv-args=DLV_ARGS Дополнительные аргументы командной строки dlv. Не действует, если не указан --dlv
--test-retries=TESTS_RETRIES
Запускать каждый тест указанное количество раз (по умолчанию: 1)
--test-stderr Выводить stderr теста в консоль онлайн
--test-stdout Выводить stdout теста в консоль онлайн
--test-disable-timeout
Отключить тайм-аут для тестов (только для локальных запусков, не совместимо с --cache-tests, --dist)
--test-binary-args=TEST_BINARY_ARGS
Передать аргументы в тестируемый бинарный файл
--dump-test-environment
Вывести содержимое корня сборки теста в формате дерева в файл run_test.log перед выполнением оболочки теста
Экспертные опции
--no-random-ports Использовать запрошенные порты
--disable-test-graceful-shutdown
Узел теста будет немедленно убит после тайм-аута
Среда выполнения
--test-param=TEST_PARAMS
Произвольные параметры, передаваемые тестам (name=val)
--autocheck-mode Запустить тесты локально с ограничениями autocheck (подразумевает --private-ram-drive и --private-net-ns)
Продвинутые опции
--private-ram-drive Создает частный ram диск для всех узлов тестирования, запрашивающих его
--private-net-ns Создает частное сетевое пространство имен с поддержкой localhost
Экспертные опции
--arcadia-tests-data=ARCADIA_TESTS_DATA_PATH
Пользовательский путь к arcadia_tests_data (по умолчанию: arcadia_tests_data)
Расчет uid теста
--cache-tests Использовать кеш для тестов
--retest Не использовать кеш для тестов
Зависимости тестов
-b, --build-all Собрать цели, не требуемые для запуска тестов, но доступные с RECURSE
Экспертные опции
--strip-skipped-test-deps
Не собирать зависимости пропущенных тестов
--build-only-test-deps
Собрать только цели, требуемые для запрошенных тестов
--strip-idle-build-results
Удалить все результатные узлы (включая узлы сборки), не требуемые для запуска тестов
--no-strip-idle-build-results
Не удалять все результатные узлы (включая узлы сборки), не требуемые для запуска тестов
Отчеты о файлах
--junit=JUNIT_PATH Путь к генерируемому junit отчету
Продвинутые опции
--allure=ALLURE_REPORT (устарело)
Путь к генерируемому отчету allure
Выводы тестов
Экспертные опции
--no-test-outputs Не сохранять testing_out_stuff
--no-dir-outputs (устарело)
Упаковать директорию вывода тестирования в промежуточные механизмы
--dir-outputs-in-nodes
Включить поддержку dir outputs в узлах
--keep-full-test-logs
Не укорачивать логи на distbuild
--test-node-output-limit=TEST_NODE_OUTPUT_LIMIT
Указывает ограничение на файлы вывода (в байтах)
--test-keep-symlinks
Не удалять символические ссылки из вывода теста
Тесты через YT
--run-tagged-tests-on-yt
Запускать тесты с тегом ya:yt на YT
Тесты через Sandbox
--run-tagged-tests-on-sandbox
Запускать тесты с тегом ya:force_sandbox в Sandbox
Покрытие
--python-coverage Собирать информацию о покрытии для python
--ts-coverage Собирать информацию о покрытии для ts
--go-coverage Собирать информацию о покрытии для go
--java-coverage Собирать информацию о покрытии для java
--clang-coverage Покрытие на основе исходного кода clang (автоматически увеличивает время ожидания тестов в 1,5 раза)
--coverage-report Создать HTML отчет о покрытии (использовать с --output)
--nlg-coverage Собирать информацию о покрытии для Alice NLG
Продвинутые опции
--coverage (устарело)
Собирать информацию о покрытии. (устаревший псевдоним для “–gcov --java-coverage --python-coverage --coverage-report”)
--coverage-prefix-filter=COVERAGE_PREFIX_FILTER
Исследовать только соответствующие пути
--coverage-exclude-regexp=COVERAGE_EXCLUDE_REGEXP
Исключить соответствующие пути из отчета о покрытии
--sancov Собирать информацию о покрытии санитайзером (автоматически увеличивает время ожидания тестов в 1,5 раза)
--fast-clang-coverage-merge
Объединять профили в памяти во время выполнения теста с использованием fuse
--enable-java-contrib-coverage
Добавить исходники и классы из contib/java в отчет jacoco
--enable-contrib-coverage
Собирать contrib с опциями покрытия и добавлять тесты coverage.extractor для бинарных файлов contrib
Экспертные опции
--coverage-report-path=COVERAGE_REPORT_PATH
Путь внутри директории вывода, куда сохранять отчет о покрытии gcov cpp (использовать с --output)
--merge-coverage Объединить все разрешенные файлы покрытия в один файл
--upload-coverage Загрузить собранное покрытие в YT
--coverage-verbose-resolve
Печатать отладочные логи на этапе разрешения покрытия
Fuzzing
--fuzzing Расширить корпус тестов. Подразумевает --sanitizer-flag=-fsanitize=fuzzer
--fuzz-case=FUZZ_CASE_FILENAME
Указать путь к файлу с данными для фаззинга (конфликтует с “–fuzzing”)
Продвинутые опции
--fuzz-opts=FUZZ_OPTS
Строка разделенных пробелом опций фаззинга (по умолчанию: )
--fuzz-minimization-only
Позволяет запустить минимизацию без фаззинга (должно быть использовано с “–fuzzing”)
--fuzz-local-store Не загружать в кеш mined корпус
--fuzz-runs=FUZZ_RUNS
Минимальное количество отдельных запусков тестов
--fuzz-proof=FUZZ_PROOF
Позволяет запустить дополнительный этап фаззинга на указанное количество секунд с момента последнего найденного случая, чтобы доказать, что больше ничего не будет найдено (по умолчанию: 0)
--fuzz-minimize Всегда запускать узел минимизации после этапа фаззинга
Специфика pytest
--test-log-level=TEST_LOG_LEVEL
Указывает уровень журналирования для вывода логов тестов (“critical”, “error”, “warning”, “info”, “debug”)
Продвинутые опции
--test-traceback=TEST_TRACEBACK
Стиль трассировки теста для pytests (“long”, “short”, “line”, “native”, “no”) (по умолчанию: short)
--profile-pytest Профилировать pytest (выводит cProfile в stderr и генерирует ‘pytest.profile.dot’ с использованием gprof2dot в директории testing_out_stuff)
--pytest-args=PYTEST_ARGS
Дополнительные опции командной строки pytest (по умолчанию: [])
Специфика тестов Java
Продвинутые опции
-R=PROPERTIES, --system-property=PROPERTIES
Установить системное свойство (name=val)
--system-properties-file=PROPERTIES_FILES
Загрузить системные свойства из файла
--jvm-args=JVM_ARGS Добавить аргументы jvm для запуска jvm
Специфика hermione
--hermione-config=HERMIONE_CONFIG
Путь к файлу конфигурации
--hermione-browser=HERMIONE_BROWSERS
Запустить тесты только в указанном браузере
Продвинутые опции
--hermione-grep=HERMIONE_GREP
Запустить тесты, соответствующие указанному шаблону
--hermione-test-path=HERMIONE_TEST_PATHS
Запустить тесты, находящиеся в указанных файлах (пути должны быть относительными по отношению к cwd)
--hermione-set=HERMIONE_SETS
Запустить тесты только в указанном наборе
--hermione-gui Запустить hermione в режиме графического интерфейса
--hermione-gui-auto-run
Автоматически запустить тесты в режиме графического интерфейса сразу после запуска
--hermione-gui-no-open
Не открывать окно браузера после запуска сервера в режиме графического интерфейса
--hermione-gui-hostname=HERMIONE_GUI_HOSTNAME
Хостнейм для запуска сервера в режиме графического интерфейса
--hermione-gui-port=HERMIONE_GUI_PORT
Порт для запуска сервера в режиме графического интерфейса
Специфика JUnit
Продвинутые опции
--junit-args=JUNIT_ARGS
Дополнительные опции командной строки JUnit

Продвинутое использование:
Продвинутые опции
--strict-inputs (устарело)
Включить строгий режим
Специфика Java
--sonar Анализировать код с помощью Sonar.
--maven-export Экспортировать в maven репозиторий
Продвинутые опции
--version=VERSION Версия артефактов для экспорта в maven
-J=JAVAC_FLAGS, --javac-opts=JAVAC_FLAGS
Установить общие флаги javac (имя=val)
--error-prone-flags=ERROR_PRONE_FLAGS
Установить флаги Error Prone
--disable-run-script-generation
Отключить генерацию скриптов запуска для JAVA_PROGRAM
Экспертные опции
--sonar-project-filter=SONAR_PROJECT_FILTERS
Анализировать только проекты, соответствующие любому фильтру
--sonar-default-project-filter
Установить значение по умолчанию --sonar-project-filter (цели сборки)
-N=SONAR_PROPERTIES, --sonar-property=SONAR_PROPERTIES
Свойства для анализатора Sonar (имя[=значение], “yes”, если значение опущено)
--sonar-do-not-compile
Не компилировать исходные коды Java. В этом случае свойство “-Dsonar.java.binaries” не устанавливается автоматически.
--sonar-java-args=SONAR_JAVA_ARGS
Свойства Java машины для запуска сканера Sonar
--get-deps=GET_DEPS Скомпилировать и собрать все зависимости в указанную директорию
-s, --sources Создавать также jar-файлы исходного кода

Загрузка:
Экспертные опции
--ttl=TTL Время жизни ресурса в днях (передайте ‘inf’ - чтобы пометить ресурс как неудаляемый) (по умолчанию: 14)

Загрузка в песочницу:
Продвинутые опции
--owner=RESOURCE_OWNER
Имя пользователя, которому принадлежат данные, сохраненные в песочнице. Требуется в случае бесконечного срока хранения ресурсов в mds.
--sandbox-url=SANDBOX_URL
URL песочницы для хранения канонических файлов (по умолчанию: https://sandbox.yandex-team.ru)
--task-kill-timeout=TASK_KILL_TIMEOUT
Тайм-аут в секундах для задачи загрузки в песочницу
--sandbox Загрузить в песочницу

Загрузка в mds:
Продвинутые опции
--mds Загрузить в MDS
--mds-host=MDS_HOST Хост MDS (по умолчанию: storage.yandex-team.ru)
--mds-port=MDS_PORT Порт MDS (по умолчанию: 80)
--mds-namespace=MDS_NAMESPACE
Пространство имен MDS (по умолчанию: devtools)
--mds-token=MDS_TOKEN
Токен Basic Auth MDS

Авторизация:
Продвинутые опции
--key=SSH_KEYS Путь к приватному SSH ключу для обмена на OAuth токен
--token=OAUTH_TOKEN OAuth токен
--user=USERNAME Имя пользователя для авторизации
--ssh-key=SSH_KEYS Путь к приватному SSH ключу для обмена на OAuth токен
```
