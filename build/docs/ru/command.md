## Основные сборочные инструменты

Основным сборочным инструментом является [команда `ya make`](ya_make.md)

## Командная строка (формат, опции)

Команды сборки интегрированы в универсальную утилиту `ya`. Утилита предлагает широкий спектр функций и параметров командной строки для адаптации процесса сборки под различные требования проекта.
Запуск `ya` без параметров выводит справку:
```bash
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

`ya [базовые опции] Команда [подкоманда] [опции команды]`

Базовые опции:
- `--precise` - отображает точные временные метки в логе.
- `--profile` - запускает профайлер cProfile для бинарного файла ya.
- `--error-file ERROR_FILE` - указывает путь к файлу, в который будут направлены сообщения об ошибках.
- `--keep-tmp` - сохраняет временные файлы после выполнения сборки.
- `--no-logs` - отключает сохранение логов в файл.
- `--no-report` - отключает создание отчетов.
- `--no-tmp-dir` - не создаёт временную директорию при выполнении.
- `--print-path` - выводит путь к исполняемому файлу ya и завершает работу.
- `--version` - выводит информацию о версии утилиты.
- `-v`, `--verbose-level` - настраивает уровень подробности выводимых сообщений на экран.

Команды:
- [`analyze_make.md`](analyze_make.md) -анализирует временя выполнения сборки
- [`dump`](ya_dump.md) - набор подкоманд для контроля различных этапов сборки.
- [`gc`](gc.md) - выполняет сборку мусора.
- [`gen-config`](gen-config.md) - генерирует конфигурацию по умолчанию для `ya`.
- [`make`](ya_make.md) - производит сборку и запускает тесты.
- [`package`](package.md) - создает пакет, используя `JSON` описание пакета.
- [`project`](project.md) - создает или изменяет файл `ya.make` проекта.
- [`style`](style.md) - запускает утилиты для проверки стиля кода.
- [`test`](test.md) - собирает и выполняет все тесты (является аналогом для `ya make -A`).
- [`tool`](tool.md) - исполняет определённый инструмент.

Следующие опции пока не работают, но предусмотрены для реализации в будущем:
- `ide` - набор подкомад для генерации проект для различных `IDE`.
- `java` - набор подкоманд для контроля различных этапов сборки `Java` кода.

Для получения более детальной помощи по каждой команде и её параметрам используйте опции `-h`, `-hh` или `-hhh` после соответствующей команды.   
Это позволит просмотреть расширенное описание и примеры использования.

Примеры:
- Запуск тестов только малого и среднего размера:
  ```bash
  ya test -tt
  ```
- Запуск только малых тестов:
  ```bash
  ya test -t
  ```
- Показать все запуски ya:
  ```bash
  ya dump debug
  ```
- Запуск всех тестов:
  ```bash
  ya test
  ```
- Собрать отладочную информацию о последнем запуске ya:
  ```bash
  ya dump debug last
  ```
`ya` является платформо-независимой утилитой и может быть запущена на большинстве операционных систем, включая `Linux`, `macOS`, и `Windows`.

Для запуска `ya` в `Windows` без указания полного пути, её можно добавить в переменную окружения `PATH`.

Для `Linux/Bash` можно использовать следующую команду:
```bash
echo "alias ya='~/yatool/ya'" >> ~/.bashrc
source ~/.bashrc
```
`ya` использует файлы конфигурации в формате [`TOML`](https://github.com/toml-lang/toml), которые должны быть размещены в корне проекта или в специально выделенной директории. 

Более с подробной информацией про конфигурационные файлы можно [ознакомиться](gen-config.md "Конфигурация ya")
