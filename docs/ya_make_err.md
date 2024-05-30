# Сообщения об ошибках системы сборки

# Сообщения `ymake` конфигурации (построения) сборочного графа

- [BadAuto](#badauto)
- [BadDir](#baddir)
- [BadFile](#badfile)
- [BadIncl](#badincl)
- [BadSrc](#badsrc)
- [BlckLst](#blcklst)
- [DEPENDENCY_MANAGEMENT](#dependency_management)
- [DupSrc](#dupsrc)
- [Garbage](#garbage)
- [Loop](#loop)
- [NoOutput](#nooutput)
- [Syntax](#syntax)
- [UnkStatm](#unkstatm)
- [ChkPeers](#chkpeers)
- [UserErr](#usererr)
- [UserWarn](#userwarn)


## BadAuto

***TBD***

## BadDir

Ошибка `BadDir` выдаётся в тех случаях, когда в макросы, обрабатывающие пути директорий (такие как `ADDINCL`, `PEERDIR`, `SRCDIR` etc), передаются невалидные пути (путь не является директорий или использование директории ограничено из-за контекста использования или политик использования директории в Аркадии).

Пример 1 (Несуществующая директория [devtools/examples/diag/ymake/bad_dir_missing_dir/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_dir_missing_dir/)):

{% code '/devtools/examples/diag/ymake/bad_dir_missing_dir/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_dir_missing_dir
Error[-WBadDir]: in $B/devtools/examples/diag/ymake/bad_dir_missing_dir/libdiag-ymake-bad_dir_missing_dir.a: PEERDIR to missing directory: $S/missing/dir
Configure error (use -k to proceed)
$
```

Пример 2 (Ограниченная к использованию библиотека [devtools/examples/diag/ymake/bad_dir_peerdir_policy/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_dir_peerdir_policy/)):

{% code '/devtools/examples/diag/ymake/bad_dir_peerdir_policy/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_dir_peerdir_policy
Error[-WBadDir]: in $B/devtools/examples/diag/ymake/bad_dir_peerdir_policy/bad_dir_peerdir_policy: PEERDIR from $S/devtools/examples/diag/ymake/bad_dir_peerdir_policy to $S/contrib/restricted/abseil-cpp is prohibited by peerdir policy
Configure error (use -k to proceed)
$
```

Пример 3 (Отсутствие подходящего подмодуля мультимодуля [devtools/examples/diag/ymake/bad_dir_incompatible_tags/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_dir_incompatible_tags/))

{% code '/devtools/examples/diag/ymake/bad_dir_incompatible_tags/ya.make' %}

{% code '/devtools/examples/diag/ymake/bad_dir_incompatible_tags/proto/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_dir_incompatible_tags
Error[-WBadDir]: in $B/devtools/examples/diag/ymake/bad_dir_incompatible_tags/libpydiag-ymake-bad_dir_incompatible_tags.a: PEERDIR from module tagged PY3 to $S/devtools/examples/diag/ymake/bad_dir_incompatible_tags/proto is prohibited: tags are incompatible
Configure error (use -k to proceed)
$
```

## BadFile

Ошибка `BadFile` выдается в тех случаях, когда в макросы, обрабатывающие пути до регулярных файлов (`SRCS`, входные параметры (`IN`) макросов кодогенерации `PYTHON`, `RUN_PROGRAM` etc), передаются невалидные пути (путь до несуществующего файла или путь на директорию).

Пример (Несуществующий файл [devtools/examples/diag/ymake/bad_file/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_file/))

{% code '/devtools/examples/diag/ymake/bad_file/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_file
Error[-WBadFile]: in $B/devtools/examples/diag/ymake/bad_file/libdiag-ymake-bad_file.a: cannot find source file: missing_file.cpp
Configure error (use -k to proceed)
$
```

## BadIncl

Ошибка `BadIncl` выдаётся в том случае, если есть несоответствие при поиске файла с использованием информации о системных инклуд файлах (описание поиска системных инклуд файлов определяется в конфигурационной переменной `SYSINCL` в https://a.yandex-team.ru/arc/trunk/arcadia/build/ymake.core.conf?rev=r7824648#L37) и директорий поиска инклуд файлов, заданных с помощью макросов `ADDINCL`.

Пример ([devtools/examples/diag/ymake/bad_incl/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_incl/)):

{% code '/devtools/examples/diag/ymake/bad_incl/ya.make' %}

{% code '/devtools/examples/diag/ymake/bad_incl/main.cpp' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_incl
Error[-WBadIncl]: in $B/devtools/examples/diag/ymake/bad_incl/bad_incl: sysincl/addincl mismatch for include stddef.h from $S/devtools/examples/diag/ymake/bad_incl/main.cpp addincl: $S/contrib/libs/musl/include/stddef.h sysincls: ${ARCADIA_ROOT}/contrib/libs/cxxsupp/libcxx/include/stddef.h
Error[-WBadIncl]: in $B/devtools/examples/diag/ymake/bad_incl/bad_incl: could not resolve include file: bits/alltypes.h included from here: $S/contrib/libs/musl/include/stddef.h
Configure error (use -k to proceed)
$
```

## BadSrc

Сообщение `BadSrc` выдаётся в тех случаях, когда для расширения файла, указанного в макросе `SRCS` или, перечисленного в ключевых параметрах `OUT` или `STDOUT` макросов кодогереации (`PYTHON`, `RUN_PROGRAM` etc), не нашлось зарегистрированного обработчика расширения (специализации макроса `SRC`).

Пример ([devtools/examples/diag/ymake/bad_src/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/bad_src/)):

{% code '/devtools/examples/diag/ymake/bad_src/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/bad_src
Warn[-WBadSrc]: in $B/devtools/examples/diag/ymake/bad_src/bad_src: can't build anything from $B/devtools/examples/diag/ymake/bad_src/generated.c++
Warn[-WBadSrc]: in $B/devtools/examples/diag/ymake/bad_src/bad_src: can't build anything from $S/devtools/examples/diag/ymake/bad_src/dummy.unknown
Ok
$
```

## BlckLst

Ошибка `BlckLst` выдаётся в случаях, когда для сборки проекта используются сущности из _запрещённых_ верхнеуровневых директорий Аркадии. _Запрещённые_ директории (для сборки) в Аркадии определяются конфигурационной переменной [`_BLACKLIST`](https://a.yandex-team.ru/arc/trunk/arcadia/build/ymake.core.conf?rev=r7825109#L23). Эта переменная содержит набор конфигурационных файлов, в которых перечислены верхнеуровневые директории, любая сущность из которых запрещена для использования в Аркадийной сборке. Стоит отметить, что набор таких _запрещённых_ директорий зависит от типа сборки. Например, в автосборке запрещено использовать (ссылаться на) файлы из корневой директории [junk](https://a.yandex-team.ru/arc/trunk/arcadia/junk), а в локальной сборке такого запрета нет.

Пример (Использование `junk` в автосборке):
```
$ ./ya make -DAUTOCHECK junk/snermolaev/hello
Error[-WBlckLst]: in At top level: Trying to use $S/junk/snermolaev/hello/ya.make from the prohibited directory junk
Error[-WBlckLst]: in $B/junk/snermolaev/hello/privet: Trying to use $S/junk/snermolaev/hello/main.cpp from the prohibited directory junk
Error[-WBlckLst]: in $B/junk/snermolaev/hello/privet: Trying to use $S/junk/snermolaev/hello/main.hpp from the prohibited directory junk
Ok
$
```

## DEPENDENCY_MANAGEMENT
Ошибка может возникать из-за слишком строгих ограничений, заданных макросом [JAVA_DEPENDENCIES_CONFIGURATION](https://docs.yandex-team.ru/ya-make/manual/java/macros#java_dependencies_configuration).
Пример:
```
DEPENDENCY_MANAGEMENT transitive peerdir $S/contrib/java/io/netty/netty-transport-native-epoll/4.1.63.Final try overwrite forced dependency contrib/java/io/netty/netty-transport-native-epoll/4.1.100.Final
```

{% note info %}

Так же ошибка может возникать при отсутствия в аркадии `contrib/java/io/netty/netty-transport-native-epoll/4.1.100.Final` из-за баги в ya make, подробнее в [YMAKE-922](https://st.yandex-team.ru/YMAKE-922)

{% endnote %}

## DupSrc

Ошибка `DupSrc` выдается в тех случаях, когда в макросе `SRCS` один и тот же исходный файл перечислен более одного раза, а так же в случаях, когда сгенерированный исходный файл является результатом кодогенерации двух и более макросов (`PYTHON`, `RUN_PROGRAM` etc).

Пример ([devtools/examples/diag/ymake/dup_src/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/dup_src/)):
```
$ ./ya make devtools/examples/diag/ymake/dup_src/
Error[-WDupSrc]: in $B/devtools/examples/diag/ymake/dup_src/dup_src: $B/devtools/examples/diag/ymake/dup_src/main.cpp.pic.o was already added in this project. Skip command: :SRCScpp=(main.cpp)
Error[-WDupSrc]: in $B/devtools/examples/diag/ymake/dup_src/dup_src: $B/devtools/examples/diag/ymake/dup_src/main.cpp.o was already added in this project. Skip command: :SRCScpp=(main.cpp)
Configure error (use -k to proceed)
$
```

## Garbage

***TBD***

## Loop

***TBD***

## NoOutput

Ошибка `NoOutput` выдаётся в том случае, если команда, добавляемая в сборочный граф, не имеет сгенерированного артефакта (т.е. у команды отсутствует `output`). Такая ситуация может получиться, если свойство `.CMD` команды не содержит ни одного макроса с модификатором `output`. А так же, если в вызове макроса кодогенерации (`PYTHON`, `RUN_PROGRAM` etc) забыли указать один из ключевых параметров, формирующих `output` команды генерации: `OUT`, `OUT_NOAUTO`, `STDOUT` или `STDOUT_NOAUTO`.

Пример ([devtools/examples/diag/ymake/no_output/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/no_output/)):
```
OWNER(g:ymake)

LIBRARY()

RUN_PYTHON3(gen.py)

END()
```

```
$ ./ya make devtools/examples/diag/ymake/no_output
Error[-WNoOutput]: in $B/devtools/examples/diag/ymake/no_output/libdiag-ymake-no_output.a: macro 2662:PYTHON=([ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] gen.py [ ]) resulted in no outputs, can't add to graph
Configure error (use -k to proceed)
$
```

## Syntax

Ошибка `Syntax` выдаётся в тех случаях, когда пользователь допустил синтаксическую ошибку в файле описания сборки проекта `ya.make`.

Пример ([devtools/examples/diag/ymake/syntax/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/syntax/))

{% code '/devtools/examples/diag/ymake/syntax/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/syntax
Error[-WSyntax]: in $S/devtools/examples/diag/ymake/syntax/ya.make: devtools/ymake/lang/makefile_reader.cpp:78: lexer error at devtools/examples/diag/ymake/syntax/ya.make:6:9:
OWNER(ymake)

PROGRAM()

SRCS()
    main[ <- HERE ].cpp
ENDSRCS()

END()

Configure error (use -k to proceed)
$
```

## UnkStatm

Ошибка `UnkStatm` выдаётся в том случае, когда в файле описании сборки проекта `ya.make` есть вызов неизвестного макроса. Макросы, которые можно использовать в сборке, описаны в [build/ymake.core.conf](https://a.yandex-team.ru/arc/trunk/arcadia/build/ymake.core.conf) или в плагинах [build/plugins](https://a.yandex-team.ru/arc/trunk/arcadia/build/plugins), а также _внутренние_ макросы реализованные в коде `ymake` (описание таких макросов можно найти [здесь](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/ymake/yndex/builtin.cpp)).

{% note warning %}

В файлах описания сборки запрещено использовать макросы, имена которых начинается с символа подчёркивания.

{% endnote %}

Пример ([devtools/examples/diag/ymake/unk_statm/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/unk_statm/))

{% code '/devtools/examples/diag/ymake/unk_statm/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/unk_statm
Error[-WUnkStatm]: in $B/devtools/examples/diag/ymake/unk_statm/libdiag-ymake-unk_statm.a: skip unknown statement: UNKNOWN_MACRO vector of size 1
  0: lib.h

Configure error (use -k to proceed)
$
```

## ChkPeers { #chkpeers }

Сообщение выдаётся если использован генерированный файл недостижимый по `PEERDIR`.

Выглядит оно так:

```
$B/fintech/bnpl/backend/src/tags/user/notifications/libtags-user-notifications.a
Error[-WChkPeers]:used a file $B/kikimr/public/api/grpc/draft/ydb_persqueue_v1.grpc.pb.h belonging to directories ($S/kikimr/public/api/grpc/draft) which are not reachable by PEERDIR
[ Guess]: PEERDIR is probably missing: $S/fintech/bnpl/backend/src/tags/user/notifications -> $S/fintech/bnpl/backend/src/server
[  Path]: $B/fintech/bnpl/backend/src/tags/user/notifications/libtags-user-notifications.a ->
[  Path]: $B/fintech/bnpl/backend/src/tags/user/notifications/libtags-user-notifications.global.a ->
[  Path]: $B/fintech/bnpl/backend/src/tags/user/notifications/tag_creation.cpp.o ->
[  Path]: $S/fintech/bnpl/backend/src/tags/user/notifications/tag_creation.cpp ->
[  Path]: $S/fintech/bnpl/backend/src/tags/user/notifications/tag_creation.h ->
[  Path]: $S/fintech/bnpl/backend/src/server/server.h ->
[  Path]: $S/fintech/bnpl/backend/src/server/config.h ->
[  Path]: $S/kernel/common_server/server/config.h ->
[  Path]: $S/taxi/logistic-dispatcher/library/logbroker/config.h ->
[  Path]: $S/kikimr/public/sdk/cpp/client/ydb_persqueue/persqueue.h ->
[  Path]: $B/kikimr/public/api/grpc/draft/ydb_persqueue_v1.grpc.pb.h
```

## UserErr

В общем случае сообщение `UserErr` выдаётся, когда данные, пришедшие от пользователя, не совместимы с заданной конфигурацией построения проекта. Причин, по которым это могло произойти, достаточно много. Это и несовместимость условия в макросах в `BUILD_ONLY_IF` и `NO_BUILD_IF` с текущей конфигурацией построения, и пустой список аргументов для некоторых макросов (например, `RECURSE_FOR_TESTS`, `RECURSE_ROOT_RELATIVE`), и несоответствие количества аргументов в вызове макросов, и так далее. Это сообщение может быть и ошибкой, и предупреждением в зависимости от конкретного случая. Кроме того, у пользователя есть возможность индуцировать сообщение `UserErr` в файле описания сборки `ya.make` с помощь макроса `MESSAGE(FATAL_ERROR текст сообщения)`.

Пример ([devtools/examples/diag/ymake/user_err/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/user_err/))

{% code '/devtools/examples/diag/ymake/user_err/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/user_err --target-platform darwin
Error[-WUserErr]: in $S/devtools/examples/diag/ymake/user_err/ya.make:6:61: This library is built for Linux only
Configure error (use -k to proceed)
$
```

## UserWarn

Сообщение `UserWarn` может выдаваться в случаях, когда некорректное использование макросов не приводит к фатальным последствиям при конфигурировании проекта (Например, дублирование ресурса в вызове макроса `DECLARE_EXTERNAL_RESOURCE`). Кроме того, у пользователя есть возможность индуцировать это сообщение `UserWarn` в файле описания сборки `ya.make` с помощью макроса `MEESAGE(текст сообщения)`.

Пример ([devtools/examples/diag/ymake/user_warn/](https://a.yandex-team.ru/arc/trunk/arcadia/devtools/examples/diag/ymake/user_warn/))

{% code '/devtools/examples/diag/ymake/user_err/ya.make' %}

```
$ ./ya make devtools/examples/diag/ymake/user_warn
Warn[-WUserWarn]: in $S/devtools/examples/diag/ymake/user_warn/ya.make: Empty library - just a placeholder
Ok
$
```
