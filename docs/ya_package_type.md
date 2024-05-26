# Форматы пакетирования

ya package поддерживает пакетирование в различных форматах. Про особенности работы с каждым из них можно прочитать на отдельной странице:

* [Упакованный tar-архив (.tar.gz)](tar.md) (по умолчанию)
* [Debian-пакет (.deb)](deb.md)
* [rpm-пакет (.rpm)](rpm.md)
* [docker-образ](docker.md)
* [Python wheel-пакет (.whl)](wheel.md)
* [Нативный мультиплатформенный пакет для Android (.aar)](aar.md)
* [npm-пакет для Node.js](npm.md)

# Сборка tar-пакетов
Сборка tar-архивов происходит, если не передавать формат пакетирования явно, но можно и указать ключ ```--tar```.

```bash
ya package <pkg.json>
```
или
```bash
ya package <pkg.json> --tar
```

## Ключи для сборки пакетов { #keys }
* `--compression-filter=X` задать фильтр для tar архива, по умолчанию - gzip. Может быть gzip/zstd
* `--compression-level=X` задать уровень сжатия для фильтра. Для gzip может быть 0-9 (по умолчанию: 6), для zstd может быть 0-22 (по умолчанию: 3)
* `--no-compression` создавать архив без сжатия
* `--raw-package` используется вместе с `--tar`, сохраняет пакет без архивирования
* `--raw-package-path=PATH` позволяет переопределить директорию для сохранения пакета без архивирования

{% note tip %}

Для ускорения сборки пакетов, можете попробоавть использовать `--compression-filter=zstd --compression-level=1`. В этом случае будет генерироваться архив в расширением `.tar.zst`

{% endnote %}


# Сборка deb-пакетов

Сборка debian-пакета производится с помощью команда
```bash
ya package --debian <package.json>
```

## Ключи для сборки deb-пакетов { #keys }
* `--not-sign-debian` не подписывать пакет: такой пакет не удасться загрузить в репозиторий, но для отладки в локальных запусках подходит
* `--debian-distribution` указание `Debian distribution` при сборке пакета (по умолчанию: `unstable`)
* `--debian-arch` указание архитектуры (передается команде ```debuild``` через ключ ```-a```)
* `--arch-all` синоним `Architecture: all`
* `--force-dupload` форсированная загрузка (```dupload --force```)
* `--debian-compression, -z` степень сжатия deb-файла (`none`, `low`, `medium`, `high`)
* `--debian-compression-type, -Z` тип сжатия deb-файла (`gzip`, `xz`, `bzip2`, `lzma`, `none`)
* `--publish-to` публикация пакета в репозитории (под капотом происходит вызов команды `dupload`)
* `--dupload-max-attempts` количество попыток публикации пакета при вызове команды ```dupload``` (по умолчанию 1)
* `--dupload-no-mail` указание вызывать команду ```dupload``` с ключом ```--no-mail```
* `--ensure-package-published` дожидаться публикации пакета в репозитории

