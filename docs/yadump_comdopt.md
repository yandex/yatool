## Описание опций команд для ya dump

Для удобства использования и повышения гибкости команды ya dump предлагают разнообразные опции, которые можно разделить на несколько групп в соответствии с их применением и областью действия. 

Вот основные группы опций:

### Общие опции:

Эти опции могут быть применены к любой подкоманде и часто используются для настройки вывода информации или специфики выполнения.

```
h, --help          Print help. Use -hh for more options and -hhh for even more.
    -k, --keep-going    Build as much as possible насколько возможно
    -C=BUILD_TARGETS, --target=BUILD_TARGETS
                        Targets to build
  Platform/build configuration
    -D=FLAGS            Set variables (name[=val], "yes" if val is omitted)
    --host-platform-flag=HOST_PLATFORM_FLAGS
                        Host platform flag
    --target-platform=TARGET_PLATFORMS
                        Target platform
    --target-platform-flag=TARGET_PLATFORM_FLAG
                        Set build flag for the last target platform
    -d                  Debug build
    -r                  Release build
    --build=BUILD_TYPE  Build type (debug, release, profile, gprof, valgrind, valgrind-release, coverage, relwithdebinfo, minsizerel, debugnoasserts, fastdebug) https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type (default: release)
    --sanitize=SANITIZE Sanitizer type(address, memory, thread, undefined, leak)
    --race              Build Go projects with race detector
```

### Опции фильтрации:

Эти опции используются для фильтрации и уточнения данных, которые должны быть извлечены или представлены в результате выполнения подкоманды.

- `--ignore-recurses`: Игнорирование рекурсивных зависимостей (RECURSE тегов в ya.make файлах).
- `--no-tools`: Исключение зависимостей, связанных с инструментами сборки из вывода.
- `--from=<module>`, `--to=<module>`: Указание начальной и конечной точки для отображения пути зависимостей. Опции `--from` и `--to` можно указывать по несколько раз

### Опции форматирования:

Предоставляют дополнительные возможности для настройки формата вывода результатов выполнения подкоманд.

- `--json`: Вывод информации в формате JSON. Подходит, когда данные предполагается дальше обрабатывать программным путем.
- `--flat-json`, `--flat-json-files` (для подкоманды dep-graph):  Форматирует вывод графа зависимостей в плоский JSON список, фокусируясь на файловых зависимостях или дугах графа. Граф зависимостей обычно содержит файловые ноды и ноды команд. Опция `--flat-json-files` позволяет вывести только файловые ноды и зависимости между ними.
- `-q`, `--quiet` - ничего не выводить.

Поддержано большинство сборочных опций `ya make`
