# ya buf

Команда позволяющая запускать линтинг и проверять поломки обратной совместимости для ваших .proto файлов. Команда интегрирует в сборку утилиту [buf](https://buf.build/).

`Usage: ya buf <subcommand>`

## Доступные команды
```
 build      Build all .proto files from the target and output an Image
 check      Check that the target has no breaking changes compared to the input image
 lint       Lint .proto files
```

Все сабкоманды поддерживают работу с кастомным конфигом для ваших прото файлов. Достаточно положить `buf.yaml` файл в `TARGET` директорию. 
Описание формата конфига можно посмотреть в [документации buf](https://docs.buf.build/configuration). В кастомном конфиге можно переопределять только `lint` и `breaking` секции,
остальные настройки игнорируются.

## ya buf lint
```
ya buf lint [OPTION]... [TARGET]

Example:
    ya buf lint alice/megamind/protos

```

Команда линтера для ваших .proto файлов. Команда ищет несоответствия стилю и выводит их на экран, если такие несоответствия есть.

Если настройки линтинга по-умолчанию не подходят, их можно переопределить положив в `TARGET` папку файл `buf.yaml`. 
Примеры настроек можно посмотреть в [документации](https://docs.buf.build/lint-configuration).

## ya buf build
```
ya buf build [OPTION]... [TARGET]

Example:
    ya buf build -o trunk-image.bin alice/megamind/protos
    ya buf build --output my-image.json alice/megamind/protos
```
Команда для того чтобы сделать слепок всех .proto файлов в `TARGET` для последующего применения детектора поломок обратной совместимости.
Удобно, например, получить слепок продакшен ветки и потом сравнить его с релиз-кандидатом.

## ya buf check
```
ya buf check [OPTION]... [TARGET]

Example:
    ya buf check alice/megamind/protos -a trunk-image.bin
    ya buf check --against-input my-image.json alice/megamind/protos
```

Команда запускает детектор поломок обратной совместимости в `TARGET` против собранного заранее слепка. В случае ошибок команда выдаст их на экран, а так же завершится с ненулевым статус кодом. 
Влиять на настройки детектора можно через файл `buf.yaml` лежащий в `TARGET`, список всех настроек можно посмотреть в [документации](https://docs.buf.build/breaking-configuration).
