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

**Пример**
```
{
    "name": "project-some-package-name",
    "maintainer": "programmers <user@mail>",
    "description": "Some package description",
    "version": "Version with templates from {revision}, {branch}, {changelog_version}",
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

### include
Включает перечень путей к пакетам (относительно корня репозитория), содержимое которых нужно добавить: включаются все секции, причем поля из секции `meta` заменяются согласно порядку следования записей в секции `include` (последней заменой является `meta` текущего пакета).

**Пример**
```
"include": [
        "devtools/package/tests/create_tarball/package_base.json"
    ]
```

Существует возможность включить содержимое подключаемого пакета в текущий пакет с добавлением префикса в путях:

**Пример**
```
"include": [
        {
            "package": "devtools/package/tests/create_tarball/package_with_include.json",
            "targets_root": "include_root"
        }
    ]
```

### build
В этом разделе указываются директивы для сборки содержимого пакета с помощью команды `ya make`. Единственный обязательный ключ — `targets`:

**Пример**
```
{
    "targets": [
        "devtools/project/hello_world",
        "devtools/project/hello_java/hello"
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
* `targets` - цели для сборки (параметр `-C` для `ya make`).
* `build_type` - профиль сборки.
* `sanitize` - сборка с санитайзером.
* `sanitizer_flags` - флаги для санитайзера.
* `lto` - сборка с LTO.
* `thinlto` - сборка с ThinLTO.
* `musl` - сборка с MUSL.
* `race` - сборка go c race санитайзером.
* `add-result` - директива `--add-result` для запуска `ya make`.
* `flags` - флаги для сборки, будут передаваться параметром `--target-platform-flag=<name>=<value>` в `ya make`.
* `target-platforms` – таргет-платформы, под которые надо производить сборку.

Поле `flags` поддерживает подстановки из секции `meta`, а также дополнительные:
* `package_name` – название пакета.
* `package_version` – текущая версия пакета.
* `package_full_name` – название пакета с версией.
* `package_root` – директория, где находится json-описание пакета относительно корня репозитория.

Этот раздел может содержать несколько секций (несколько запусков `ya make`), причем каждая секция должна иметь уникальный ключ:

**Пример**
```
"build": {
         "build_1": {
            "targets": [
                "devtools/project/hello_world"
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
                "devtools/project/hello_world"
            ]
        }
    }
```
Далее ключи (`build_1` или `build_2`) необходимо использовать в разделе `data` в элементах `BUILD_OUTPUT`.

Цели внутри одной секции собираются совместно, в то время как разные секции собираются последовательно. Последовательная сборка секций обусловлена тем, что разные секции могут собирать одинаковые цели с разными флагами, что может вызвать проблемы из-за совпадения имён. Каждая секция будет настраиваться отдельно для сборки. Поэтому рекомендуется собирать все цели в одной конфигурации в пределах одной секции: это сократит время на конфигурацию, может увеличить параллелизм в процессе сборки и ускорить её.

#### Strip
Чтобы получить бинарные файлы без стриппинга (без удаления несущих информации элементов, которые не являются необходимыми для нормального и правильного выполнения), нужно добавить:

**Пример**
```
"flags": [
            {
                "name": "NO_STRIP",
                "value": "yes"
            }
        ]
```

#### Custom version 
С помощью параметра `--custom-version=<str>` можно задать шаблон версии пакета. Он имеет более высокий приоритет, чем версия, указанная в файле описания. Пользовательская версия может быть встроена в собираемые программы в `ya package`. Чтобы получить строку с пользовательской версией, используйте функцию `GetCustomVersion()` из `library/cpp/svnversion/svnversion.h` для `C++`, и поле `Info.CustomVersion` для `Go`

Любая информация о версиях, встроенная в программы, по умолчанию не обновляется при сборке. Такое поведение желательно для ускорения процесса сборки и тестирования пакетов. Чтобы принудительно обновить информацию о версии, используйте флаг `-DFORCE_VCS_INFO_UPDATE=yes` (параметр `force_vcs_info_update` для задачи `YA_PACKAGE_2`).” 

Также пользовательскую ('custom') версию можно передать в сборку (не рекомендуется):

**Пример**
```
"flags": [
            {
                "name": "PACKAGE_VERSION",
                "value": "{package_version}"
            }
        ]
```

### data 
В этом разделе содержится список элементов, каждый из которых имеет поля `source` и `destination`. В `source` обязательно должен быть указан тип (`type`) элемента, который может быть одним из следующих: `BUILD_OUTPUT`, `RELATIVE`, `DIRECTORY` или `TEST_DATA`. Остальные поля в `source` зависят от указанного типа элемента. В `destination` достаточно указать только `path`, но также можно явно установить права и степень их распространения через `attributes`. Если на этапе подготовки пакета файлы, указанные в `destination`, уже существуют, перезапись будет пропущена при эквивалентности файлов. В случае расхождения содержимого файлов возникнет ошибка сборки пакета (`sanity check`). Если вы намеренно перезаписываете файлы в пакете, следует явно указывать ключ `--overwrite-read-only-files`.

Поле `path` поддерживает те же подстановки, которые можно использовать в секции `meta`, а также дополнительные:
* `package_name` – название пакета.
* `package_version` – текущая версия пакета.
* `package_full_name` – название пакета с версией.
* `package_root` – директория, где находится json-описание пакета относительно корня репозитория.

Блок `destination` может принимать дополнительные параметры:
* `"temp": true` – пометка, позволяющая поместить результат работы блока во временное хранилище, доступное далее через [TEMP](#temp)
* `"archive": "/path/to/archive_name.tgz"` – используется вместо `path`, чтобы запаковать в архив результат работы блока `source`

**Пример**
```
{
  "meta": {
    "name": "archive example"
  },
  "data": [
    {
      "source": {
        "type": "BUILD_OUTPUT",
        "path": "contrib/python/hg/mercurial/help"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "BUILD_OUTPUT",
        "path": "contrib/python/hg/mercurial/templates"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "BUILD_OUTPUT",
        "path": "vcs/hg/server/config/server.rc"
      },
      "destination": {
        "path": "/tmp/hg_server/hg/default.d/",
        "temp": true
      }
    },
    {
      "source": {
        "type": "BUILD_OUTPUT",
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

#### BUILD_OUTPUT
```
{
    "source": {
        "type": "BUILD_OUTPUT",
        "path": "devtools/project/hello_world"
    },
    "destination": {
        "path": "/"
    }
}
```

Если секция `build` имеет несколько ключей, то обязательно надо указывать `build_key`:

```
{
    "source": {
        "type": "BUILD_OUTPUT",
        "build_key": "build_1",
        "path": "devtools/project/hello_world"
    },
    "destination": {
        "path": "/"
    }
}
```

Поддерживается ключ `files`, который работает по `fnmatch`:
```
{
    "source": {
        "type": "BUILD_OUTPUT",
        "path": "devtools/project/hello_java/hello",
        "files": [
            "*.jar"
        ]
    },
    "destination": {
        "path": "/java/jar/"
    }
}
```
#### DIRECTORY 
Применяется для создания пустой директории в пакете
```
{
    "source": {
        "type": "DIRECTORY"
    },
    "destination": {
        "path": "/empty_dir"
    }
}
```
#### RELATIVE 
Используется для указания относительных путей к файлам
```
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
Поле `symlinks` указывает, что символические ссылки из ресурсов не должны удаляться.

#### SYMLINK
Символическая ссылка по пути `path` будет указывать на `target`.
```
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
#### INLINE
Текстовый файл с содержимым, где работают подстановки аналогичные тем, что используются для поля `version`.
```
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
#### TEMP
Путь внутри временного хранилища, куда предварительно были помещены файлы с пометкой `"temp": true.`
```
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

#### Атрибуты владения и прав доступа к файлам 

В секции `destination` можно указать объект `attributes`. В качестве ключа используется тип атрибута (поддерживается `mode`), в качестве значения — объект с полем `value` (значение атрибута) и опциональным булевским полем `recursive` (включает рекурсивное распространение атрибута по дереву файловой системы, по умолчанию `false`).
```
{
    "source": {
        "type": "BUILD_ROOT",
        "path": "devtools/project/hello_world"
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
При сборке в `deb`-пакет, для изменения атрибута `owner` или `group`, напишите `postinst`-скрипт, который вручную запустит команду `chown` с необходимыми аргументами

### params
Этот раздел используется для указания параметров пакетирования, которые непосредственно связаны с описываемым пакетом.

Все опции, указанные в этом разделе, могут быть переопределены через соответствующие параметры командной строки `cli` при запуске команды `ya package`.
Название параметра | Тип | Cli аналог | Описание | Допустимые значения
:--- | :-- | :--- | :--- | :--- 
format | string | `--tar`/`--debian`/`--rpm`/etc | Позволяет указать конкретный формат сборки пакета | "debian"\"tar"\"docker"\"rpm"\"wheel"\"aar"\"npm" 
arch_all | bool | `--arch-all` | Указывает "Architecture: all" при сборке debian. | true/false 
artifactory | bool | `--artifactory` | Собирает пакет, загружая в artifactory при указании `--publish-to <settings.xml>`  | true/false |
compress_archive | bool | `--no-compression` | Позволяет отключить сжатие tar архивов | true/false |
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
resource_attrs | dict<str, str> | `--upload-resource-attr` | Добавляет указанные атрибуты к ресурсу |
resource_type | string | `--upload-resource-type=X` | Тип создаваемого ресурса | 
sign | bool | `--not-sign-debian` | Отключает подпись deb пакета | true/false
sloppy_deb | bool | `--sloppy-and-fast-debian` | Fewer checks and no compression when building debian package (error prone) | true/false
store_debian | bool | `--dont-store-debian` | Отключает сохранение deb файла в отдельном архиве | true/false
strip | bool | `--strip` | Выполняет `strip -g` для бинарей (удаляет только debugging symbols) | true/false
wheel_platform | string | `--wheel-platform` | Указать платформу для сборки wheel | 
wheel_python3 | bool | `--wheel-python3` | Использовать python3 для сборки пакета | true/false

Пример ниже позволяет избежать постоянного указания ключей `--wheel-python3` `--docker-repository=yasdc` при запуске `ya package <package.json>`:

```
{
    "params": {
        "docker_repository": "yasdc",
        "wheel_python3": true
    }
}
```

### userdata
Секция для произвольных пользовательских данных. Не используется и не проверяется `ya package`.
### postprocess
Этот раздел позволяет описать действия, которые необходимо выполнить после основной работы `ya package` над директорией с результатами. Он состоит из списка команд (`source`), а также аргументов к ним (`arguments`). Также можно переопределить текущую рабочую директорию, если она отличается от директории с результатами (`cwd`), и задать переменные окружения (`env`) в формате ключ — значение.

Источники команд могут быть следующими:
#### BUILD_OUTPUT
Бинарный файл, собранный из репозитория (должен быть указан в секции `build`). Например:
```
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

#### BUILD_ROOT 
Файл из репозитория (должен быть запускаемым файлом)
```
{
    "source": {
        "type": "BUILD_ROOT",
        "path": "market/mbi/push-api/push-api/src/main/script/copy_file.sh"
    },
    "arguments": [
        "../resources/application.properties",
        "/{package_name}/properties.d/00-application.properties"
    ]
}
```