## Работа с конфигурационными файлами { #conffiles }
В ```ya package``` по историческим причинам по умолчанию включен хелпер `dh_noconffiles`, при котором никакие файлы не считаются файлами конфигурации (в т.ч. из папки `/etc`), для отмены данного поведения надо использовать ```"noconffiles_all": false``` в секции [meta](json.md#meta)`([пример](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/fat/data/package_conffiles.json?rev=4258754#L7))

## Вызов дополнительных хелперов { #debhelpers }
Если надо вызывать дополнильные хелперы, можно воспользоваться файлом `debian/extra_rules` ([примеры в репозитории](https://cs.yandex-team.ru/#!,extra_rules,,arcadia)).

## postinst, prerm etc { #debian }
В случае, если требуются `postinst` и иные стандартные debian-файлы, нужно создать каталог `debian` рядом с json-описанием пакета, в котором поместить нужные файлы, например, `postinst`.
*ВАЖНО* в файлах postinst, prerm и других должна быть строка `#DEBHELPER#`

## Сборка deb-пакетов для Python { #deb-python }
При наличии в пакете путей, в которые устанавливаются пакеты для python, например, `/usr/lib/python2.7/dist-packages`, срабатывает debian-хелпер `dh_pysupport`, включенный по умолчанию, который все файлы в таких путях перекладывает в «правильные» по его мнению места, а именно в `usr/share/pyshared`. В связи с этим:
1. Не будет работать смена владельцев и изменения прав на файлы, так как данное действие происходит после вызова этого хелпера и исходных путей нет.
2. Нужно не забыть добавить в файл-описание пакета в секцию meta, в список `pre-depends` добавить зависимость от пакета `python-support` иначе на чистой машине пакет не подключится к Python несмотря на наличие файлов.

Для того, что отключить `dh_pysupport`, нужно рядом с файлом описания пакета положить файл `debian/extra_rules` с таким содержимым:

```
override_dh_pysupport:
    # Nothing to do
```

Если сборка пакета явно закладывается на `dh_pysupport`, то необходимо добавить в секцию `build-depends` пакет `python-support`:
```json
 "meta": {
        ...
        "build-depends": ["python-support"]
    },
```

## Troubleshooting

- `debian/rules:X: *** missing separator.  Stop.
dpkg-buildpackage: error: fakeroot debian/rules clean subprocess returned exit status 2`
Одна из частых причин вызывающих ошибку, это использование пробелов вместо табуляции (`\t`) для отступов в описании правил `debian/extra_rules`. 

## Воспроизводимость сборки и совместимость между дистрибутивами { #ubuntu-version }
```ya package``` использует системные инструменты для сборки deb-пакетов, поэтому результат зависит от используемой ОС и может быть не совместим с другой.
* алгоритм компрессии (xz) выбираемый `dpkg-builddeb` по умолчанию в новых версиях Ubuntu не совместим c precise
* на дистрибутивах до xenial не получится правильно собрать пакет с поддержкой systemd

## Особенности запуска через задачу YA_PACKAGE_2 { #sandbox }
Для публикации пакетов в dist.yandex-team.ru из задачи Sandbox следует выбрать робота в поле задачи `key_user`, чтобы подписывать и загружать пакеты от его имени.

Если необходимо создать нового робота для подписи, нужно сделать следующее:

1. [Заводим робота](https://wiki.yandex-team.ru/tools/support/zombik) с почтовым ящиком и запрашиваем права на заливку пакетов через в [Управляторе](https://idm.yandex-team.ru) в системе `Cauth->conductor.dist-duploaders->SSH`
    По умолчанию у робота  командный процессор выставлен в ```/bin/false```, поэтому нужно зайти в аккаунт робота на Стаффе, кликнуть на аватарку в шапке, перейти в «Настройки», и поменять «Командный процессор», например, на ```/bin/bash```, чтобы робота не вкидывало с серверов при авторизации по ssh.

2. Получаем oauth токен, который будем обменивать на ssh сертификат, для робота по [этой инструкции](https://docs.yandex-team.ru/skotty/howto-robots#podgotovka).

3. Полученный токен кладём в Яндекс Секретницу [yav](https://yav.yandex-team.ru/). [Группа в Sandbox](https://sandbox.yandex-team.ru/admin/groups), от которой выполняются задачи (допустим, она называется `IEX-CI`), должна иметь доступ к секрету. Место хранения токена необходимо указать в параметре `skotty_oauth_token`. Подробнее про использование секретов из Яндекс Секретницы в sandbox можно почитать [тут](https://docs.yandex-team.ru/sandbox/dev/secret). 

    Чтобы передать секрет из yav в CI используется следующий синтаксис:

    ```
    skotty_oauth_token: sec-<your_secret_id>#<your_key_name>
    ```
    
    где your_secret_id - идентификатор секрета в yav, your_key_name - имя ключа.

4. Создаем GPG-ключи
    1. Генерируем ключи:
    ```bash
    gpg --gen-key
    ```
    Отвечаем, что ключ нужен для Робота Бендера, указываем его почтовый адрес `robot-bender@yandex-team.ru`. *Важно*, чтобы в качестве почтового адреса был указан адрес робота.

    Пароль у ключа оставляем пустым, иначе не будет работать.

    2. Получаем права на upload в репозиторий. Экспортируем ключ:
    ```bash
    gpg --export --armor robot-bender >robot-bender.gpg
    ```
    (Обратите внимание на *наличие* опции ```--armor```)
    
    a) Добавляем этот публичный GPG ключ на Стафф в разделе "GPG-ключи". В первое поле (Ключ) идёт содержимое файла `robot-bender.gpg`.

    3. Экспортируем ключи для зашифрованного хранилища Sandbox Vault:
    ```bash
    gpg --export robot-bender | base64
    ```
    (Обратите внимание на *отсутствие* опции ```--armor```)
    ```bash
    gpg --export-secret-keys robot-bender | base64
    ```
    (у ключа не должно быть пароля, а то ключ нельзя будет использовать внутри sandbox)

    Получившееся [кладём в yav](https://yav.yandex-team.ru/). В параметрах задачи также необходимо заполнить `gpg_public_yav_key` и `gpg_private_yav_key` - место хранения ключей в Яндекс Секретнице соответственно.

    Чтобы передать секрет из yav в CI используется следующий синтаксис:

    ```
    gpg_public_yav_key: sec-<your_secret_id>#<your_public_key_name>
    gpg_private_yav_key: sec-<your_secret_id>#<your_private_key_name>
    ```
    
    где your_secret_id - идентификатор секрета в yav, your_public_key_name/your_private_key_name - имя ключа.

    [Подробнее про GPG-ключи написано в документации к dist](https://doc.yandex-team.ru/Debian/deb-pckg-guide/concepts/GPG.xml)

5. Добавить робота и его группу в [список роботов задачи](https://a.yandex-team.ru/arc/trunk/arcadia/sandbox/projects/common/build/ya_package_config/consts.py?rev=r7844539#L503) (после коммита надо подождать около 30 минут, пока код задач обновится на сервере).

6. Итого в задаче YA_PACKAGE_2 у вас должно добавиться 4 параметра:

- key_user
- skotty_oauth_token
- gpg_public_yav_key
- gpg_private_yav_key

А в yav секрете 3 ключа:

- skotty_oauth_token
- gpg_public_yav_key
- gpg_private_yav_key


# Сборка rpm-пакетов
Чтобы собрать rpm-пакет, нужно выполнить команду
```bash
ya package --rpm <pkg.json>
```

При сборке пакета, поля из секции [meta](json.md#meta) описания пакета формируется *.spec файл.
Если нужны дополнительные секции `post` и т.д. в spec-файле, необходиом создать каталог `rpm`, в который поместить файлы с именем равным названию секции и нужным содержимым (поддерживаются `pre`, `post`, `preun`, `postun`, `check`, `changelog`).

Для локальной сборки rpm-пакетов на машине, где эти пакеты собираются, должна быть установлена утилита `rpmbuild`.

Если rpm не является основным пакетным менеджером в вашей системе, то мы рекомендуем не собирать rpm пакеты локально, а воспользоваться sandbox задачей `YA_PACKAGE`.



# Сборка docker-образов

С помощью команды
```bash
ya package <package.json> --docker
```
можно собрать докер-образ – для этого в json-описание пакета нужно добавить `Dockerfile`, который будет использоваться для последующих вызовов ```docker build``` и ```docker-push```, сам файл не обязательно должен лежать рядом с json-описанием, но обязательно должен иметь `/Dockerfile` в `destination`, [например](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/create_docker/package/package.json):

```json
{
    "source": {
        "type": "RELATIVE",
        "path": "Dockerfile"
    },
    "destination": {
        "path": "/Dockerfile"
    }
}
```
Перед вызовом команды ```docker build``` в ее рабочей директории производится сборка пакета и подготовка его содержимого, таким образом при формировании команд в `DockerFile` (`COPY` и подобных) можно рассчитывать на наличие файлов и структуру каталогов, описанных в json-описании.

*Внимание*
* По умолчанию пакет будет иметь тег равный ```registry.yandex.net/<repository>/<package name>:<package version>```
  * `registry.yandex.net` – реестр образов, задается через параметр ```--docker-registry```, который по умолчанию задан как `registry.yandex.net`;
  * `<repository>` – название репозитория для пакета, задается через ```--docker-repository``` и по умолчанию не имеет значения.
* Для успешной работы ```ya package --docker``` необходимо наличие установленного на хост-системе пакета с `docker`, а также права на его запуск;
* ```ya package --docker``` не выполняет действий по авторизации в реестре (команду ```docker login```) – предполагается, что пользователь заранее об этом позаботился.
* Если не задан параметр ```--docker-save-image```, то в архиве пакета будет только результат работы команд ```docker```.
* Бинари, собираемые в процессе работы ```ya package --docker```, по дефолту, будут собраны под вашу хост-платформу, а не под платформу, указанную в докере. Для изменения платформы, нужно явно указать платформу в поле ```target-platforms``` секции [build](json.md#build)

## Ключи для сборки docker-образов { #keys }
* `--docker-platform` указание платформы, под которой будет работать образ. Опция полезна, если платформа, на которой собирается пакет отличается от платформы, на которой будет работать образ. Может быть указана в секции [params](https://docs.yandex-team.ru/ya-make/usage/ya_package/json#params)
* `--docker-registry` указание реестра для публикации (по умолчанию `registry.yandex.net`)
* `--docker-repository` указание репозитория для образа (участвует в имени образа)
* `--docker-save-image` сохранение образа в виде отдельного файла в архиве
* `--docker-push` публикация образа в реестр
* `--docker-no-cache` указание ключа `--no-cache` для вызываемой под капотом команды ```docker build```
* `--docker-use-remote-cache` указание ключа `--cache-from` для вызываемой под капотом команды ```docker build```
* `--docker-network` указание ключа `--network` для вызываемой под капотом команды ```docker build```
* `--docker-build-arg` указание ключа `--build-arg` для вызываемой под капотом команды ```docker build```; формат значения должен иметь вид `<key>=<value>`
* `--docker-secret` указание ключа `--secret` для вызываемой под капотом команды ```docker build```; формат значения должен иметь вид `id=ENV_VAR` (см. [документацию](https://docs.docker.com/build/building/secrets/#sources))
* `--custom-version` - указание спецефичного тега образа (по умолчанию тег берется из имени текущей ветки в arc)

{% note info %}

При передаче аргументов в контейнер не забудьте прописать в Dockerfile:

```
ARG MY_ARG_NAME
```
[Пример](https://a.yandex-team.ru/arcadia/devtools/dummy_arcadia/package/hello_world_docker) Dockerfile с передачей аргументов.

{% endnote %}

## Публикация образов  { #publish }
При добавлении `--docker-push` происходит публикация образа в реестре. Для успешной публикации у пользователя должны быть права на этот пакет в `IDM`. Подробнее про особенности работы с `registry.yandex.net` можно почитать [тут](https://wiki.yandex-team.ru/qloud/docker-registry/).

## Использование удаленного кэша { #remote-cache }
При добавлении `--docker-use-remote-cache` в качестве источника кэша используется образ из реестра, путь до которого формируется на основе данных `package.json`. Дополнительно можно указать `--docker-remote-image-version` и тогда в качестве источника будет использоваться образ указанной версии.

{% note info %}

В качестве кэша могут использоваться только те образы, что были собраны с флагом `BUILDKIT_INLINE_CACHE=1`. Собрать образ с этим флагом можно, например, так:
```
ya package package.json --docker --docker-build-arg BUILDKIT_INLINE_CACHE=1
```
В некоторых случаях docker может автоматически не включить BuildKit, тогда это надо сделать отдельно с помощью переменной окружения `DOCKER_BUILDKIT=1`.

{% endnote %}

## Сохранение образов { #save }
Если нужно сохранить полученный образ в файл (например, для последующей его загрузки с помощью ```docker load```), то необходимо запускать:
```bash
ya package --docker --docker-save-image
```
файл образа будет сохранен в архиве пакета.

## Особенности запуска через задачу YA_PACKAGE_2 { #sandbox }
docker-образ можно собрать с помощью задачи `YA_PACKAGE_2`, для чего предварительно в [Vault](https://sandbox.yandex-team.ru/admin/vault) необходимо положить OAuth-токен для авторизации в реестре, а в задаче заполнить поля, относящиеся к сборке docker-образов, выбрав `Package type` = `docker`:
* Если в таске не задан lxc container, по умолчанию будет использоваться наш [4467981730](https://sandbox.yandex-team.ru/resource/4467981730/view) (ubuntu 18.04-bionic, python 3.6.9, docker 23.0.6, docker-buildx-plugin 0.10.4). В нём решено много проблем с конфигурацией и запуском docker на Sandbox.
* `Image repository` – репозиторий для образа (соответствует ключу ```--docker-repository```)
* `Save docker image in resource` – сохранять или нет файл образа (соответствует ключу ```--docker-save-image```)
* `Push docker image` – публиковать образ в реестре или нет (соответствует ключу ```--docker-push```)
* `Docker registry` – докер-реестр (по умолчению: registry.yandex.net, соответствует ключу ```--docker-registry```)
* `Docker user` – пользователь для команды ```docker login```
* `Use image in registry as cache source` - использовать в качестве кэша образ из реестра (соответствует ключу ```--docker-use-remote-cache```)
* `Custom image version to use as cache source` - пользовательская версия образа в реестре (соответствует ключу `--docker-remote-image-version`)
* `Docker token vault name` – название ключа в `Vault`, где хранится токен для команды ```docker login```
* `Docker build args` - аргументы, которые необходимо передать в контейнер. Чтобы передать переменную окружения, вместо значения нужно вписать `None`. `None` раскрывается в соответствующее значение переменной окружения

[Пример](https://a.yandex-team.ru/arcadia/devtools/dummy_arcadia/package/hello_world_docker/a.yaml) запуска через YA_PACKAGE_2.

### Передача секрета в контейнер для docker build { #docker-build-secret }
В случае необходимости безопасно передать секрет внутрь docker container для его сборки, вы можете воспользоваться `--docker-secret id=ENV_VAR` без указания значения, где ENV_VAR - переменная окружения, которая содержит необходимое значение секрета. См. так же документацию про [docker build secrets](https://docs.docker.com/build/building/secrets/#sources).

Для того чтобы передать секрет в docker контейнер при сборке образа средствами `YA_PACKAGE_2`, нужно:
- Положить секрет в [cекретницу](https://yav.yandex-team.ru/).
- Выставить в таске `env_vars` в [соответствующей нотации](https://a.yandex-team.ru/arcadia/sandbox/projects/common/build/parameters/__init__.py?rev=r11156568#L1612-1627), для того чтобы нужные переменные окружения содержали требуемый секрет.
- Добавить в `docker_secrets` нужные переменные окружения из `env_vars`, которые содержат секреты. В этом случае `ya package` возьмёт значение из окружения, которое было задано в `evn_vars`. [Пример](https://a.yandex-team.ru/arcadia/devtools/dummy_arcadia/package/hello_world_docker/a.yaml) передачи секрета из env_vars.

{% note warning %}

НЕ передавайте секреты через Docker build args, это небезопасно. Для передачи секрета используйте docker_secrets.

[Пример](https://a.yandex-team.ru/arcadia/devtools/dummy_arcadia/package/hello_world_docker) Dockerfile с передачей секрета

{% endnote %}

### Best practice { #best-practice }

#### Slow apt install in docker { #slow-apt-install-in-docker }

Если вы наблюдаете длительное время работы `apt install` внутри `docker build` при использовании таски `YA_PACKAGE_2` и время `Package successfully packed in <secs>` значительно отличается от локального времени сборки пакета, вам нужно:
- Или рассмотреть переезд на более свежую версию ubuntu в Dockerfile (`20.04 focal` и выше)
- Или добавить ad hoc ограничение, в виде `ulimit -n 1024`, которое чинит проблему для более старых версий ubuntu (см. [пример](https://a.yandex-team.ru/arcadia/commit/r10554889))


# Сборка wheel-пакетов

Чтобы собрать wheel-пакет, нужно выполнить
```bash
ya package <package.json> --wheel
```
В json-конфиге пакета нужно описать структуру пакета со специальным скриптом `setup.py` в корне формирующим пакет, который будет вызван с параметром ```bdist_wheel```.
Для сборки python3 пакета, нужно к опции ```--wheel``` добавить опцию ```--wheel-python3```, чтобы получилось ```ya package <package.json> --wheel --wheel-python3``` .
Версию пакета достаточно указать в `package.json`, а в `setup.py` ее можно получить через переменную окружения `YA_PACKAGE_VERSION`:
```python
setup(
    <...>,
    version=os.environ.get("YA_PACKAGE_VERSION"),
)
```
Пример описания wheel-пакета можно найти по [ссылке](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/create_wheel/data/package_wheel.json).

## Публикация пакета  { #publish }
Если в вызове ```ya package``` добавить параметр ```--publish-to <repo_addr>```, то после сборки пакета он будет опубликован с помощью вызова ```twine upload --repository-url <repo_addr>```, при этом ключи от репозитория будут запрошены в консоли.

## Особенности запуска через задачу YA_PACKAGE_2 { #sandbox }
Для публикации из sandbox в таске `YA_PACKAGE_2` помимо адреса репозитория нужно указать `wheel_access_key_token_yav` и `wheel_secret_key_token_yav` ключи, которые предварительно нужно сохранить в [yav](https://yav.yandex-team.ru/) (про использование секретов из Яндекс Секретницы в sandbox можно почитать [тут](https://docs.yandex-team.ru/sandbox/dev/secret)). Подробнее про ключи pypi-репозитория можно почитать [здесь](https://wiki.yandex-team.ru/pypi/).

Чтобы собрать пакет в sandbox с помощью `YA_PACKAGE_2`, задачу необходимо выполнить в контейнере, в котором установлены `setuptools`, `wheel`, `twine`, по умолчанию используется https://sandbox.yandex-team.ru/resource/1197547734.


# Сборка aar-пакетов

Для сборки aar-пакетов, нужно выполнить
```bash
ya package --aar <package.json>
```

## Публикация пакетов { #publish }
Для публикации aar-пакетов, нужно добавить к запуску ключ ```--publish-to <settings.xml>```, `settings.xml` должен содержать в себе параметры, необходимые для загрузки пакета в `artifactory`.
Пример такого settings-файла можно найти по [ссылке](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ya/package/tests/create_aar/good_settings.xml?rev=7217997&blame=true).
При публикации пакета внутри ```ya package``` вызывается команда ```mvn -s <settings.xml> deploy:deploy-file -Dfile=<путь до собранного aar пакета>```.

# Сборка npm-пакетов
Чтобы собрать npm-пакет, нужно выполнить
```bash
ya package <pkg.json> --npm
```
Обратите внимание, что для npm-пакетов файл с именем `package.json` уже имеет предопределенное значение, и здесь требуется конфиг для ya package называть по-другому, например, `pkg.json`.
В `pkg.json` описании пакета нужно описать структуру пакета с файлом `package.json` в корне, описывающим сам npm-пакет.

## Публикация пакетов  { #publish }
Для публикации необходимо указать целевой `npm registry` через опцию ```--publish-to <npm_registry>``` и после сборки пакета он будет опубликован.
При локальном запуске с целью публикации авторизационная информация берется из локального `.npmrc`-конфига. Версию собираемого пакета можно переопределить через опцию `--custom-version`.

## Особенности запуска через задачу YA_PACKAGE { #sandbox }
При публикации из `YA_PACKAGE_2` параметр таски `npm_registry` принимает дефолтное значение `https://npm.yandex-team.ru`.
Помимо адреса репозитория нужно указать `npm_login` пользователя в npm, под которым осуществляется публикация, а также место хранения пароля пользователя в Секретнице `npm_password_yav`. Пароль нужнно предварительно нужно сохранить в [yav](https://yav.yandex-team.ru/) (про использование секретов из Яндекс Секретницы в sandbox можно почитать [тут](https://docs.yandex-team.ru/sandbox/dev/secret)).
Версию пакета в таске можно переопределить с помощью параметра `custom_version`.

Чтобы собрать пакет в sandbox с помощью YA_PACKAGE, задачу необходимо выполнить в контейнере, в котором установлен `npm`, по умолчанию используется https://sandbox.yandex-team.ru/resource/3877795087/view.



