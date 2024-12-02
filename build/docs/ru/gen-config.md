## ya gen-config

Команда `ya gen-config` используется для генерации конфигурационного файла инструмента `ya`. Это позволяет пользователю создать стандартный (базовый) конфигурационный файл, содержащий описание и закомментированные значения по умолчанию для различных настроек. Пользователь может затем настроить файл конфигурации в соответствии со своими требованиями.

### Использование

`ya gen-config [OPTION]… [ya.conf]…`

- `ya gen-config path_proect/${USER}/ya.conf` генерирует пользовательский конфигурационный файл в указанном месте. Если конфигурация уже существует, новая конфигурация сохраняет и учитывает ранее заданные параметры.

- Если пользователь поместит конфигурацию в свой домашний каталог с именем `ya.conf`, она будет автоматически использоваться для определения параметров работы `ya`.

- Значения в `ya.conf` имеют наименьший приоритет и могут быть переопределены через переменные окружения или аргументы командной строки.

- Если каталог проекта (`path_proect`) отсутствует, можно сохранить конфигурацию в `~/.ya/ya.conf`.

### Опции

- `-h`, `--help` — показать справку по использованию команды. Используйте `-hh` для вывода дополнительных опций и `-hhh` для еще более расширенной помощи.
- `--dump-defaults` — выгрузить значения по умолчанию в формате JSON.

### Формат `ya.conf`

