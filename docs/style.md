## ya style

Привести С++, Python или Go (только import-ы) код к аркадийному стилю.

`ya style [OPTION]... [FILE OR DIR]...`

## Опции style
```
--reindent     Выровнять строки по минимальному отступу
-h, --help     Распечатать справку
```

Фильтрация файлов по языку, ключи можно комбинировать:
```
--py
--cpp
--go
--yamake
```

## Примеры style
```
ya style file.cpp  обновить стиль файла file.cpp
ya style           обновить стиль текста из <stdin>, перенаправить результат в <stdout>
ya style .         обновить стиль всех файлов в данной директории рекурсивно
ya style folder/   обновить стиль всех файлов во всех подпапках рекурсивно
ya style . --py    обновить стиль всех python-файлов в данной директории рекурсивно
```

## C++

В качестве линтера для C++ используется утилита clang-format

## Python

Можно защищать блоки кода с помощью  `# fmt: on/off`.

В качестве линтера для Python используется black

Файл конфигурации для black

## Go

В качестве линтера для Go используется утилита yoimports, поэтому при запуске `ya style` обновятся только import-ы.

Для полноценного линтинга нужно использовать yolint


## Java
Для java существует отдельная команда ya jstyle

Отформатировать Java-код.

`ya jstyle [OPTION]... [TARGET]...`

Запускает исправление стиля java-кода. Основан на форматтере JetBrains Itellij IDEA

## Опции
```
  Bullet-proof options
    -h, --help          Print help
  Advanced options
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30GiB)
    -C=BUILD_TARGETS, --target=BUILD_TARGETS
                        Targets to build
  Authorization options
    --key=SSH_KEYS      Path to private ssh key to exchange for OAuth token
    --token=OAUTH_TOKEN oAuth token
    --user=USERNAME     Custom user name for authorization
```

## Пример
```
  ya jstyle path/to/dir  restyle all supported files in directory
```
