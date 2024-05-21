## Опции ya dump compile-commands и compilation-database

Выводит список JSON-описаний сборочных команд через запятую. Каждая команда включает три свойства: "command", "directory" и "file".

**Пример вывода **
```
{
        "command": "clang++ --target=x86_64-linux-gnu --sysroot=~/.ya/tools/v3/1966560555 -B~/.ya/tools/v3/1966560555/usr/bin -c -o ~/ydbwork/ydb/yt/yt/core/_/logging/zstd_compression.cpp.o -I~/ydbwork/ydb -I~/ydbwork/ydb -I~/ydbwork/ydb/contrib/libs/linux-headers -I~/ydbwork/ydb/contrib/libs/linux-headers/_nf -I~/ydbwork/ydb/contrib/libs/cxxsupp/libcxx/include -I~/ydbwork/ydb/contrib/libs/cxxsupp/libcxxrt/include -I~/ydbwork/ydb/contrib/libs/clang16-rt/include -I~/ydbwork/ydb/contrib/libs/zlib/include -I~/ydbwork/ydb/contrib/libs/double-conversion -I~/ydbwork/ydb/contrib/libs/libc_compat/include/readpassphrase -I~/ydbwork/ydb/contrib/libs/libc_compat/reallocarray -I~/ydbwork/ydb/contrib/libs/libc_compat/random -I~/ydbwork/ydb/contrib/libs/snappy/include -I~/ydbwork/ydb/contrib/libs/c-ares/include -I~/ydbwork/ydb/contrib/libs/farmhash/include -I~/ydbwork/ydb/contrib/libs/openssl/include -I~/ydbwork/ydb/contrib/libs/brotli/include -I~/ydbwork/ydb/yt/yt/build -I~/ydbwork/ydb/yt -I~/ydbwork/ydb/contrib/libs/protobuf/src -pipe -m64 -g -ggnu-pubnames -fexceptions -fno-common -ffunction-sections -fdata-sections -fuse-init-array -fcolor-diagnostics -faligned-allocation -fdebug-default-version=4 -fstack-protector -Wall -Wextra -Wno-parentheses -Wno-implicit-const-int-float-conversion -Wno-unknown-warning-option -Werror -DARCADIA_ROOT=~/ydbwork/ydb -DARCADIA_BUILD_ROOT=~/ydbwork/ydb -D_THREAD_SAFE -D_PTHREADS -D_REENTRANT -D_LARGEFILE_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_YNDX_LIBUNWIND_ENABLE_EXCEPTION_BACKTRACE -UNDEBUG -D__LONG_LONG_SUPPORTED -DSSE_ENABLED=1 -DSSE3_ENABLED=1 -DSSSE3_ENABLED=1 -DSSE41_ENABLED=1 -DSSE42_ENABLED=1 -DPOPCNT_ENABLED=1 -DCX16_ENABLED=1 -mpclmul -D_libunwind_ -DLIBCXX_BUILDING_LIBCXXRT -DCARES_STATICLIB -Wno-array-parameter -Wno-deprecate-lax-vec-conv-all -Wno-unqualified-std-cast-call -Wno-unused-but-set-parameter -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-address-of-packed-member -DCATBOOST_OPENSOURCE=yes -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt -mcx16 -Wno-array-parameter -Wno-deprecate-lax-vec-conv-all -Wno-unqualified-std-cast-call -Wno-unused-but-set-parameter -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-address-of-packed-member -std=c++20 -Woverloaded-virtual -Wimport-preprocessor-directive-pedantic -Wno-ambiguous-reversed-operator -Wno-defaulted-function-deleted -Wno-deprecated-anon-enum-enum-conversion -Wno-deprecated-enum-enum-conversion -Wno-deprecated-enum-float-conversion -Wno-deprecated-volatile -Wno-pessimizing-move -Wno-return-std-move -Wno-undefined-var-template -Wdeprecated-this-capture -nostdinc++ -DCATBOOST_OPENSOURCE=yes -nostdinc++ ~/ydbwork/ydb/yt/yt/core/logging/zstd_compression.cpp",
        "directory": "~/ydbwork/ydb",
        "file": "~/ydbwork/ydb/yt/yt/core/logging/zstd_compression.cpp"
    }
```
В параметре `command` указаны используемый компилятор, целевая платформа, зависимости и другая информация. В `directory` папка проекта и в `file` сам файл.

Подкоманды `compile-command`s и `compilation-database` поддерживают большинство сборочных опций `ya make`:
* Опция `--rebuild` применяется  для выполнения полной пересборки проекта, игнорируя все ранее сгенерированные результаты и выводит список описаний сборочных команд. Удаляются все промежуточные и итоговые файлы, созданные во время прошлых процессов сборки, чтобы исключить влияние устаревших или некорректных данных на новую сборку.
* Опции `-C=BUILD_TARGETS`, `--target=BUILD_TARGETS` применяются, для установки конкретных целей сборки, задаваемых значением `BUILD_TARGETS` и выводит список описаний сборочных команд в формате `JSON`.
* Параметры `-j=BUILD_THREADS`, `--threads=BUILD_THREADS` используются для указания число рабочих потоков сборки, которые должны быть задействованы в процессе сборки проекта.
* При использовании опции `--clear` будут удалены временные данные и результаты предыдущих сборок, которые находятся в директории проекта и выводит список описаний сборочных команд
* Опция `--add-result=ADD_RESULT` предназначена для управления результатами, выдаваемыми системой сборки. Она дает возможность точно определить, какие файлы следует включать в итоговый набор результатов.
* Опция `--add-protobuf-result` позволяет системе сборки явно получить сгенерированный исходный код Protobuf для соответствующих языков программирования.
* Опция `--add-flatbuf-result` предназначена для того, чтобы система сборки могла автоматически получить сгенерированный исходный код выходных файлов FlatBuffers (flatc) для соответствующих языков программирования.
* `--replace-result` - Собирать только цели, указанные в `--add-result` и вывести список описаний сборочных команд.
* Опция `--force-build-depends` обеспечивает комплексную подготовку к тестированию за счет принудительной сборки всех зависимостей, объявленных в `DEPENDS`.
* Опции `-R` и `--ignore-recurses` предотвратят автоматическую сборку проектов, объявленных с использованием макроса `RECURSE` в файлах `ya.make`.
* `--no-src-links` - Не создавать символические ссылки в исходной директории и вывести список описаний сборочных команд.
* Опция `--stat` используется для отображения статистики выполнения сборки.
* Опция `-v` или `--verbose`,  служит для активации режима подробного вывода.
* Активация опции `-T` приводит к тому, что статус сборки выводится в построчном режиме.
* Опция `-D=FLAGS` позволяет определять или переопределять переменные среды сборки (сборочные флаги), задавая их имя и значение.
* `--cache-stat` — перед сборкой выдать статистику по наполнению локального кэша и вывести список описаний сборочных команд.
