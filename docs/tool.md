## ya tool

Утилита ya tool позволяет пользователям запускать различные инструменты, предоставляемые пакетом ya. 

### Использование

Для запуска инструмента, предоставляемого утилитой ya, используйте следующий синтаксис:

`ya tool <подкоманда> [параметры]`

### Список доступных инструментов

Чтобы получить список доступных инструментов, просто выполните команду: `ya tool`

Эта команда отобразит список инструментов, которые можно выполнить с помощью утилиты:

- `atop`: Утилита мониторинга системных и процессорных ресурсов
- `black`: Стилизатор `Python` кода (только для `Python 3`)
- `buf`: Линтер и детектор изменений для `Protobuf`
- `c++`: Запуск компилятора `C++`
- `c++filt`: Запуск `c++filt` (деманглирование символов `C++`)
- `cc`: Запуск компилятора `C`
- `clang-format`: Запуск стилизатора исходного кода `Clang-Format`
- `clang-rename`: Запуск инструмента рефакторинга `Clang-Rename`
- `gcov`: Запуск программы покрытия тестов `gcov`
- `gdb`: Запуск `GNU Debugger gdb`
- `gdbnew`: Запуск `gdb` для `Ubuntu 16.04` или позднее
- `go`: Запуск инструментов `Go`
- `gofmt`: Запуск форматировщика кода `Go gofmt`
- `llvm-cov`: Запуск утилиты покрытия `LLVM`
- `llvm-profdata`: Запуск утилиты профильных данных `LLVM`
- `llvm-symbolizer`: Запуск утилиты символизации `LLVM`
- `nm`: Запуск утилиты nm для отображения символов из объектных файлов
- `objcopy`: Запуск утилиты `objcopy` для копирования и преобразования объектных файлов
- `strip`: Запуск утилиты strip для удаления символов из бинарных файлов

###  Параметры инструментов ya tool

Чтобы получить подробную информацию о параметрах и аргументах, поддерживаемых каждым инструментом, используйте опцию -h с конкретной подкомандой: `ya tool <подкоманда> -h`

**Пример**

`ya tool buf -h`

Эта команда предоставит подробное описание использования инструмента, его параметров и примеры.
```
Usage:
  buf [flags]
  buf [command]

Available Commands:
  beta            Beta commands. Unstable and will likely change.
  check           Run lint or breaking change checks.
  generate        Generate stubs for protoc plugins using a template.
  help            Help about any command
  image           Work with Images and FileDescriptorSets.
  ls-files        List all Protobuf files for the input location.
  protoc          High-performance protoc replacement.

Flags:
  -h, --help                help for buf
      --log-format string   The log format [text,color,json]. (default "color")
      --log-level string    The log level [debug,info,warn,error]. (default "info")
      --timeout duration    The duration until timing out. (default 2m0s)
  -v, --version             version for buf

Use "buf [command] --help" for more information about a command.
```
### Дополнительные параметры

* `--ya-help` предоставляет информацию об использовании конкретного инструмента, запускаемого через `ya tool`
Синтаксис команды: `ya tool <подкоманда> --ya-help`
* `--print-path` используется для вывода пути к исполняемому файлу конкретного инструмента, запускаемого через утилиту `ya tool`. Параметр позволяет пользователю узнать точное местоположение инструмента на файловой системе. Например, чтобы получить путь к исполняемому файлу инструмента `clang-format`, выполните следующую команду: `ya tool clang-format --print-path`
* `--force-update` используется для проверки наличия обновлений и обновления конкретного инструмента, запускаемого через утилиту 'ya tool`. Например, чтобы принудительно обновить и затем запустить инструмент `go`, используйте следующую команду: `ya tool go --force-update`