Файл `ya.conf` должен быть в формате [toml](https://github.com/toml-lang/toml).

Важные указания для управления этим файлом:

- Для опций без параметров следует указывать `true` в качестве значения.
- Для опций, представляющих собой «словари» (например, `flags`), необходимо открыть соответствующую секцию (таблицу). В этой секции указываются записи в формате `key = "value"`.

В файле `ya.conf` представлены различные параметры, которые можно настроить для управления процессом сборки, тестирования, кешированием и другими аспектами работы с проектом.

#### Основные параметры конфигурационного файла ya.conf

| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| auto_exclude_symlinks | false | Автоматически исключать симлинки |
| copy_shared_index_config | false | Копировать конфигурацию Shared Index |
| detect_leaks_in_pytest | true | Обнаруживать утечки в Pytest |
| eager_execution | false | Быстрое выполнение |
| exclude_dirs | [] | Исключить каталоги |
| external_content_root_modules | [] | Добавить модули внешнего контента |
| fail_maven_export_with_tests | false | Завершать экспорт Maven с тестами при ошибке |
| generate_tests_for_deps | false | Генерировать тесты для зависимостей |
| generate_tests_run | false | Генерировать тестовые конфигурации Junit |
| idea_files_root | "None" | Корневая директория для .ipr и .iws файлов |
| idea_jdk_version | "None" | Версия JDK для проекта IDEA |
| iml_in_project_root | false | Хранить .iml файлы в корне проекта |
| iml_keep_relative_paths | false | Сохранять относительные пути в .iml файлах |
| minimal | false | Минимальный набор настроек проекта |
| oauth_exchange_ssh_keys | true | Обмен oauth-ключами SSH |
| oauth_token_path | "None" | Путь к файлу oauth токена |
| omit_test_data | false | Исключить test_data |
| omitted_test_statuses | ["good", "xfail", "not_launched"] | Cтатусы тестов, которые нужно пропустить |
| project_name | "None" | Имя проекта IDEA |
| regenarate_with_project_update | "None" | Перегенерация вместе с обновлением проекта |
| run_tests_size | 1 | Размер тестов по умолчанию для выполнения (1 — маленькие, 2 — маленькие+средние, 3 — все) |
| separate_tests_modules | false | Не объединять модули тестов с их библиотеками |
| setup_pythonpath_env | true | Настроить переменную окружения PYTHONPATH |
| strip_non_executable_target | "None" | Удалить неисполняемые цели |
| test_fakeid | "" | Идентификатор поддельного теста |
| use_atd_revisions_info | false | Использовать информацию о ревизиях ATD |
| use_command_file_in_testtool | false | Использовать командный файл в инструментарии для тестов |
| use_jstyle_server | false | Использовать сервер Jstyle |
| use_throttling | false | Использовать ограничение скорости |
| with_common_jvm_args_in_junit_template | false | Добавить общие флаги JVM_ARGS в шаблон Junit |
| with_content_root_modules | false | Генерировать модули корневого содержимого |
| with_long_library_names | false | Генерировать длинные имена библиотек |
| ya_bin3_required | "None" | Требуется ya_bin3 |

#### Проектные опции Idea конфигурационного файла ya.conf
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| add_py_targets | false | Добавить цели Python 3 |
| content_root | "None" | Корневая директория для проекта CLion |
| dry_run | false | Эмуляция создания проекта без выполнения действий |
| filters | [] | Фильтрация содержимого |
| full_targets | false | Полное построение графа целей проекта |
| group_modules | "None" | Группировка модулей Idea (дерево, плоско) |
| idea_project_root | "None" | Корневая директория проекта IntelliJ IDEA |
| lite_mode | false | Легкий режим для решения (быстрое открытие, без сборки) |
| local | false | Рекурсивный поиск модулей проекта Idea |
| remote_build_path | "None" | Путь к директории для вывода CMake на удаленном хосте |
| remote_deploy_config | "None" | Имя конфигурации удаленного сервера для удаленного инструментарием |
| remote_deploy_host | "None" | Имя хост-сервера для удаленной конфигурации |
| remote_repo_path | "None" | Путь к удаленному репозиторию на удаленном хосте |
| remote_toolchain | "None" | Генерировать конфигурации для удаленного инструментарием с этим именем |
| use_sync_server | false | Использовать сервер синхронизации вместо наблюдателей файлов |

#### Опции вывода результатов сборки
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| add_host_result | [] | Обрабатывать выбранные результаты сборки на хосте |
| add_result | [] | Обрабатывать выбранные результаты сборки |
| all_outputs_to_result | false | Обрабатывать все выходные данные узла вместе с выбранными результатами сборки |
| create_symlinks | true | Создавать симлинки в директории с исходниками |
| force_build_depends | false | Строить по DEPENDS в любом случае |
| ignore_recurses | false | Не строить по RECURSES |
| install_dir | "None" | Путь для сохранения результирующих бинарных файлов и библиотек |
| output_root | "None" | Директория для вывода результат сборки |
| suppress_outputs | [] | Не выводить/копировать файлы с указанными суффиксами |
| symlink_root | "None" | Корневая директория для сохранения результатов в виде символических ссылок |

#### Опции печати результатов сборки
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| default_suppress_outputs | [".o", ".obj", ".mf", "…", ".cpf", ".cpsf", ".srclst", ".fake", ".vet.out", ".vet.txt", ".self.protodesc"] | Не выводить/копировать файлы с указанными суффиксами по умолчанию |
| ext_progress | false | Печатать дополнительную информацию о прогрессе |
| mask_roots | "None" | Закрывать исходные и корневые пути в stderr |
| output_style | "ninja" | Стиль вывода информации (ninja/make) |
| print_statistics | false | Печатать статистику выполнения сборки |
| show_timings | false | Печатать время выполнения команд |
| statistics_out_dir | "None" | Директория для вывода дополнительных статистических данных |

#### Конфигурация платформы/сборки
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| build_type | "release" | Тип сборки (debug, release, profile и т.д.) |
| host_platform | "None" | Платформа хоста |
| preset_disable_customization | false | Отключить кастомизацию торговли ya |
| target_platforms | [] | Целевая платформа |

#### Опции работы с кешем
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| auto_clean_results_cache | true | Автоматическая очистка кеша результатов сборки |
| build_cache | false | Включить кеш сборки |
| build_cache_conf_str | [] | Переопределить конфигурации |
| build_cache_master | false | Включить режим мастера кеша сборки |
| cache_codec | "None" | Кодек для кеша |
| cache_size | 322122547200 | Максимальный размер кеша |
| new_store | true | Использовать альтернативное хранилище |
| strip_symlinks | false | Удалить все результаты симлинков, кроме файлов из текущего графа |
| symlinks_ttl | 604800 | Время жизни кеша результатов |
| tools_cache | false | Включить кеш инструментов |
| tools_cache_conf_str | [] | Переопределить конфигурационные опции |
| tools_cache_gl_conf_str | [] | Переопределить глобальные конфигурации кеша инструментов |
| tools_cache_ini | "None" | Переопределить встроенный ini-файл кеша инструментов |
| tools_cache_master | false | Включить режим мастера кеша инструментов |
| tools_cache_size | 32212254720 | Максимальный размер кеша инструментов |

#### Генерация графа
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| compress_ymake_output | false | Сжимать вывод ymake для уменьшения максимального использования памяти |
| compress_ymake_output_codec | "zstd08_1" | Кодек для сжатия вывода ymake |

#### Флаги функций
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| dir_outputs_test_mode | false | Включить новый режим вывода директорий |
| dump_debug_enabled | false | Включить отладочный режим дампа |
| local_executor | true | Использовать локальный исполнитель вместо Popen |
| new_runner | true | Использовать альтернативный раннер |
| platform_schema_validation | false | Не проверять целевые платформы по схеме |
| runner_dir_outputs | true | Отключить поддержку вывода директорий в раннере |

#### Опции тестирования
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| cache_fs_read | false | Использовать кеш файловой системы вместо памяти (только для чтения) |
| cache_fs_write | false | Использовать кеш файловой системы вместо памяти (только для записи) |
| cache_tests | false | Использовать кеш для тестов |
| canonization_backend | "None" | Назначить бэкенд для канонизации с шаблоном |
| canonization_scheme | "https" | Протокол для бэкенда канонизации (https по умолчанию) |
| dir_outputs | true | Архивировать выходную директорию тестирования |
| dir_outputs_in_nodes | false | Включить поддержку вывода директорий в узлах |
| disable_flake8_migrations | true | Включить все проверки flake8 |
| disable_jstyle_migrations | false | Включить все проверки стиля java |
| fail_fast | false | Завершать при первом тесте с ошибкой |
| inline_diff | false | Отключить усечение комментариев и выводить diff в терминал |
| junit_args | "None" | Дополнительные параметры командной строки для JUnit |
| junit_path | "None" | Путь для генерации отчета junit |
| last_failed_tests | false | Перезапускать тесты, которые не прошли последним запуском |
| merge_split_tests | true | Не объединять разделенные тесты в директорию ({testing_out_stuff}) c макросом FORK_*TESTS |
| remove_implicit_data_path | false | Удалить неявный путь из макроса DATA |
| remove_result_node | false | Удалить узел результата из графа, печатать отчет по тестам в ya |
| remove_tos | false | Удалить верхний уровень директории {testing_out_stuff} |
| run_tagged_tests_on_yt | false | Запускать тесты с тэгом ya:yt на YT |
| show_passed_tests | false | Показывать пройденные тесты |
| show_skipped_tests | false | Показывать пропущенные тесты |
| store_original_tracefile | false | Хранить оригинальный файлы trace |
| strip_idle_build_results | false | Удалить все узлы результата (включая узлы сборки), которые не нужны для выполнения тестов |
| strip_skipped_test_deps | false | Не строить зависимости пропущенных тестов |
| test_node_output_limit | "None" | Лимит размера выводных файлов тестов (в байтах) |
| test_output_compression_filter | "zstd" | Фильтр сжатия вывода тестов (none, zstd, gzip) |
| test_output_compression_level | 1 | Уровень сжатия вывода тестов для указанного фильтра |
| test_stderr | false | Выводить stderr тестов в консоль в режиме реального времени |
| test_stdout | false | Выводить stdout тестов в консоль в режиме реального времени |
| test_traceback | "short" | Стиль backtrace для тестов ("long", "short", "line", "native", "no") |
| ytexec_wrapper_m_cpu | 250 | Требования к millicpu для distbuild. |

#### Опции тестирования
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| cache_fs_read | false | Использовать кеш файловой системы вместо памяти (только для чтения) |
| cache_fs_write | false | Использовать кеш файловой системы вместо памяти (только для записи) |
| cache_tests | false | Использовать кеш для тестов |
| canonization_backend | "None" | Назначить бэкенд для канонизации с шаблоном |
| canonization_scheme | "https" | Протокол для бэкенда канонизации (https по умолчанию) |
| dir_outputs | true | Архивировать выходную директорию тестирования |
| dir_outputs_in_nodes | false | Включить поддержку вывода директорий в узлах |
| disable_flake8_migrations | true | Включить все проверки flake8 |
| disable_jstyle_migrations | false | Включить все проверки стиля java |
| fail_fast | false | Завершать при первом тесте с ошибкой |
| inline_diff | false | Отключить усечение комментариев и выводить diff в терминал |
| junit_args | "None" | Дополнительные параметры командной строки для JUnit |
| junit_path | "None" | Путь для генерации отчета junit |
| last_failed_tests | false | Перезапускать тесты, которые не прошли последним запуском |
| merge_split_tests | true | Не объединять разделенные тесты в директорию ({testing_out_stuff}) c макросом FORK_*TESTS |
| remove_implicit_data_path | false | Удалить неявный путь из макроса DATA |
| remove_result_node | false | Удалить узел результата из графа, печатать отчет по тестам в ya |
| remove_tos | false | Удалить верхний уровень директории {testing_out_stuff} |
| run_tagged_tests_on_yt | false | Запускать тесты с тэгом ya:yt на YT |
| show_passed_tests | false | Показывать пройденные тесты |
| show_skipped_tests | false | Показывать пропущенные тесты |
| store_original_tracefile | false | Хранить оригинальный файлы trace |
| strip_idle_build_results | false | Удалить все узлы результата (включая узлы сборки), которые не нужны для выполнения тестов |
| strip_skipped_test_deps | false | Не строить зависимости пропущенных тестов |
| test_node_output_limit | "None" | Лимит размера выводных файлов тестов (в байтах) |
| test_output_compression_filter | "zstd" | Фильтр сжатия вывода тестов (none, zstd, gzip) |
| test_output_compression_level | 1 | Уровень сжатия вывода тестов для указанного фильтра |
| test_stderr | false | Выводить stderr тестов в консоль в режиме реального времени |
| test_stdout | false | Выводить stdout тестов в консоль в режиме реального времени |
| test_traceback | "short" | Стиль backtrace для тестов ("long", "short", "line", "native", "no") |
| ytexec_wrapper_m_cpu | 250 | Требования к millicpu для distbuild. |

#### Дополнительные опции
| Опция | Значение по умолчанию | Примечания |
|----------------|-----|----------------------------------------------------|
| test_types_fakeid | | Переименовывание цветов для разметки (например, bad = "light-red") |
| terminal_profile | | Профиль терминала |
| flags | | Установка переменных (name[=val], "yes" если значение опущено) |
| host_platform_flags | | Флаги платформы хоста |
| test_size_timeouts | | Установка таймаутов тестов для каждого размера (small=60, medium=600, large=3600) |

В настоящий момент нет удобного инструмента для того, чтобы определить, что делает тот или иной параметр. В будущем мы планируем добавить описания для параметров конфигурации и сделать работу с ними прозрачней.

Для того, чтобы настроить нужный аспект работы `ya` через файл конфигурации нужно:
- Найти аргумент в исходном коде (например `-j`, он будет обернут в класс `*Consumer`)
- Найти переменную, которая выставляется с помощью аргумента, обычно это `*Hook` (в данном случае `build_threads`)
- Найти соответствующий `ConfigConsumer`, это и будет нужное название параметра

Конфигурационный файл можно настроить под конкретные нужды проекта, активировав или деактивировав определенные функции, чтобы оптимизировать процесс разработки и сборки.

### Порядок применения опций

Порядок применения опций для настройки инструментария `ya` описывает иерархию и логику переопределения настроек конфигураций, которые используются при работе с системой сборки.

Опции `ya`, указанные в файлах конфигурации или переданные через командную строку, применяются в следующем порядке, где каждый последующий уровень может переопределять настройки предыдущего.

Вот возможные места:
1. `$path_proect/ya.conf` — общие настройки для проекта.
2. `$path_proect/${USER}/ya.conf` — пользовательские настройки в рамках одного проекта.
3. `$repo/../ya.conf` — если требуется иметь разные настройки для разных репозиториев.
4. `~/.ya/ya.conf` — глобальные пользовательские настройки на уровне системы.
5. Переменные окружения.
6. Аргументы командной строки.

### Возможности именования `ya.conf`

Файлы конфигурации могут иметь специализированные имена, которые позволяют менять настройки в зависимости от конкретной системы или команды:

- `ya.conf` — базовый файл конфигурации.
- `ya.${system}.conf` — для конкретной операционной системы.
- `ya.${command}.conf` — для конкретной команды.
- `ya.${command}.${system}.conf` — для конкретной команды под конкретную операционную систему.

Модификаторы `${system}` и `${command}` адресуют конфигурационные файлы к определенной системе или команде, например, `ya.make.darwin.conf` для команды `ya make` на системе `darwin`.

### Примеры опций для конкретных команд `ya`

#### Глобальные настройки и локальные переопределения
```bash
project_output = "/default/path"

[ide.qt]
project_output = "/path/to/qt/project"

[ide.msvs]
project_output = "c:\path\to\msvs\project"
```
В приведенном примере задается общий путь до проекта как `"/default/path"`, однако для команд `ya ide qt` и `ya ide msvs` устанавливаются специализированные пути.

#### Переопределение словарных опций
```bash
[flags]
NO_DEBUGINFO = "yes"

[dump.json-test-list.flags]
MY_SPECIAL_FLAG = "yes"
```
Здесь для большинства сценариев используется флаг `NO_DEBUGINFO="yes"`, но для команды `ya dump json-test-list` задается дополнительный флаг `MY_SPECIAL_FLAG="yes"`, в то время как `NO_DEBUGINFO` не применяется.

#### Подстановка переменных окружения

Строковые ключи могут указывать переменные окружения в формате `${ENV_VAR}`, которые будут подменяться после загрузки конфигов.

#### Настройка цветов

`ya` использует систему маркировки текста с применением переменных окружения для управления цветовой схемой в терминале. Это позволяет пользователям менять настройки цветового отображения различных элементов терминала для улучшения читаемости и визуального восприятия.

```bash
alt1 = "cyan"
alt2 = "magenta"
alt3 = "light-blue"
bad = "red"
good = "green"
imp = "light-default"
path = "yellow"
unimp = "dark-default"
warn = "yellow"
```
Для изменения цветов, связанных с этими маркерами, можно использовать секцию `terminal_profile` в конфигурационном файле `ya.conf`. Это позволяет задать пользовательские цвета для каждого из маркеров.

**Пример конфигурационного файла**
```bash
[terminal_profile]
bad = "light-red"
unimp = "default"
```
В примере выше, цвет для маркера `bad` изменен на `light-red` (светло-красный), а для `unimp` используется цвет по умолчанию.

Чтобы добавить интересующие целевые платформы, достаточно несколько раз описать следующую конструкцию:
```bash
[[target_platform]]
platform_name = "default-darwin-arm64"
build_type = "relwithdebinfo"

[target_platform.flags]
ANY_FLAG = "flag_value"
ANY_OTHER_FLAG = "flag_value"
```
На каждый параметр командной строки `--target-platform-smth` существует аналогичный ключ для файла конфигурации.

### Описание дополнительных опций (alias)

Alias в `ya` позволяет объединять часто используемые аргументы в единое короткое обозначение, облегчая выполнение повторяющихся задач и упрощая командные вызовы. Это особенно полезно, когда нужно постоянно задавать одни и те же аргументы в командах сборки `ya make`.

Alias-ы описываются в конфигурационных файлах с использованием синтаксиса `TOML Array of Tables`. Это позволяет группировать настройки и легко применять их при необходимости.

#### Примеры использования alias
##### Добавление .go-файлов

Для добавления симлинков на сгенерированные .go-файлы в обычном режиме необходимо указать множество аргументов:
```bash
ya make path/to/project --replace-result --add-result=.go --no-output-for=.cgo1.go --no-output-for=.res.go --no-output-for=_cgo_gotypes.go --no-output-for=_cgo_import.go
```
##### Конфигурация alias-а в ya.make.conf:
```bash
# path_proect/<username>/ya.make.conf
replace_result = true  # --replace-result
add_result_extend = [".go"]  # --add-result=.go
suppress_outputs = [".cgo1", ".res.go", "_cgo_gotypes.go"]  # --no-output-for options
```
##### Создание alias-а в ya.conf:

```bash
# path_proect/<username>/ya.conf

[[alias]]
replace_result = true
add_result_extend = [".go"]
suppress_outputs = [".cgo1", ".res.go", "_cgo_gotypes.go"]

[alias._settings.arg]
names = ["–add-go-result"]
help = "Add generated .go files"
visible = true
```
Такой alias позволяет заменить длинную команду на:
`ya make path/to/project --add-go-result`

##### Отключение предпостроенных тулов

Для отключения использования предпостроенных тулов в обычном режиме нужны следующие аргументы:
```bash
ya make path/to/project -DUSE_PREBUILT_TOOLS=no --host-platform-flag=USE_PREBUILT_TOOLS=no`
```
Описание желаемого поведения пропишите в `ya.conf`:
```bash
[host_platform_flags]
USE_PREBUILT_TOOLS = "no"

[flags]
USE_PREBUILT_TOOLS = "no"
```
Теперь опишите alias, который будет включаться по аргументу, переменной окружения или выставите значения в любом `ya.conf`:
```bash
[[alias]]
[alias.host_platform_flags]
USE_PREBUILT_TOOLS = "no"

[alias.flags]
USE_PREBUILT_TOOLS = "no"

[alias._settings.arg]
names = ["-p", "–disable-prebuild-tools"]
help = "Disable prebuild tools"
visible = true

[alias._settings.env]
name = "YA_DISABLE_PREBUILD_TOOLS"

[alias._settings.conf]
name = "disable_prebuild_tools"
```
Теперь для активации поведения используйте один из следующих способов:
```bash
# Длинный аргумент:
  `ya make path/to/project --disable-prebuild-tools`
# Короткий аргумент:
  `ya make path/to/project -p`
# Переменная окружения:
  `YA_DISABLE_PREBUILD_TOOLS=yes ya make path/to/project`
# Значение в конфиге:
  echo "\ndisable_prebuild_tools=true\n" >> path_proect/$USER/ya.conf
  ya make path/to/project
```
#### Работа с несколькими alias-ами

Alias-ы в `ya` предлагают гибкий способ для упрощения и автоматизации командных вызовов, предоставляя возможность группировать часто используемые аргументы. Эта возможность не ограничивается лишь одним alias-ом или одним файлом, а позволяет создавать и применять множество alias-ов, разбросанных по различным файлам конфигурации. При этом alias-ы из разных файлов дополняют друг друга, а не перезаписывают настройки.

#### Множественные alias-ы

Можно создавать любое количество alias-ов, включая их в один или несколько файлов конфигурации. Это обеспечивает значительную гибкость в настройке среды разработки.

Alias-ы, определенные в разных файлах, не конфликтуют и не заменяют друг друга, что позволяет комбинировать различные конфигурационные файлы без риска потери настроек.

**Пример с множественными файлами**
```bash
# path/to/first/ya.conf
some_values = true

third_alias = true

[[alias]]
# ...
[alias._settings.conf]
name = "first_alias"

[[alias]]
# ...

# path/to/second/ya.conf
some_other_values = true
[[alias]]
# ...
[alias._settings.conf]
name = "third_alias"

[[alias]]
first_alias = true
# ...

```
В этом примере конфигурации alias-ов из двух разных файлов будут успешно применены и не повлияют друг на друга отрицательно.

#### Пример с использованием target_platform
```bash
[[alias]]

[[alias.target_platform]]  # --target-platform
platfom_name = "..."
build_type = "debug"  # --target-platform-debug
run_tests = true  # --target-platform-tests
target_platform_compiler = "c_compiler"  # --target-platform-c-compiler
# ...
[alias.target_platform.flags]  # --target-platform-flag
FLAG = true
OTHER_FLAG = "other_value"

[alias._settings.arg]  # Create argument consumer for alias
names = ["-a", "--my-cool-alias"]  # Short and long name
help = "This alias are awesome! It really helps me"  # Help string
visible = true  # make it visible in `ya make --help`
[alias._settings.env]  # Create environment consumer for alias, must starts with YA
name = "YA_MY_COOL_ALIAS"
[alias._settings.conf]  # Create config consumer for alias, can be enabled from any config-file
name = "my_cool_alias"
```

#### Семантика alias-ов

Внутри одного блока `[[alias]]` можно задавать произвольное количество опций и подопций.

##### Создание аргумента или переменной окружения

Для добавления аргумента или переменной окружения, используются ключи `[alias._settings.arg]` и `[alias._settings.env]`. Определенные таким образом настройки становятся доступными во всех подкомандах `ya`.

Для создания опции, которая будет существовать только в конкретной команде (например `make`), достаточно дописать между ключами `alias` и `settings` произвольный префикс:

`[alias.make._settings.args]` — будет активно только для `ya make ...`


#### Включение через конфигурационный файл

Ключ `[alias._settings.conf]` позволяет включить определенный alias через любой конфигурационный файл. Это добавляет уровень гибкости, позволяя активировать alias, даже если он описан в файле, который применяется раньше по порядку обработки.

Таким образом, появляется возможность применять один alias внутри другого, обеспечивая таким образом простейшую композицию.

#### Композиция

Если возникла необходимость вынести общую часть из alias-ов, воспользуйтесь композицией.
Пусть у вас есть два alias-а:
```bash
[[alias]]
first_value = 1
second_value = 2
[[alias._settings.arg]]
names = ["--first"]

[[alias]]
second_value = 2
third_value = 3
[[alias._settings.arg]]
names = ["--second"]
```
Чтобы вынести общую часть, создайте новый alias c параметром конфигурации:
```bash
[[alias]]
second_value = 2
[[alias._settings.conf]]
name = "common_alias"

[[alias]]
first_value = 1
common_alias = true  # Call alias
[[alias._settings.arg]]
names = ["--first"]

[[alias]]
common_alias = true  # Call alias
third_value = 3
[[alias._settings.arg]]
names = ["--second"]
```
Теперь при вызове `ya <команда> --second` применится alias `common_alias`, и выставится значение для `third_value`.

Особенности:
- Можно вызывать несколько alias-ов внутри другого alias-а.
- Глубина «вложенности» может быть любой.
- Есть защита от циклов.
- Можно использовать alias, объявленный позже места использования или находящийся в другом файле.
