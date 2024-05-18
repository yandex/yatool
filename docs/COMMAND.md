## Основные сборочные инструменты

Основным сборочным инструментом является команда `ya make`

## Командная строка (формат, опции)

Команды сборки интегрированы в универсальную утилиту `ya`. Утилита предлагает широкий спектр функций и параметров командной строки для адаптации процесса сборки под различные требования проекта.
Запуск `ya` без параметров выводит справку:
```
Yet another build tool.

Usage: ya [--precise] [--profile] [--error-file ERROR_FILE] [--keep-tmp]
          [--no-logs] [--no-report] [--no-tmp-dir] [--print-path] [--version]
          [-v]

Options:
  --precise             show precise timings in log
  --profile             run python profiler for ya binary
  --error-file ERROR_FILE
  --keep-tmp
  --no-logs
  --no-report
  --no-tmp-dir
  --print-path
  --version
  -v, --verbose-level

Available subcommands:
  dump            Repository related information
  gc              Collect garbage
  gen-config      Generate default ya config
  ide             Generate project for IDE
  java            Java build helpers
  make            Build and run tests
                  To see more help use -hh/-hhh
  package         Build package using json package description in the release build type by default.
  style           Run styler
  test            Build and run all tests
                  ya test is alias for ya make -A
  tool            Execute specific tool
Examples:
  ya test -tt         Build and run small and medium tests
  ya test -t          Build and run small tests only
  ya dump debug       Show all items
  ya test             Build and run all tests
  ya dump debug last  Upload last debug item
```
### Общее использование

ya [опции] [подкоманды]

Основные опции:
- --precise - отображает точные временные метки в логе.
- --profile - запускает профайлер Python для бинарного файла ya.
- --error-file ERROR_FILE - указывает путь к файлу, в который будут направлены сообщения об ошибках.
- --keep-tmp - сохраняет временные файлы после выполнения сборки.
- --no-logs - отключает вывод логов.
- --no-report - отключает создание отчетов.
- --no-tmp-dir - не создаёт временную директорию при выполнении.
- --print-path - выводит путь к исполняемому файлу ya.
- --version - выводит информацию о версии утилиты.
- -v, --verbose-level - настраивает уровень подробности выводимых сообщений.

Команды:
- dump - отображение информации, связанной с репозиторием.
- gc - выполняет сборку мусора.
- gen-config - генерирует конфигурацию по умолчанию для ya.
- ide - генерирует проект для интегрированной среды разработки.
- java - вспомогательные функции для сборки Java проектов.
- make - производит сборку и запускает тесты.
- package - создает пакет, используя JSON описание пакета.
- style - запускает утилиты для проверки стиля кода.
- test - собирает и выполняет все тесты (является алиасом для ya make -A).
- tool - исполняет определённый инструмент.

Для получения более детальной помощи по каждой команде и её параметрам используйте опции -h, -hh или -hhh после соответствующей команды. Это позволит просмотреть расширенное описание и примеры использования.

Примеры:
- Запуск тестов только малого и среднего размера:
  ```ya test -tt```
- Запуск только малых тестов:
  ```ya test -t```
- Показать все элементы с помощью dump:
  ```ya dump debug```
- Запуск всех тестов:
  ```ya test```
- Загрузить последний отладочный элемент:
  ```ya dump debug last```

`ya` является платформо-независимой утилитой и может быть запущена на большинстве операционных систем, включая Linux, macOS, и Windows.
Для запуска `ya` в Windows без указания полного пути, её можно добавить в переменную окружения PATH.
Для Linux/Bash можно использовать следующую команду:
```
echo "alias ya='~/yatool/ya'" >> ~/.bashrc
source ~/.bashrc
```
`ya` использует файлы конфигурации в формате `TOML`, которые должны быть размещены в корне проекта или в специально выделенной директории. 

Более с подробной информацией про конфигурационные файлы можно [ознакомиться](gen-config.md "Конфигурация ya")
