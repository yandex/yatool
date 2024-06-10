## Сообщения об ошибках системы сборки

- [BadDir](#baddir)
- [BadFile](#badfile)
- [BadIncl](#badincl)
- [BadSrc](#badsrc)
- [BlckLst](#blcklst)
- [DEPENDENCY_MANAGEMENT](#dependency_management)
- [DupSrc](#dupsrc)
- [NoOutput](#nooutput)
- [Syntax](#syntax)
- [UnkStatm](#unkstatm)
- [ChkPeers](#chkpeers)
- [UserErr](#usererr)
- [UserWarn](#userwarn)

### BadDir

Ошибка `BadDir` возникает, когда макросы, работающие с путями директорий (например, `ADDINCL`, `PEERDIR`, `SRCDIR` и другие), получают некорректные пути. Это может происходить если переданный путь не является директорией или его использование ограничено в силу контекста или политики использования директорий.

### BadFile

Ошибка `BadFile` возникает, когда макросы, обрабатывающие пути к обычным файлам (например, `SRCS`, входные параметры (`IN`) макросов кодогенерации `PYTHON`, `RUN_PROGRAM` и других), получают некорректные пути. Это может происходить если указанный путь ведет к несуществующему файлу или к директории вместо файла.

### BadIncl

Ошибка `BadIncl` возникает, когда происходит несоответствие при поиске файла, используя информацию о системных `#include`файлах. Поиск системных `#include` файлов определяется в конфигурационной переменной `SYSINCL`, а директории поиска `#include` файлов задаются с помощью макросов `ADDINCL`.

### BadSrc

Сообщение `BadSrc` возникает, когда для расширения файла, указанного в макросе `SRCS` или перечисленного в ключевых параметрах `OUT` или `STDOUT` макросов кодогенерации (`PYTHON`, `RUN_PROGRAM` и других), не найден зарегистрированный обработчик расширения (специализация макроса `SRC`).

### BlckLst

Ошибка `BlckLst` возникает, когда при сборке проекта используются сущности из запрещенных верхнеуровневых директорий. Запрещенные директории для сборки определяются конфигурационной переменной `_BLACKLIST`, которая содержит набор конфигурационных файлов, перечисляющих верхнеуровневые директории, использование которых запрещено. По умолчанию в локальной сборке этот запрет не действует.

### DEPENDENCY_MANAGEMENT

Ошибка может возникнуть из-за излишне строгих ограничений, установленных макросом `JAVA_DEPENDENCIES_CONFIGURATION`.

### DupSrc

Ошибка `DupSrc` возникает, когда один и тот же исходный файл указан более одного раза в макросе `SRCS`, а также в случаях, когда сгенерированный исходный файл является результатом кодогенерации двух или более макросов (`PYTHON`, `RUN_PROGRAM` и других).

### NoOutput

Ошибка `NoOutput` возникает, когда команда, добавляемая в сборочный граф, не имеет сгенерированной цели (т.е. у команды отсутствует `output`). Такая ситуация может возникнуть, если свойство `.CMD` команды не содержит ни одного макроса с модификатором `output`, а также если при вызове макроса кодогенерации (`PYTHON`, `RUN_PROGRAM` и других) забыли указать один из ключевых параметров, формирующих `output` команды генерации: `OUT`, `OUT_NOAUTO`, `STDOUT` или `STDOUT_NOAUTO`.

### Syntax

Ошибка `Syntax` возникает, когда пользователь допускает синтаксическую ошибку в файле описания сборки проекта `ya.make`.

### UnkStatm

Ошибка `UnkStatm` возникает, когда в файле описания сборки проекта `ya.make` используется неизвестный макрос. 

В файлах описания сборки запрещено использовать макросы, имена которых начинается с символа подчёркивания.

### ChkPeers

Сообщение выдаётся, если используется сгенерированный файл, недостижимый по `PEERDIR`.

### UserErr

Сообщение `UserErr` выдается, когда данные, предоставленные пользователем, не совместимы с заданной конфигурацией построения проекта. Причин для этого может быть множество: несовместимость условий в макросах `BUILD_ONLY_IF` и `NO_BUILD_IF` с текущей конфигурацией построения, пустые списки аргументов для некоторых макросов (например, `RECURSE_FOR_TESTS`, `RECURSE_ROOT_RELATIVE`), несоответствие количества аргументов в вызове макросов и другие. Это сообщение может быть как ошибкой, так и предупреждением, в зависимости от конкретного случая. Кроме того, пользователь может самостоятельно инициировать сообщение `UserErr` в файле описания сборки `ya.make` с помощью макроса `MESSAGE` (`FATAL_ERROR текст сообщения`).

### UserWarn

Сообщение `UserWarn` может выдаваться в случаях, когда некорректное использование макросов не приводит к фатальным последствиям при конфигурировании проекта (например, дублирование ресурса в вызове макроса `DECLARE_EXTERNAL_RESOURCE`). Кроме того, пользователь может инициировать это сообщение `UserWarn` в файле описания сборки `ya.make` с помощью макроса `MESSAGE`(текст сообщения).