# ya maven-import

Импортировать артефакты из Maven-репозитория в arcadia/contrib/java.

`ya maven-import [OPTION]... [<GROUPID>:<ARTIFACTID>[:<EXTENSION>[:<CLASSIFIER>]]:<VERSION> ...]...`

## ya maven-import
Для импорта библиотек из maven репозиториев в ваш репозиторий используется команда `ya maven-import groupId:artifactId:version` (которая взаимодействует с maven). В результате выполнения команды нужная библиотека со всеми зависимостями появится в [contrib/java](https://a.yandex-team.ru/arc/trunk/arcadia/contrib/java) в виде модуля `JAVA_LIBRARY`, описанного в `groupId/artifactId/version/ya.make`(закоммитить нужно самостоятельно). После этого можно зависеть от этого модуля, используя `PEERDIR`.

### bom-файлы
Помимо непосредственно jar артефактов  `ya maven-import` умеет импортировать т.н. bom-файлы. BOM (Bill of Meterials) файлы декларируют версии для какого-то набора артефактов при помощи секции `<dependencyManagement>`, но не создают зависимости на сами артефакты [пример такого файла](https://search.maven.org/artifact/org.springframework.boot/spring-boot-dependencies/2.2.2.RELEASE/pom). В Аркадии они принимают вид `.inc` файла с DEPENDENCY_MANAGEMENT ([пример](https://a.yandex-team.ru/arcadia/contrib/java/ch/qos/logback/logback-classic/1.1.2/ya.dependency_management.inc)). Данные файлы при необходимости создаются сами (если артефакт их порождает), для их импорта ничего дополнительно делать не нужно. Предполагается, что пользователи могут добавлять их в свои проекты при помощи макроса `INCLUDE`.

### Работа с лицензиями
Лицензии (макрос `LICENSE()`) заполняется на базе информации из тегов `<license>` в `pom.xml` файлах. Заполнение макроса работает только для контрибов, которые заливаются из "доверенных" репозиториев. Их список содержится в [этом файле](https://a.yandex-team.ru/arcadia/contrib/java/TRUSTED_REPOS). В тегах содержатся неформалиованные строки, которые требуется канонизировать. Для канонизации используется [ya tool license-analyzer](https://docs.yandex-team.ru/devtools/rules/contrib/licenses#find-license-java). К сожалению под windows он не работает, поэтому известные трансформации дампятся в [специальный файл](https://a.yandex-team.ru/arcadia/contrib/java/LICENSE_ALIASES) (под windows вместо license-analyzera'а используется он).

### Обзор флагов

* `-o=CONTRIB_OWNER, --owner=CONTRIB_OWNER` - изменить значение макроса OWNER у заливаемых контрибов (по умолчанию - g:java-contrib)
* `-r=REMOTE_REPOS, --remote-repository=REMOTE_REPOS` - кастомизировать список maven-репозиториев, в которых будут искаться артефакты (по умолчанию репозитории из [contrib/java/MAVEN_REPOS](https://a.yandex-team.ru/arc/trunk/arcadia/contrib/java/MAVEN_REPOS))
* `-d, --dry-run` - скачать артфеакты из maven, но не загружать в sandbox и соответсвенно не трогать файлы в contrib/java (флаг для дебага)
* `--unified-mode` - использовать улучшенную систему ресолва артефактов в maven (по умолчанию включена, флаг оставлен для совместимости со старыми скриптами)
* `--legacy-mode` - использовать более старую подсистему ресолва артефактов, в редких случаях она может поресолвить то, на чем ломается unified (см известные проблемы)
* `--no-write-licenses` - не заполнять `LICENSE()`
* `--no-canonize-licenses` - не пытаться канонизировать значения `<license>` из pom.xml при помощи [ya tool license-analyzer](https://docs.yandex-team.ru/devtools/rules/contrib/licenses#find-license-java)
* `--minimal-pom-validation` - опустить проверку корректности pom.xml на apache-стандарт (иногда даже в central встречаются pom'ники, не соответствующие стандарту maven 2) - включена по дефолту
* `--strict-pom-validation` - проверять pom.xml на соответсвие apache-стандарту

## Опции
```
  Bullet-proof options
    -o=CONTRIB_OWNER, --owner=CONTRIB_OWNER
                        Libraries owner. Default: g:java-contrib.
    -r=REMOTE_REPOS, --remote-repository=REMOTE_REPOS
                        Specify remote repository manually to improve performance.
    -d, --dry-run       Do not upload artifacts and do not modify contrib.
    --legacy-mode       Run legacy importer (instead of unified).
    --unified-mode      Run unified importer (instead of legacy).
    -j=BUILD_THREADS, --threads=BUILD_THREADS
                        Build threads count (default: 32)
    -T                  Do not rewrite output information (ninja/make)
    --do-not-output-stderrs
                        Do not output any stderrs
    -h, --help          Print help
    --checkout          Checkout missing dirs
  Advanced options
    --mask-roots        Mask source and build root paths in stderr
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30GiB)
  Developers options
    --ymake-bin=YMAKE_BIN
                        Path to ymake binary
    --no-ymake-resource Do not use ymake binary as part of build commands
    --no-ya-bin-resource
                        Do not use ya-bin binary as part of build commands
  Sandbox upload options
    --sandbox-owner=RESOURCE_OWNER
                        User name to own data saved to sandbox (default: spreis)
    --sandbox-url=SANDBOX_URL
                        sandbox url to use for storing canonical file (default: https://sandbox.yandex-team.ru)
    --task-kill-timeout=TASK_KILL_TIMEOUT
                        Timeout in seconds for sandbox uploading task
    --sandbox           Upload to Sandbox
    -s=RESOURCE_OWNER   User name to own data saved to sandbox (default: spreis)
    --skynet            Should upload using skynet
    --http              Should upload using http
    --sandbox-mds       Should upload using MDS as storage
  Authorization options
    --key=SSH_KEYS      Path to private ssh key to exchange for OAuth token
    --token=OAUTH_TOKEN oAuth token (default: [HIDDEN])
    --user=USERNAME     Custom user name for authorization (default: spreis)
    -t=OAUTH_TOKEN      oAuth token (default: [HIDDEN])
```
