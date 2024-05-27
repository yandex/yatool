## Формат описания пакетов
Описание пакета состоит из следующих секций:
- [meta](#meta)
- [include](#include)
- [build](#build)
- [data](#data)
- [params](#params)
- [userdata](#userdata)
- [postprocess](#postprocess)

### meta
Раздел с метаинформацией включает в себя название, версию, описание пакета и прочие поля, которые применяются для различных форматов упаковки.

```json
{
    "name": "project-some-package-name",
    "maintainer": "programmers <user@mail>",
    "description": "Some package description",
    "version": "Version with templates from {revision}, {branch}, {sandbox_task_id}, {changelog_version}",
    "pre-depends": ["package1", "package2"],
    "depends": ["package3", "package4"],
    "provides": ["package5"],
    "conflicts": ["package6", "package7"],
    "replaces": ["package8"],
    "build-depends": ["package"],
    "homepage": "https://<homepage>",
    "rpm_release": "The number of times this version of the software was released",
    "rpm_license": "The license of the software being packaged"
}
```

* `name` – имя пакета
* `maintainer` – ответственный за пакет
* `description` – описание пакета
* `version` – версия пакета
* `pre-depends` – указание зависимостей для сборки deb-пакета [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#syntax-of-relationship-fields)
* `depends` – указание зависимостей для сборки deb-пакета [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#syntax-of-relationship-fields)
* `provides` – указание Provides для сборки deb-пакета [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#virtual-packages-provides)
* `conflicts` – указание конфликтующих пакетов для сборки deb-пакета [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#conflicting-binary-packages-conflicts)
* `replaces` –  указание замещающих пакетов [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#overwriting-files-and-replacing-packages-replaces)
* `build-depends` – указание списка зависимостей при сборке пакета [подробнее](https://www.debian.org/doc/debian-policy/ch-relationships.html#relationships-between-source-and-binary-packages-build-depends-build-depends-indep-build-depends-arch-build-conflicts-build-conflicts-indep-build-conflicts-arch)
* `homepage` – возможность указать домашнюю страницу пакетируемого проекта
* `rpm_release` – номер релиза для сборки RPM-пакета
* `rpm_license` – указание лицензии для сборки RPM-пакета
* `rpm_buildarch` – указание типа архитектуры для сборки RPM-пакета

Поддерживаемые подстановки для полей мета-инофрмации:
* `revision` – текущая ревизия, берется от корня репозитория
* `branch` – имя текущей ветки
* `changelog_version` – версия из changelog-файла. Подстановка доступна при сборке deb-пакета с указанием пути до `changelog` через ключ команды ```--change-log```

## include { #include }
Содержит список путей пакетов (относительно корня Аркадии), содержимое которых нужно включить: включаются все секции, при этом поля из секции `meta` переопределяются в порядке следования записей секции `include` (последним ее переписывает `meta` текущего пакета).

[Пример](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/create_tarball/package_with_include_include.json).

```json
"include": [
        "devtools/ya/package/tests/create_tarball/package_base.json"
    ]
```

Есть возможность включить содержимое включаемого пакета в текущий с префиксом в путях:
```json
"include": [
        {
            "package": "devtools/ya/package/tests/create_tarball/package_with_include.json",
            "targets_root": "include_root"
        }
    ]
```

## build { #build }
В данной секции указываются директивы для сборки содержимого пакета, командой ```ya make```, единственный обязательный ключ - `targets`:

```json
{
    "targets": [
        "devtools/dummy_arcadia/hello_world",
        "devtools/dummy_arcadia/hello_java/hello"
    ],
    "build_type": "<build type (debug, release, etc.)>",
    "sanitize": "<sanitizer (memory, address, etc.)>",
    "sanitizer_flags": "<sanitizer flags>",
    "lto": <build with LTO (true/false)>,
    "thinlto": <build with ThinLTO (true/false)>,
    "musl": <build with MUSL (true/false)>,
    "race": <build golang with race sanitizer (true/false)>,
    "add-result": [
        ".java"
    ],
    "flags": [
        {
            "name": "name",
            "value": "value"
        }
    ],
    "target-platforms": [
        "platform1",
        "platform2"
    ]
}
```
* `targets` - цели для сборки (параметр `-C` для ```ya make```)
* `build_type` - профиль сборки
* `sanitize` - сборка с санитайзером
* `sanitizer_flags` - флаги для санитайзера
* `lto` - сборка с LTO
* `thinlto` - сборка с ThinLTO
* `musl` - сборка с MUSL
* `race` - сборка go c race санитайзером
* `add-result` - директива ```--add-result``` для запуска ```ya make```
* `flags` - флаги для сборки, будут передаваться параметром ```--target-platform-flag=<name>=<value>``` в ```ya make```
* `target-platforms` – таргет-платформы, под которые надо производить сборку

Поле `flags` поддерживает подстановки из секции `meta`, а также дополнительные:
* `package_name` – название пакета
* `package_version` – текущая версия пакета
* `package_full_name` – название пакета с версией
* `package_root` – директория, где находится json-описание пакета относительно корня Аркадии

{% note warning %}

Кастомизация флагов является помехой для использования общего кэша. К ней может дополнительно понадобиться [отдельный проектный кэш](https://docs.yandex-team.ru/ya-make/usage/ya_make/yt_store#ispolzovanie-chastnogo-yt-kesha-proekta).

{% endnote %}

Данная секция может состоять из нескольких секций (запусков ```ya make```), каждая секция в этом случае должна иметь уникальный ключ:
```json
"build": {
         "build_1": {
            "targets": [
                "devtools/dummy_arcadia/hello_world"
            ],
            "flags": [
                {
                    "name": "CFLAGS",
                    "value": "-DMYFLAG"
                }
            ]
        },
        "build_2": {
            "targets": [
                "devtools/dummy_arcadia/hello_world"
            ]
        }
    }
```
Потом ключи (`build_1` или `build_2`) нужно использовать в секции `data` в элементах `BUILD_OUTPUT`.

{% note tip %}

Цели внутри одной секции строятся вместе, а разные секции - последовательно. Последовательная сборка секций связана с тем, что разные секции могут собирать одинаковые цели с разными флагами и возникнут проблемы из-за совпадения имён. Каждая секция будет также отдельно конфигурироваться для сборки. В связи с этим рекомендуется собирать все цели в одной конфигурации в рамках одной секции: это сократит время конфигурирования, может увеличить параллелизм в сборке и ускорить её.

{% endnote %}

#### Strip
Для получения нестрипнутых бинарных файлов нужно добавить:
```json
"flags": [
            {
                "name": "NO_STRIP",
                "value": "yes"
            }
        ]
```

#### Custom version { #custom-version }
С помощью `--custom-version=<str>` можно задать шаблон для версии пакета.
Он имеет более высокий приоритет над версией, указанной в файле описания.
Custom version может быть зашита в собираемые программы в ya package.
Для получения строки с custom version следует использовать [GetCustomVersion()](https://a.yandex-team.ru/arcadia/build/scripts/c_templates/svnversion.h?rev=r11141829#L30) из `library/cpp/svnversion/svnversion.h` для cpp и [Info.CustomVersion](https://a.yandex-team.ru/arcadia/library/go/core/buildinfo/buildinfo.go?rev=r11141829#L55) для go.

{% note info %}

Любая информация про версии, зашиваемая в программы, по умолчанию не обновляется при сборке.
Это желаемое поведение для ускорения сборки и тестирования пакетов.
Для принудительного обновления информации о версии нужно использовать `-DFORCE_VCS_INFO_UPDATE=yes` (параметр `force_vcs_info_update` у таски `YA_PACKAGE_2`)

{% endnote %}

Так же custom версию, можно передать в сборку (не рекомендуется):

```json
"flags": [
            {
                "name": "PACKAGE_VERSION",
                "value": "{package_version}"
            }
        ]
```
см. [пример целиком](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/create_tarball/package_with_version_flag.json).


## data { #data }
В этой части описания содержится список элементов, каждый из которых имеет `source` и `destination`.
В `source` обязательно должен быть указан тип(`type`) элемента, один из `BUILD_OUTPUT`, `ARCADIA`, `RELATIVE`, `DIRECTORY`, `SANDBOX_RESOURCE` и `TEST_DATA`. Остальные поля в `source` зависят от указанного типа элемента.
В `destination` достаточно указать только `path`, но можно также явно установить права и степень их распространения через `attributes`. Если на этапе подготовки пакета `destination` файлы уже присутствуют в пакете, в случае эквивалентности файлов перезапись будет опущена, в случае расхождения контента файлов будет ошибка сборки пакета (sanity check). Если вы перезаписываете файлы в пакете с умыслом, следует явно указывать ключ ```--overwrite-read-only-files```

Поле `path` поддерживает те же подстановки, которые можно использовать в секции `meta`, а также дополнительные:
* `package_name` – название пакета
* `package_version` – текущая версия пакета
* `package_full_name` – название пакета с версией
* `package_root` – директория, где находится json-описание пакета относительно корня Аркадии

Блок `destination` может принимать дополнительные параметры:
* ```"temp": true``` – пометка, позволяющая поместить результат работы блока во временное хранилище, доступное далее через [TEMP](#data-temp)
* ```"archive": "/path/to/archive_name.tgz"``` – используется вместо `path`, чтобы запаковать в архив результат работы блока `source`

```json
{
  "meta": {
    "name": "archive example"
  },
  "data": [
    {
      "source": {
        "type": "ARCADIA",
        "path": "contrib/python/hg/mercurial/help"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "ARCADIA",
        "path": "contrib/python/hg/mercurial/templates"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "ARCADIA",
        "path": "vcs/hg/server/config/server.rc"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/default.d/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "ARCADIA",
        "path": "vcs/hg/common/config/common.rc"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/default.d/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "TEMP",
        "path": "tmp/hg_server"
      },
      "destination": {
        "archive": "/crossplatform/hg_server.tgz"
      }
    }
  ]
}
```

Ниже перечислены несколько основных примеров использования элементов `data`:

### BUILD_OUTPUT { #data-build-output }
```json
{
    "source": {
        "type": "BUILD_OUTPUT",
        "path": "devtools/dummy_arcadia/hello_world"
    },
    "destination": {
        "path": "/"
    }
}
```

Если секция `build` имеет несколько ключей, то обязательно надо указывать `build_key`:

```json
{
    "source": {
        "type": "BUILD_OUTPUT",
        "build_key": "build_1",
        "path": "devtools/dummy_arcadia/hello_world"
    },
    "destination": {
        "path": "/"
    }
}
```

Поддерживается ключ `files`, который работает по fnmatch:
```json
{
    "source": {
        "type": "BUILD_OUTPUT",
        "path": "devtools/dummy_arcadia/hello_java/hello",
        "files": [
            "*.jar"
        ]
    },
    "destination": {
        "path": "/java/jar/"
    }
}
```

### SANDBOX_RESOURCE { #data-sandbox-resource }
```json
{
    "source": {
        "type": "SANDBOX_RESOURCE",
        "id": 79663241,
        "path": "apache-maven-3.3.3/bin",
        "untar": true,
        "files" : [<files>],
        "symlinks": false
    },
    "destination": {
        "path": "/apache/bin",
        "attributes": {
            "mode": {
                "value": "+x"
             }
        }
    }
}
```
* В качестве `path` можно передать путь имя файла ресурса, внутри ресурса (если это ресурс-папка), или путь внутри ресурса-архива (если ресурс состоит из 1 tar-архива). Также можно опустить `path`, если ресурс - затаренный список файлов, и перечислить необходимые файлы в `files` (или [glob-паттерн](https://en.wikipedia.org/wiki/Glob_(programming))).
* Если в ресурсе находится исполняемый файл, то нужно проставить правильно атрибуты (как в примере);
* `untar` - распаковать архив;
* `files` - список файлов ресурса для пакетирования;
* `symlinks` - Не удалять симлинки из ресурсов.

### ARCADIA { #data-arcadia }
Чтобы забрать данные, лежащие в репозитории, нужно использовать тип `ARCADIA`:
```json
{
    "source": {
        "type": "ARCADIA",
        "path": "devtools/dummy_arcadia/hello_world",
        "files": [
            "ya.make",  # взять один конкретный файл
            "*.cpp",  # взять все .cpp файлы из дирректории и всех поддирректорий; для раскрытия шаблона используется fnmatch
            "glob:*.txt",  # взять все .txt файлы только из указанной дирректории (без поддирректорий)
            "glob:*/*.txt"  # взять все .txt файлы только из поддирректорий первого уровня
        ]
    },
    "destination": {
        "path": "/da/",  # слеш на конце обязательный
        "attributes": {
            "mode": {
                "value": "-w",
                "recursive": true
            }
        }
    }
}
```
Важно понимать, что данный тип именно для данных, которые закомичены в репозиторий, а не данные, которые в него линкуются локально после сборки.
### DIRECTORY { #data-directory }
создать пустую директорию в пакете
```json
{
    "source": {
        "type": "DIRECTORY"
    },
    "destination": {
        "path": "/empty_dir"
    }
}
```
### RELATIVE { #data-relative }
```json
{
    "source": {
        "type": "RELATIVE",
        "path": "some_text.txt",
        "symlinks": false 
    },
    "destination": {
        "path": "/data_dir/"
    }
}
```

Здесь поле `symlinks` обладает аналогичным поведением, что и в [SANDBOX_RESOURCE](#data-sandbox-resource).

### SYMLINK { #data-symlink }
Символическая ссылка `path` будет указывать на `target`.
```json
{
    "source": {
        "type": "SYMLINK"
    },
    "destination": {
        "path": "/usr/lib/libmylib.so",
        "target": "/usr/lib/libmylib.so.0"
    }
}
```
### INLINE { #data-build-inline }
Текстовый файл с содержимым, работают подстановки аналогичные как для поля `version`.
```json
{
    "source": {
        "type": "INLINE",
        "content": "arbitrary content, substitutions supported"
    },
    "destination": {
        "path": "/some_package_dir/info.txt"
    }
}
```

### TEMP { #data-temp }
Путь внутри временного хранилища (куда заранее были привезены файлы с пометкой ```"temp": true```.
```json
{
      "source": {
        "type": "TEMP",
        "path": "tmp/temp_path"
      },
      "destination": {
        "archive": "/dir/temp_archive.tgz"
      }
}
```

### Атрибуты владения и прав доступа к файлам { #data-file-attributes }
В секции `destination` можно задать объект `attributes`. В качестве ключа используется тип атрибута (поддерживается `mode`), в качестве значения — объект с полем `value` (значение атрибута) и опциональным булевским полем `recursive` (включает рекурсивное распространение атрибута по дереву файловой системы, по умолчанию `false`).

Пример:
```json
{
    "source": {
        "type": "ARCADIA",
        "path": "devtools/dummy_arcadia/hello_world"
    },
    "destination": {
        "path": "/sources-read-only-root",
        "attributes": {
            "mode": {
                "value": "-w"
            }
        }
    }
}
```

При сборке в deb-пакет атрибут для смены `owner` или `group` напишите `postinst`-скрипт, в котором запустите программу `chown` вручную с нужными аргументами.

## params { #params }
Секция используется для указания параметров пакетирования, которые неразрывно связаны с описываемым пакетом.

Все опции указанные в этой секции могут быть переопределены через соответствующие cli параметры при запуске `ya package`.

Название параметра | Тип | Cli аналог | Описание | Допустимые значения
:--- | :-- | :--- | :--- | :---
format | string | `--tar`/`--debian`/`--rpm`/etc | Позволяет указать конкретный формат сборки пакета | "debian"/"tar"/"docker"/"rpm"/"wheel"/"aar"/"npm"
arch_all | bool | `--arch-all` | Указывает "Architecture: all" при сборке debian. | true/false
artifactory | bool | `--artifactory` | Собирает пакет, загружая в artifactory при указании `--publish-to <settings.xml>`  | true/false
compress_archive | bool | `--no-compression` | Позволяет отключить сжатие tar архивов | true/false
compression_filter | string | `--compression-filter=X` | Задаёт кодек для tar архива | "gzip"/"zstd"
compression_level | int | `--compression-level=X` | Задаёт степень сжатия для кодека | 0-9 для gzip, 0-22 для zstd
create_dbg | bool | `--create-dbg` | Создаёт отдельный пакет с отладочной информацией. Учитывается только в случае указания `--strip`/`--full-strip` | true/false
custom_version | string | `--custom-version=X` | Позволяет задать шаблон для версии пакета |
debian_arch | string | `--debian-arch=X` | Позволяет задать тип архитектуры для debuild | "amd64"
debian_compression_level | string | `-z`/`--debian-compression` | Задаёт степень сжатия для deb-file | none, "low", "medium", "high"
debian_compression_type | string | `-Z`/`--debian-compression-type` | Указать тип сжатия для deb-file | none, "gzip" (по умол.), "xz", "bzip2", "lzma"
docker_add_host | list | `--docker-add-host` | То же самое, что и Docker --add-host |
docker_build_arg | dict<str, str> | `--docker-build-arg=X=Y` | Передаётся в виде ключа `--build-arg X=Y` в docker build. Cli опция `--docker-build-arg` расширяет множество параметров указанных в package.json в секции `params.docker_build_arg`, а не заменяет их. При совпадении ключей, cli значение переопределяет значение из секции `docker_build_arg`|
docker_build_network | string | `--docker-network=X` | Передаётся в виде ключа `--network=X` в docker build | "host"/etc
docker_platform | string | `--docker-platform=X` | Позволяет указать платформу для docker сборки (требует buildx плагина для docker) |
docker_registry | string | `--docker-registry=X` | Задаёт docker registry |
docker_repository | string | `--docker-repository=X` | Задаёт docker репозиторий |
docker_save_image | bool | `--docker-save-image` | Сохраняет docker image в архив | true/false
docker_secrets | list | `--docker-secret` | Позволяет передать секреты в docker аналогично `docker --secret` | см. [документацию](https://docs.docker.com/build/building/secrets/#sources)
docker_target | string | `--docker-target` | Указание целевого этапа сборки (--target) |
dupload_max_attempts | int | `--dupload-max-attempts=X` | Количество попыток dupload | >0
dupload_no_mail | bool | `--dupload-no-mail` | Добавляет опцию `--no-mail` для dupload | true/false
ensure_package_published | bool | `--ensure-package-published` | Верификация доступности пакета после публикации | true/false
force_dupload | bool | `--force-dupload` | Добавляет опцию `--force` для dupload | true/false
full_strip | bool | `--full-strip` | Выполняет `strip` для бинарей | true/false
overwrite_read_only_files | bool | `--overwrite-read-only-files` | Разрешить перезаписывать read-only файлы при сборке пакета (error prone) | true/false
raw_package | bool | `--raw-package` | Отключает упаковку пакета в tar | true/false
resource_attrs | dict<str, str> | `--upload-resource-attr` | Добавляет указанные атрибуты к ресурсу sandbox |
resource_type | string | `--upload-resource-type=X` | Тип создаваемого ресурса | Например "YA_PACKAGE"
sign | bool | `--not-sign-debian` | Отключает подпись deb пакета | true/false
sloppy_deb | bool | `--sloppy-and-fast-debian` | Fewer checks and no compression when building debian package (error prone) | true/false
store_debian | bool | `--dont-store-debian` | Отключает сохранение deb файла в отдельном архиве | true/false
strip | bool | `--strip` | Выполняет `strip -g` для бинарей (удаляет только debugging symbols) | true/false
wheel_platform | string | `--wheel-platform` | Указать платформу для сборки wheel | 
wheel_python3 | bool | `--wheel-python3` | Использовать python3 для сборки пакета | true/false

Пример ниже позволяет избавить запуск `ya package <package.json>` от постоянного указания ключей `--wheel-python3 --docker-repository=yasdc`:
```json
{
    "params": {
        "docker_repository": "yasdc",
        "wheel_python3": true
    }
}
```

## userdata { #userdata }
Секция для произвольных пользовательских данных. Не используется и не проверяется ```ya package```.
## postprocess { #postprocess }
Секция, позволяющая описать действия, которые необходимо выполнить после основной работы ```ya package``` над директорией с результатами. Состоит из списка команд (`source`) и аргументов к ним (`arguments`). Так же можно переопределить текущую рабочую директорию, если она отличается от директории с результатами (`cwd`), и переменные окружения (`env`) в формате ключ — значение.

Источники команд могут быть следующими:
### BUILD_OUTPUT { #postprocess-build-output }
Бинарный файл, собранный из репозитория (должен быть указан в секции `build`). Например:
```json
{
    "source": {
        "type": "BUILD_OUTPUT",
        "build_key": "build_tools",
        "path": "maps/mobile/tools/ios-packager/ios-packager"
    },
    "arguments": [
        "--library", "./bundle.a",
        "--libraries-temp", "./libs",
        "--headers-temp", "./headers",
        "--module-name", "YandexMapsMobile",
        "--framework-name", "YandexMapsMobile",
        "--framework-version", "0",
        "--header-prefixes", "YRT", "YMA", "YDS",
        "--modulemap-exclude-prefixes", "YMA"
    ]
}
```
### SANDBOX_RESOURCE { #postprocess-sandbox-resource }
Ресурс из Сандбокса (должен быть запускаемым файлом).
```json
 {
    "source": {
        "type": "SANDBOX_RESOURCE",
        "id": "1130399306",
        "path": "bin/lipo",
        "untar": true
    },
    "arguments": [
        "-create",
        "./libs/bundle.x86_64.a",
        "./libs/bundle.arm64.a",
        "./libs/bundle.i386.a",
        "./libs/bundle.armv7.a",
        "-output",
        "bundle.a"
    ]
}
```
### ARCADIA { #postprocess-arcadia }
Файл из репозитория (должен быть запускаемым файлом)
```json
{
    "source": {
        "type": "ARCADIA",
        "path": "market/mbi/push-api/push-api/src/main/script/copy_file.sh"
    },
    "arguments": [
        "../resources/application.properties",
        "/{package_name}/properties.d/00-application.properties"
    ]
}
```

## Примеры
Больше примеров можно найти в [devtools/dummy_arcadia/package](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/dummy_arcadia/package).

А также [поиском по Аркадии](https://a.yandex-team.ru/search?search=%2522data%2522%253A%2520%255C%255B%2Cpackage.json%2Cj%2Carcadia%2Cfrontend%2C200&repo=arc). Однако, стоит помнить, что простой поиск по имени файла `package.json` не показателен, так как во фронтенд-разработке это имя файла [зарезервировано](https://docs.npmjs.com/cli/v9/configuring-npm/package-json) для манифеста npm-пакета.

