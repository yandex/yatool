# ya java

Получить информацию о зависимостях в java-проектах.

`ya java <subcommand>`


## Доступные команды
```
 classpath            Print classpath
 dependency-tree      Print dependency tree
 find-all-paths       Find all PEERDIR paths of between two targets
```
# Java: вспомогательные команды

## ya maven-import
Для импорта библиотек из maven репозиториев в аркадию используется команда `ya maven-import groupId:artifactId:version` (которая взаимодействует с maven). В результате выполнения команды нужная библиотека со всеми зависимостями появится в [contrib/java](https://a.yandex-team.ru/arc/trunk/arcadia/contrib/java) в виде модуля `JAVA_LIBRARY`, описанного в `groupId/artifactId/version/ya.make`(закоммитить нужно самостоятельно). После этого можно зависеть от этого модуля, используя `PEERDIR`.

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


## ya java classpath
Команда позволяет узнать, какие библиотеки попали в classpath данного модуля после применения правил `DEPENDENCY_MANAGEMENT`, `EXCLUDE` и разрешения конфликтов версий. Например:
```
~/iceberg/misc$ ya java classpath
iceberg/misc/iceberg-misc.jar
iceberg/bolts/iceberg-bolts.jar
contrib/java/junit/junit/4.12/junit-4.12.jar
contrib/java/org/hamcrest/hamcrest-core/1.3/hamcrest-core-1.3.jar
contrib/java/org/openjdk/jmh/jmh-generator-annprocess/1.11.2/jmh-generator-annprocess-1.11.2.jar
contrib/java/org/openjdk/jmh/jmh-core/1.11.2/jmh-core-1.11.2.jar
contrib/java/net/sf/jopt-simple/jopt-simple/4.6/jopt-simple-4.6.jar
contrib/java/org/apache/commons/commons-math3/3.2/commons-math3-3.2.jar
contrib/java/com/google/code/findbugs/jsr305/3.0.0/findbugs-jsr305-3.0.0.jar
iceberg/misc-bender-annotations/iceberg-misc-bender-annotations.jar
iceberg/misc-signal/iceberg-misc-signal.jar
devtools/jtest/devtools-jtest.jar
contrib/java/log4j/log4j/1.2.17/log4j-1.2.17.jar
contrib/java/org/apache/logging/log4j/log4j-api/2.5/log4j-api-2.5.jar
contrib/java/org/apache/logging/log4j/log4j-core/2.5/log4j-core-2.5.jar
contrib/java/commons-logging/commons-logging/1.1.1/commons-logging-1.1.1.jar
contrib/java/org/slf4j/slf4j-api/1.7.12/slf4j-api-1.7.12.jar
contrib/java/joda-time/joda-time/2.5/joda-time-2.5.jar
contrib/java/commons-lang/commons-lang/2.4/commons-lang-2.4.jar
contrib/java/org/ow2/asm/asm-all/5.0.3/asm-all-5.0.3.jar
contrib/java/javax/servlet/javax.servlet-api/3.0.1/javax.servlet-api-3.0.1.jar
contrib/java/javax/annotation/jsr250-api/1.0/jsr250-api-1.0.jar
contrib/java/org/productivity/syslog4j/0.9.46/syslog4j-0.9.46.jar
contrib/java/org/easymock/easymock/3.4/easymock-3.4.jar
contrib/java/org/objenesis/objenesis/2.4/objenesis-2.4.jar
```

## ya java test-classpath
Команда, позволяющая узнать classpath, с которым будет запускаться данный Java тест. Например:
```
~/iceberg/misc/ut$ ya java test-classpath 
iceberg/misc/ut:
	iceberg/misc/ut/misc-ut.jar
	iceberg/misc/testlib/iceberg-misc-testlib.jar
	devtools/jtest/devtools-jtest.jar
	contrib/java/com/google/code/gson/gson/2.8.6/gson-gson-2.8.6.jar
	contrib/java/com/beust/jcommander/1.72/jcommander-1.72.jar
	iceberg/misc/iceberg-misc.jar
	iceberg/bolts/iceberg-bolts.jar
	contrib/java/com/google/code/findbugs/jsr305/3.0.2/findbugs-jsr305-3.0.2.jar
	iceberg/misc-bender-annotations/iceberg-misc-bender-annotations.jar
	iceberg/misc-signal/iceberg-misc-signal.jar
	contrib/java/log4j/log4j/1.2.17/log4j-1.2.17.jar
	contrib/java/org/apache/logging/log4j/log4j-api/2.11.0/log4j-api-2.11.0.jar
	contrib/java/org/apache/logging/log4j/log4j-core/2.11.0/log4j-core-2.11.0.jar
	contrib/java/commons-logging/commons-logging/1.1.1/commons-logging-1.1.1.jar
	contrib/java/org/slf4j/slf4j-api/1.7.25/slf4j-api-1.7.25.jar
	contrib/java/joda-time/joda-time/2.10.1/joda-time-2.10.1.jar
	contrib/java/org/apache/commons/commons-lang3/3.5/commons-lang3-3.5.jar
	contrib/java/org/ow2/asm/asm-all/5.0.3/asm-all-5.0.3.jar
	contrib/java/javax/servlet/javax.servlet-api/3.0.1/javax.servlet-api-3.0.1.jar
	contrib/java/org/productivity/syslog4j/0.9.46/syslog4j-0.9.46.jar
	contrib/java/org/objenesis/objenesis/2.4/objenesis-2.4.jar
	contrib/java/com/sun/activation/javax.activation/1.2.0/javax.activation-1.2.0.jar
	contrib/java/javax/annotation/javax.annotation-api/1.3.1/annotation-javax.annotation-api-1.3.1.jar
	iceberg/bolts/testlib/iceberg-bolts-testlib.jar
	contrib/java/junit/junit/4.12/junit-4.12.jar
	contrib/java/org/hamcrest/hamcrest-core/1.3/hamcrest-core-1.3.jar
	contrib/java/org/openjdk/jmh/jmh-generator-annprocess/1.19/jmh-generator-annprocess-1.19.jar
	contrib/java/org/openjdk/jmh/jmh-core/1.19/jmh-core-1.19.jar
	contrib/java/net/sf/jopt-simple/jopt-simple/4.6/jopt-simple-4.6.jar
	contrib/java/org/apache/commons/commons-math3/3.2/commons-math3-3.2.jar
	contrib/java/org/easymock/easymock/3.4/easymock-3.4.jar
	devtools/junit-runner/devtools-junit-runner.jar
```

## ya java dependency-tree
Команда печатает дерево зависимостей данного модуля с объяснением выбора конкретных версий библиотек в тех ситуациях, когда имел место конфликт. Пример использования:
```
~/arcadia/iceberg/misc$ ya java dependency-tree
iceberg/misc
|-->iceberg/bolts
|   |-->contrib/java/junit/junit/4.12
|   |   |-->contrib/java/org/hamcrest/hamcrest-core/1.3
|   |-->contrib/java/org/openjdk/jmh/jmh-generator-annprocess/1.11.2
|   |   |-->contrib/java/org/openjdk/jmh/jmh-core/1.11.2
|   |   |   |-->contrib/java/net/sf/jopt-simple/jopt-simple/4.6
|   |   |   |-->contrib/java/org/apache/commons/commons-math3/3.2
|   |-->contrib/java/com/google/code/findbugs/jsr305/3.0.0
|-->iceberg/misc-bender-annotations
|-->iceberg/misc-signal
|-->devtools/jtest
|-->contrib/java/log4j/log4j/1.2.17
|-->contrib/java/org/apache/logging/log4j/log4j-api/2.5
|-->contrib/java/org/apache/logging/log4j/log4j-core/2.5
|   |-->contrib/java/org/apache/logging/log4j/log4j-api/2.5 (*)
|-->contrib/java/commons-logging/commons-logging/1.1.1
|-->contrib/java/org/slf4j/slf4j-api/1.7.12
|-->contrib/java/joda-time/joda-time/2.5
|-->contrib/java/commons-lang/commons-lang/2.4
|-->contrib/java/org/ow2/asm/asm-all/5.0.3
|-->contrib/java/javax/servlet/javax.servlet-api/3.0.1
|-->contrib/java/javax/annotation/jsr250-api/1.0
|-->contrib/java/org/productivity/syslog4j/0.9.46
|-->contrib/java/org/easymock/easymock/3.4
|   |-->contrib/java/org/objenesis/objenesis/2.2 (omitted because of confict with 2.4)
|-->contrib/java/junit/junit/4.12 (*)
|-->contrib/java/org/objenesis/objenesis/2.4
```
