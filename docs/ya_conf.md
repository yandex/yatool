
Конфигурационный файл включает в себя различные настройки для инструментария разработки.

[О генерация конфигурационного файла инструмента](gen-config.md) `ya`

Ниже представлен перевод на русский язык основных строк конфигурации и комментариев к ним.
```
# Сохранить конфигурацию в папку junk/{USER}/ya.conf или в ~/.ya/ya.conf
#
# Добавить все каталоги с символическими ссылками в модулях в список исключений (–auto-exclude-symlinks)
# auto_exclude_symlinks = false
#
# Скопировать конфигурацию проекта для Совместных Индексов, если она существует (–copy-shared-index-config)
# copy_shared_index_config = false
#
# detect_leaks_in_pytest = true
#
# Создать проект в актуальном формате на основе каталогов (–directory-based)
# directory_based = true
#
# eager_execution = false
#
# Исключить каталоги с определенными именами из всех модулей (–exclude-dirs)
# exclude_dirs = []
#
# Добавить модули с внешним корнем содержимого (–external-content-root-module)
# external_content_root_modules = []
#
# fail_maven_export_with_tests = false
#
# Генерировать тесты для зависимостей PEERDIR (–generate-tests-for-dependencies)
# generate_tests_for_deps = false
#
# Генерировать конфигурации запуска для тестов junit (–generate-junit-run-configurations)
# generate_tests_run = false
#
# Корень для файлов .ipr и .iws (–idea-files-root)
# idea_files_root = “None”
#
# Версия JDK проекта (–idea-jdk-version)
# idea_jdk_version = “None”
#
# Хранить файлы “.iml” в дереве корня проекта (по умолчанию хранятся в дереве корня исходного кода) (–iml-in-project-root)
# iml_in_project_root = false
#
# Сохранять относительные пути в файлах “.iml” (работает с --iml-in-project-root) (–iml-keep-relative-paths)
# iml_keep_relative_paths = false
#
# Создать минимальный набор настроек проекта (–ascetic)
# minimal = false
#
# oauth_exchange_ssh_keys = true
#
# oauth_token_path = “None”
#
# Не экспортировать test_data (–omit-test-data)
# omit_test_data = false
#
# Список статусов тестов, которые по умолчанию игнорируются. Используйте ‘-P’, чтобы увидеть все тесты. Допустимые статусы: crashed, deselected, diff, fail, flaky, good, internal, missing, not_launched, skipped, timeout, xfail, xfaildiff, xpass
# omitted_test_statuses = [“good”, “xfail”, “not_launched”,]
#
# Имя проекта Idea (.ipr и .iws файлы) (–project-name)
# project_name = “None”
#
# regenarate_with_project_update = “None”
#
# Стандартные размеры тестов для запуска (1 для маленьких, 2 для маленьких+средних, 3 для запуска всех тестов)
# run_tests_size = 1
#
# Не объединять модули тестов с их собственными библиотеками (–separate-tests-modules)
# separate_tests_modules = false
#
# setup_pythonpath_env = true
#
# strip_non_executable_target = “None”
#
# test_fakeid = “”
#
# use_atd_revisions_info = false
#
# use_command_file_in_testtool = false
#
# use_jstyle_server = false
#
# use_throttling = false
#
# Добавить общие флаги JVM_ARGS в стандартный шаблон junit (–with-common-jvm-args-in-junit-template)
# with_common_jvm_args_in_junit_template = false
#
# Генерировать модули корней содержимого (–with-content-root-modules)
# with_content_root_modules = false
#
# Генерировать длинные имена библиотек (–with-long-library-names)
# with_long_library_names = false
#
# ya_bin3_required = “None”
#
# ========== Настройки проекта Idea ===========================================
#
# Добавить цели Python 3 в проект (–add-py3-targets)
# add_py_targets = false
#
# Корневая директория для проекта CLion (-r, --project-root)
# content_root = “None”
#
# Эмулировать создание проекта, но ничего не делать (-n, --dry-run)
# dry_run = false
#
# Учитывать только отфильтрованное содержимое (-f, --filter)
# filters = []
#
# Старый режим: Включить генерацию полного графа целей для проекта. (–full-targets)
# full_targets = false
#
# Группировать модули Idea в соответствии с путями: (tree, flat) (–group-modules)
# group_modules = “None”
#
# Корневой путь проекта IntelliJ IDEA (-r, --project-root)
# idea_project_root = “None”
#
# Легкий режим для решения (быстрое открытие, без сборки) (-m, --mini)
# lite_mode = false
#
# Только доступные локально проекты являются модулями idea (-l, --local)
# local = false
#
# Путь к каталогу для вывода CMake на удаленном хосте (–remote-build-path)
# remote_build_path = “None”
#
# Имя конфигурации удаленного сервера, связанной с удаленным набором инструментов (–remote-deploy-config)
# remote_deploy_config = “None”
#
# Имя хоста, связанное с конфигурацией удаленного сервера (–remote-host)
# remote_deploy_host = “None”
#
# Путь к репозиторию arc на удаленном хосте (–remote-repo-path)
# remote_repo_path = “None”
#
# Генерировать конфигурации для удаленного набора инструментов с этим именем (–remote-toolchain)
# remote_toolchain = “None”
#
# Развертывать локальные файлы через синхронизационный сервер вместо наблюдателей за файлами (–use-sync-server)
# use_sync_server = false
#
# ========== Интеграция с плагином IDE ========================================
#
# Тип проекта для использования в ya project update при регенерации из Idea (–project-update-kind)
# project_update_kind = “None”
#
# Запустить ya project update для этих каталогов при регенерации из Idea (–project-update-targets)
# project_update_targets = []
#
# ========== Управление операциями Ya =========================================
#
# Количество потоков сборки (-j, --threads)
# build_threads = 2
#
# Строить как можно больше (-k, --keep-going)
# continue_on_fail = false
#
# Пользовательская директория сборки (определяется автоматически) (-B, --build-dir)
# custom_build_directory = “None”
#
# Установить стандартные требования к устройству, использовать None для отключения (–default-node-reqs)
# default_node_requirements_str = “None”
#
# Приоритеты загрузчиков и параметры (–fetcher-params)
# fetcher_params_str = “None”
#
# Количество потоков для линковки (–link-threads)
# link_threads = 0
#
# Не использовать кэши ymake при повторной попытке (–no-ymake-caches-on-retry)
# no_caches_on_retry = false
#
# Включить дополнительный кэш, основанный на динамических UID-ах только по содержимому [по умолчанию] (–content-uids)
# request_content_uids = false
#
# Установить значение nice для процессов сборки (–nice)
# set_nice_value = 10
#
# Использовать clonefile вместо жесткой ссылки на macOS (–use-clonefile)
# use_clonefile = true
#
# ========== Выборочное извлечение ============================================
#
# Заранее загружать директории, необходимые для сборки (–prefetch)
# prefetch = false
#
# ========== Вывод результатов сборки =========================================
#
# Обрабатывать выбранный вывод сборки хоста как результат (–add-host-result)
# add_host_result = []
#
# Обрабатывать выбранный вывод сборки как результат (–add-result)
# add_result = []
#
# Обрабатывать все выходные данные узла вместе с выбранным выходным результатом сборки (–all-outputs-to-result)
# all_outputs_to_result = false
#
# Не создавать никаких символических ссылок в исходном каталоге (–no-src-links)
# create_symlinks = true
#
# Строить по зависимостям DEPENDS в любом случае (–force-build-depends)
# force_build_depends = false
#
# Не строить по RECURSES (–ignore-recurses)
# ignore_recurses = false
#
# Путь для накопления результирующих двоичных файлов и библиотек (-I, --install)
# install_dir = “None”
#
# Каталог с результатами сборки (-o, --output)
# output_root = “None”
#
# Не создавать символические ссылки/копировать вывод для файлов с данным суффиксом, они все равно могут быть сохранены в кэше как результат (–no-output-for)
# suppress_outputs = []
#
# Корень хранилища результатов (–result-store-root)
# symlink_root = “None”
#
# ========== Вывод ============================================================
#
# Не создавать символические ссылки/копировать вывод для файлов с данным суффиксом, если не переопределено с помощью --add-result (–no-output-default-for)
# default_suppress_outputs = [“.o”, “.obj”, “.mf”, “…”, “.cpf”, “.cpsf”, “.srclst”, “.fake”, “.vet.out”, “.vet.txt”, “.self.protodesc”,]
#
# Печатать дополнительную информацию о ходе выполнения (–show-extra-progress)
# ext_progress = false
#
# Замаскировать пути к исходному и каталогу сборки в stderr (–mask-roots)
# mask_roots = “None”
#
# Не переписывать информацию вывода (ninja/make) (-T)
# output_style = “ninja”
#
# Показать статистику выполнения сборки (–stat)
# print_statistics = false
#
# Печатать время выполнения команд (–show-timings)
# show_timings = false
#
# Каталог дополнительного вывода статистики (–stat-dir)
# statistics_out_dir = “None”
#
# ========== Конфигурация платформы/сборки ====================================
#
# Тип сборки (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type (–build)
# build_type = “release”
#
# Платформа хоста (–host-platform)
# host_platform = “None”
#
# Отключить настройку ya make (–disable-customization)
# preset_disable_customization = false
#
# Целевая платформа (–target-platform)
# target_platforms = []
#
# ========== Локальный кэш ====================================================
#
# Автоочистка кэша результатов (–auto-clean)
# auto_clean_results_cache = true
#
# Включить кэш сборки (–ya-ac)
# build_cache = false
#
# Переопределение опций конфигурации (–ya-ac-conf)
# build_cache_conf_str = []
#
# Включить режим мастера кэша сборки (–ya-ac-master)
# build_cache_master = false
#
# Кодек кэша (–cache-codec)
# cache_codec = “None”
#
# Максимальный размер кэша (–cache-size)
# cache_size = 322122547200
#
# Пробовать альтернативное хранилище (–new-store)
# new_store = true
#
# Удалить все символические ссылки результатов, кроме файлов из текущего графа (–gc-symlinks)
# strip_symlinks = false
#
# TTL кэша результатов (–symlinks-ttl)
# symlinks_ttl = 604800
#
# Включить кэш инструментов (–ya-tc)
# tools_cache = false
#
# Переопределение опций конфигурации (–ya-tc-conf)
# tools_cache_conf_str = []
#
# Переопределение опций конфигурации (–ya-gl-conf)
# tools_cache_gl_conf_str = []
#
# Переопределение встроенного ini-файла кэша инструментов (–ya-tc-ini)
# tools_cache_ini = “None”
#
# Включить режим мастера кэша инструментов (–ya-tc-master)
# tools_cache_master = false
#
# Mаксимальный размер кэша инструментов (–tools-cache-size)
# tools_cache_size = 32212254720
# ========== Генерация графа ==================================================
#
# Сжать вывод ymake для снижения максимального использования памяти (–compress-ymake-output)
# compress_ymake_output = false
#
# Кодек для сжатия вывода ymake (–compress-ymake-output-codec)
# compress_ymake_output_codec = “zstd08_1”

# ========== Флаги функциональности ==========================================
#
# Включить новые функции для выводов каталогов (–dir-outputs-test-mode)
# dir_outputs_test_mode = false
#
# Включить вывод отладки (–dump-debug)
# dump_debug_enabled = false
#
# Использовать локальный исполнитель вместо Popen (–local-executor)
# local_executor = true
#
# Пробовать альтернативный исполнитель (–new-runner)
# new_runner = true
#
# Не валидировать target-platforms (–disable-platform-schema-validation)
# platform_schema_validation = false
#
# Отключить поддержку dir_outputs в исполнителе (–disable-runner-dir-outputs)
# runner_dir_outputs = true

# ========== Надежные опции ===========================================
#
# Настроить конфигурацию clang-tidy для проекта по умолчанию (–setup-tidy)
# setup_tidy = false

# ========== Тестирование =====================================================
#
# Использовать кэш файловой системы вместо кэша памяти (только чтение) (–cache-fs-read)
# cache_fs_read = false
#
# Использовать кэш файловой системы вместо кэша памяти (только запись) (–cache-fs-write)
# cache_fs_write = false
#
# Использовать кэш для тестов (–cache-tests)
# cache_tests = false
```
