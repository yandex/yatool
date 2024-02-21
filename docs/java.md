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
