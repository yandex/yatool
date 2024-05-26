## ya package

Команда пакетирования `ya package` позволяет собирать различные типы пакетов, описанных в специальных `JSON`-файлах, и публиковать в различных фиксированных конфигурациях.

### Синтаксис
Общий формат команды выглядит следующим образом:

`ya package [OPTION]... [PACKAGE DESCRIPTION FILE NAME(S)]...`

где:
- [OPTION] - это дополнительные флаги или ключи, которые модифицируют поведение выбранной подкоманды.
- [PACKAGE DESCRIPTION FILE NAME(S)] - Названия файлов описания пакета в формате `JSON` (package.json)

### Поддерживаемые форматы пакетов

Для пакетирования доступны следующие форматы, выбираемые ключом командной строки:

* `--tar` tar-архив (по умолчанию)
* `--debian` deb-пакет
* `--rpm` rpm-пакет
* `--docker` docker-образ
* `--wheel` Python wheel-пакет
* `--aar` aar - нативный пакет для Android
* `--npm` npm - пакет для Node.js

По умолчанию все программы, указанные в `package.json`, собираются в режиме “release”. Для настройки параметров сборки ознакомьтесь с [форматом описания](ya_package_format.md).

### Все опции пакетирования

- `--no-cleanup`: Не очищать временные директории.
- `--change-log=CHANGE_LOG`: Текст логов изменений или путь к существующему файлу с логами изменений.
- `--publish-to=PUBLISH_TO`: Опубликовать пакет в соотвествующем пакетном репозитории.
- `--strip`: Усечение бинарных файлов от отладочной информации (только отладочные символы: `strip -g`).
- `--full-strip`: Полное усечение бинарных файлов.
- `--no-compression`: Не сжимать`tar`-архив (только для параметра `--tar`).
- `--create-dbg`: Дополнительно создает пакет с отладочной информацией (работает только в случае использования `--strip` или `--full-strip`).
- `--key=KEY`: Ключ для подписания.
- `--debian`: Создание пакета в формате debian.
- `--tar`: Создание пакета в формате `tar` архива.
- `--docker`: Создание Docker-образа.
- `--rpm`: Создание пакета в формате `rpm`.
- `--wheel`: Создание пакета в формате `wheel`.
- `--wheel-repo-access-key=WHEEL_ACCESS_KEY_PATH`: Путь к ключу доступа к репозиторию `wheel`.
- `--wheel-repo-secret-key=WHEEL_SECRET_KEY_PATH`: Путь к секретному ключу для репозитория `wheel`.
- `--docker-registry=DOCKER_REGISTRY`: Реестр `Docker`.
- `--docker-repository=DOCKER_REPOSITORY`: Указать приватный репозиторий.
- `--docker-save-image`: Сохранить Docker-образ в архиве.
- `--docker-push`: Сохранить Docker-образ в реестре.
- `--docker-network=DOCKER_BUILD_NETWORK`: Параметр `--network` для команды `docker build`.
- `--raw-package`: Используется с `--tar` для получения содержимого пакета без упаковки.
- `--raw-package-path=RAW_PACKAGE_PATH`: Пользовательский путь для параметра `--raw-package`.
- `--codec=CODEC`: Имя кодека для сжатия `uc`.
- `--codecs-list`: Показать доступные кодеки для сжатия с использованием `--uc`.
- `--ignore-fail-tests`: Создать пакет независимо от того, прошли ли тесты или нет.
- `--new`: Использовать новый формат `JSON` для `ya package`.
- `--old`: Использовать старый формат `JSON` для `ya package`.
- `--not-sign-debian`: Не подписывать `debian`-пакет.
- `--custom-version=CUSTOM_VERSION`: Произвольная версия пакета.
- `--debian-distribution=DEBIAN_DISTRIBUTION`: Debian-дистрибуция (по умолчанию: `unstable`).
- -`-arch-all`: Использовать “Architecture: all” в `debian`.
- `--force-dupload`: Включить `dupload --force`.
- `-z=DEBIAN_COMPRESSION_LEVEL`, `--debian-compression=DEBIAN_COMPRESSION_LEVEL`: Уровень сжатия deb-файла (none, low, medium, high).
- `-Z=DEBIAN_COMPRESSION_TYPE`, `--debian-compression-type=DEBIAN_COMPRESSION_TYPE`: Тип сжатия, используемый при создании `deb-файла (допустимые типы: `gzip`, `xz`, `bzip2`, `lzma`, `none`).
- `--data-root=CUSTOM_DATA_ROOT`: Путь к пользовательской директории данных, по умолчанию `<source root>/../data`.
- `--dupload-max-attempts=DUPLOAD_MAX_ATTEMPTS`: Количество попыток запуска `dupload` при сбое (по умолчанию: 1).
- `--nanny-release=NANNY_RELEASE`: Уведомить `Nanny` о новом релизе.
- `--dupload-no-mail`: Включить `dupload` c `--no-mail`.
- `--overwrite-read-only-files`: Перезаписывать файлы с правами только на чтение в пакете.
- `--ensure-package-published`: Убедиться, что пакет доступен в репозитории.

### Начало работы

Для сборки пакета нужно запустить команду с указанием пути к `JSON`-описанию и необходимого формата. Пакет будет собран в текущей директории. Если формат пакетирования не указан, будет создан `tar`-архив.

#### Пример

`ya package devtools/project/package/hello_world.json`

#### Результат:
```
tar -tvzf ./some-package-hello-world.85c7e374108166bfc1b2a47ca888830965a07708.tar.gz
drwxrwxr-x 0/0 0 2021-03-11 06:58 some_package_dir/
-rwxrwxr-x 0/0 19488 2021-03-11 06:58 some_package_dir/hello_world
```
Получаем архив с исполняемым файлом внутри директории `some_package_dir`.

#### Описание архива:
```
{
    "meta": {
        "name": "some-package-hello-world",
        “maintainer”: “Programmers <mail>”,
        "description": "hello world test package",
        "version": "{revision}"
    },
    "build": {
        "targets": [
            "devtools/project/hello_world"
        ]
    },
    "data": [
        {
            "source": {
                "type": "BUILD_OUTPUT",
                "path": "devtools/project/hello_world/hello_world"
            },
            "destination": {
                "path": "/some_package_dir/"
            }
        }
    ]
}
```

#### Объяснение JSON-описания

- Имя пакета:`some-package-hello-world`
- Версия пакета: текущая ревизия репозитория, определяемая автоматически
- Содержимое пакета: программа `devtools/project/hello_world` будет сложена в каталог `some_package_dir`.

По умолчанию `ya package` создаёт пакет в текущем каталоге. С помощью опций `-O/--package-output` вы можете переопределить каталог, куда `ya package` сохранит создаваемые пакеты.


ya package, являясь надстройкой над системой сборки, стремится к тем же идеалам, что и ya make — предоставляя герметичную воспроизводимую (не бинарно) сборку артефактов с минимально необходимым набором CLI параметров для сборки пакета.

Описание package.json включает в себя все, что относится к:
- Конфигурации сборки: целевые программы, сборочные флаги, платформы и прочее
- Зависимостям из репозитория и ресурсам Sandbox
- Способам раскладки артефактов внутри пакета, права доступа, имена файлов/каталогов
- Метаданным, относящимся к пакету и его сборке
- Параметрам пакетирования, которые неразрывно связаны с описываемым пакетом
### Основные опции
```
    -d                  Debug build
    -r                  Release build
    --sanitize=SANITIZE Sanitizer type(address, memory, thread, undefined, leak)
    --sanitizer-flag=SANITIZER_FLAGS
                        Additional flag for sanitizer
    --lto               Build with LTO
    --thinlto           Build with ThinLTO
    --sanitize-coverage=SANITIZE_COVERAGE
                        Enable sanitize coverage
    --afl               Use AFL instead of libFuzzer
    --musl              Build with musl-libc
    --pch               Build with Precompiled Headers
    --hardening         Build with hardening
    --race              Build Go projects with race detector
    --cuda=CUDA_PLATFORM
                        Cuda platform(optional, required, disabled) (default: optional)
    -j=BUILD_THREADS, --threads=BUILD_THREADS
                        Build threads count (default: 32)
    --checkout          Checkout missing dirs
    --report-config=REPORT_CONFIG_PATH
                        Set path to TestEnvironment report config
    -h, --help          Print help
    -v, --verbose       Be verbose
    --ttl=TTL           Resource TTL in days (pass 'inf' - to mark resource not removable) (default: 14)
```
### Опции запуска тестов"
```
    -t, --run-tests     Run tests (-t runs only SMALL tests, -tt runs SMALL and MEDIUM tests, -ttt runs SMALL, MEDIUM and FAT tests)
    -A, --run-all-tests Run test suites of all sizes
    --add-peerdirs-tests=PEERDIRS_TEST_TYPE
                        Peerdirs test types (none, gen, all) (default: none)
    --test-tool-bin=TEST_TOOL_BIN
                        Path to test_tool binary
    --test-tool3-bin=TEST_TOOL3_BIN
                        Path to test_tool3 binary
    --profile-test-tool=PROFILE_TEST_TOOL
                        Profile specified test_tool calls
```

## Пример
`ya package <path to json description>  Create tarball package from json description`
