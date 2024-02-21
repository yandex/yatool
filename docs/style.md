# ya jstyle

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


