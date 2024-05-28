## ya style

Команда `ya style` используется для испрвления стиля кода на языках `C++`, `Python` и `Go`. 

Файлы могут быть переданы непосредственно или обработаны рекурсивно в директориях.

### Синтаксис 

`ya style [OPTION]... [FILE OR DIR]...`

### Опции style
```
--reindent     Выровнять строки по минимальному отступу
-h, --help     Распечатать справку
```
Ключи для фильтрации файлов по языку могут быть комбинированы:
```
--py
--cpp
--go
--yamake
```
### Линтеры по языкам

#### C++
Для форматирования `C++` кода используется утилита `clang-format`.

#### Python
Команда поддерживает защиту блоков кода с помощью `# fmt: on/off`. Для форматирования используется линтер `black`.

#### Go
В качестве линтера для `Go` используется утилита `yoimports`, поэтому при запуске `ya style` обновятся только `import`-ы.

Для полноценного линтинга нужно использовать `yolint`

### Примеры использования ya style
```
ya style file.cpp  обновить стиль файла file.cpp
ya style           обновить стиль текста из <stdin>, перенаправить результат в <stdout>
ya style .         обновить стиль всех файлов в данной директории рекурсивно
ya style folder/   обновить стиль всех файлов во всех подпапках рекурсивно
ya style . --py    обновить стиль всех python-файлов в данной директории рекурсивно
```

### Java
Для `java` существует отдельная команда `ya jstyle`

Отформатировать Java-код.

`ya jstyle [OPTION]... [TARGET]...`

Команда `ya jstyle` используется для форматирования Java-кода. Базируется на форматтере `JetBrains IntelliJ IDEA`.

#### Опции
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

#### Пример
```
  ya jstyle path/to/dir  restyle all supported files in directory
```
