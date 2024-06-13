## ya test

Команда `ya test` предназначена для тестирования программных проектов.

Она позволяет запускать тесты, проверять корректность кода, соответствие стандартам оформления, устойчивость к ошибкам и другие аспекты.

Основные возможности команды `ya test`:

* [Запуск тестов](#Запуск-тестов): Выполняет тесты, указанные в проекте.
* [Описание тестов](#Описание-тестов): Тесты описываются в специальном файле ya.make, где можно указать зависимости, параметры, теги и требования к ресурсам.
* [Вывод результатов](#Управление-выводом-результатов): Сохраняет результаты тестов, предоставляет информацию о пройденных и неудачных тестах.
* [Отладка](#Отладка): Включает режимы отладки для детального анализа проблем.
* [Фильтрация тестов](#Выборочное-тестирование): Позволяет запускать только выбранные тесты по имени, тегам, размеру и типу.

Команда `ya test -h` или `--help` выводит справочную информацию.  
Используйте `-hh` для отображения дополнительных опций и `-hhh` для получения еще более детальной информации.

Общий синтаксис команды
```
 ya test [OPTION]… [TARGET]…
```
Параметры:
- [OPTION]: Параметры, уточняющие, как должны быть выполнены тесты (выбор конкретных тестов, вывод результатов и т.д.). Можно указывать несколько опций через пробел.
- [TARGET]: Аргументы, определяющие конкретные цели тестирования (имена тестовых наборов или индивидуальных тестов). Можно указать несколько целей через пробел.

Основные понятия

* Test: Индивидуальная проверка кода на корректность, стандарты оформления и устойчивость к ошибкам.
* Chunk: Группа тестов, выполняемых как единственный узел в графе команд сборки.
* Suite: Коллекция тестов одного типа с общими зависимостями и параметрами.

Взаимосвязь между понятиями
- Тесты объединяются в Chunk на основе общей логики и последовательности выполнения.
- Chunks группируются в Suite, который предоставляет контексты и параметры для всех входящих в него тестов.
- Suite является основной единицей для запуска и фильтрации тестов, а также отслеживания зависимостей и ошибок.

### Описание тестов

Тесты описываются в файле `ya.make`. 

Можно использовать макросы и опции для задания параметров и зависимостей:

- `DEPENDS(path1 [path2...])`: Указывает зависимости проекта, которые необходимы для запуска тестов.
- `SIZE(SMALL | MEDIUM | LARGE)`: Определяет размер теста, что помогает в управлении временем выполнения и ресурсами (значения: SMALL, MEDIUM, LARGE).
- `TIMEOUT(time)`: Устанавливает максимальное время выполнения теста.
- `TAG(tag1 [tag2...])`: Добавляет теги к тестам, что помогает в их категоризации и фильтрации.
- `REQUIREMENTS()`: Указывает требования к ресурсам для выполнения тестов, такие как количество процессоров, объем памяти, использование диска и т.д.
- `ENV(key=[value])`: Устанавливает переменные окружения, нужные для выполнения тестов.
- `SKIP_TEST(Reason)`: Отключает все тесты в модульной сборке. Параметр `Reason` указывает причину отключения. 

### Запуск тестов

При запуске тестов в локальной среде разработки необходимо учитывать несколько ключевых моментов.
Все тесты запускаются в режиме отладки (`debug`).  
Время выполнения тестов зависит от их размеров: SMALL, MEDIUM или LARGE.  
Результаты тестов сохраняются в директорию `test-results``. Внутри нее находятся поддиректории с логами и файлами, порожденными тестами.  
Если тесты завершились неуспешно, в консоль выводится информация о путях к результатам.  
Кроме того, для корректной работы некоторых тестов необходимо наладить их взаимодействие с тестовым окружением.

Для простого запуска тестов есть следующие ключи командной строки:

- `-t` — запустить только SMALL тесты.
- `-tt` — запустить SMALL и MEDIUM тесты.
- `-ttt`, `-A`, `--run-all-tests` — запустить тесты всех размеров.
- `--test-param=TEST_PARAM` — параметризовать тесты значениями из командной строки. `TEST _PARAM` имеет вид `name=val`, доступ к параметрам зависит от используемого фреймворка.
- `--retest`: Принудительный перезапуск тестов без использования кэша.

**Пример**
```
$ ya test -t devtools/examples/tutorials/python
```
Запустит все тесты, которые найдёт по `RECURSE`/`RECURSE_FOR_TESTS` от `devtools/examples/tutorials/python`, включая тесты стиля и тесты импорта для Python. 

Использует следующие умолчания для сборки:
- Платформа будет определена по *реальной платформе*, на которой запущена команда `ya test`.
- Тесты будут собраны в режиме `debug` — он используется по умолчанию.
- Кроме тестов будут собраны все остальные цели (библиотеки и программы), достижимые по `RECURSE`/`RECURSE_FOR_TESTS` от `devtools/examples/tutorials/python`. 
Это включает сборку всех необходимых зависимостей.

По умолчанию система сборки выполнит все запрошенные тесты.  
После их завершения для всех упавших тестов будет предоставлена краткая информация о провалах, включая ссылки на подробные данные.   
Для успешных и проигнорированных тестов будет выведен только общий статус с указанием их количества.

### Управление выводом результатов
Команда `ya test` предоставляет несколько опций для управления выводом результатов тестирования. 

Эти опции позволяют контролировать объем информации, которые возвращаются после запуска тестов, и помогают быстрее находить и исправлять ошибки:

* `-L`, `--list-tests`: Выводит список тестов, которые будут выполнены.
```
ya test -tL
```
* `--fail-fast`: Завершает выполнение тестов при первой неудаче.
```
ya test -t --fail-fast
```
* `-P`, `--show-passed-tests`: Показать пройденные тесты.
```
ya test -t --show-passed-tests
```
* `--show-skipped-tests`: Показать пропущенные тесты.
* `--show-metrics`: Показать метрики тестов.

В конце выполнения тестов для всех упавших тестов будет выдана краткая информация о невыполнении (включая ссылки на более полную информацию). 
Для прошедших и проигнорированных (отфильтрованных) тестов будет выдан только общий короткий статус с количеством тех и других тестов.

Примеры полного отчета при использовании различных опций:
```
ya test -t --show-passed-tests --show-skipped-tests --show-metrics
```
В этом примере выводится информация о всех успешно пройденных тестах, отфильтрованных тестах, а также метрики тестирования, что дает полный обзор выполненных действий и результатов тестирования.

**Пример**
```
# Список всех пользовательских тестов (без тестов стиля, импортов и подобных проверок)
ya test -AL --regular-tests devtools/examples/tutorials/python
```
Команда `ya test` поддерживает параллельный запуск тестов.

### Отладка
Команда ya test предлагает несколько опций для отладки:
* `--test-debug`: Включить режим отладки тестов (PID, дополнительные параметры).
* `--pdb`, `--gdb`, `--dlv`: Запустить тесты с интеграцией в различные отладчики (для Python, C++, Go).

### Выборочное тестирование

Команда `ya test` поддерживает большое количество опций для фильтрации и выборочного запуска тестов.

Фильтрация тестов:

1. `-F=TESTS_FILTERS`, `--test-filter=TESTS_FILTERS`: Запуск только определенных тестов по имени, шаблону или фильтру.
```
ya test -A -F "subname"
```
В фильтрах можно использовать символы подстановки, такие как `*`, который соответствует любому количеству символов. 
Каждый последующий фильтр расширяет множество тестов для запуска.
```
$ ya test -t -F <file>.py::<ClassName>::*
```
2. `--test-tag=TEST_TAGS_FILTER`: Запуск тестов, помеченных определенными тегами.
```
ya test -A --test-tag tag1+tag2-tag3
```
Эта команда запустит все тесты, у которых есть теги `tag1` и `tag2` и нет тега `tag3`.

3. `--test-size=TEST_SIZE_FILTERS`: Запуск тестов определенного размера (SMALL, MEDIUM, LARGE).
```
ya test -A --test-size=MEDIUM
```
4. `--test-type=TEST_TYPE_FILTERS`: Запуск тестов определенного типа (например, `UNITTEST`, `PYTEST`).
```
ya test -tt --test-type unittest+gtest
```
Эта команда запустит только тесты типов unittest и gtest.

5. `--test-filename=TEST_FILES_FILTER`: Запуск тестов из указанного исходного файла.
```
ya test -A --test-filename=test_example.py
```
6. `-X`, `--last-failed-tests`: Запустить только тесты, упавшие в предыдущем запуске.
7. `--regular-tests`: Запустить только пользовательские тесты.
8. `--style`: Запустить только тесты стиля.

### Проверки кода и корректности данных

В системе сборки имеются разнообразные инструменты для проверки кода и данных, которые гарантируют соответствие стандартам качества, стиля и безопасности. Эти инструменты включают линтеры, статический анализ и другие средства, предназначенные для различных языков программирования.

#### Python

Для языка `Python`, `flake8` автоматически проверяет стиль кода для всех подключенных через `ya.make` файлов
в секциях `PY_SRCS` и `TEST_SRCS`, включая плагины для проверки длины строки до 200 символов 
и игнорирование ошибок с помощью комментариев `# noqa` или `# noqa: E101`.  
Ошибку `F401` можно подавить в файле `__init__.py` с помощью `# flake8 noqa: F401`.  
Для директории `contrib` можно отключить проверку стиля через макрос `NO_LINT()`.  
Для проектов на Python 3 линтер `black` подключается с помощью макроса `STYLE_PYTHON()`, который генерирует тесты для проверки стиля кода.
Запустить тесты `black` можно командой: 
```
ya test -t --test-type black.
```
Импорт-тесты проверяют импортируемость программ `Python`, собранных из модулей, обнаруживая конфликты и отсутствующие файлы.  
Эти проверки называются `import_test`, они не являются `style-тестами` и собираются с использованием `inline-сборки`.  
Отключить проверку стиля можно с помощью `NO_CHECK_IMPORTS`, указав конкретные исключения.

#### Java

Для исходных файлов на `Java`, стиль кода проверяется утилитой `checkstyle` на обычном и строгом уровнях, последний включается макросом `LINT(strict)`, а конфигурационные файлы хранятся в директории `resource`.
Опционально можно проверить наличие дублирующихся классов в `classpath` с помощью макроса `CHECK_JAVA_DEPS(yes)`.

#### Kotlin

Проекты на `Kotlin` автоматически проверяются утилитой `ktlint` для стиля кода.
Проверку можно отключить через макрос `NO_LINT(ktlint)` в файле сборки, а исключения можно добавить через макрос `KTLINT_BASELINE_FILE`.

#### Go

Для проектов на `Go` используются инструменты `gofmt` для проверки стиля кода и `govet` для статического анализа подозрительных конструкций и антипаттернов.

#### C++

Для кода на `C++` применяется `clang-tidy`, который используется для статического анализа.
Его можно запустить с флагом `ya test -t -DTIDY`.

Пример ya.make файла с проверками стиля может выглядеть так:
```
OWNER(g:some-group)

PY3_LIBRARY()

PY_SRCS(
    src/init.py
    src/module.py
)

STYLE_PYTHON()

NO_CHECK_IMPORTS(
    devtools.pylibrary.
)

END()
```
### Канонизация (и переканонизация)

Для некоторых типов тестов в системе сборки поддерживается механизм сравнения с эталонными (каноническими) данными. 

1. Канонические данные: Это эталонные данные, с которыми сравниваются результаты теста.
2. Канонизация: Процесс обновления эталонных данных, чтобы соответствовать текущим результатам тестов.
3. Переканонизация: Повторная канонизация, проводимая для обновления эталонных данных после изменения тестов или их окружения.
Если  данные нужно обновить воспользуйтесь опцией `-Z` (`--canonize-tests`).

При локальном запуске, эталонные данные могут сохраниться в подкаталог `canondata` рядом с тестом.

### Тесты с Санитайзером

Система позволяет собирать инструментированные программы с санитайзерами. 
Поддерживаются санитайзеры `AddressSanitizer`, `MemorySanitizer`, `ThreadSanitizer`, `UndefinedBehaviorSanitizer` и `LeakSanitizer`.

Запуск с санитайзером:
```
ya test -t --sanitize=address
```
Для правильной работы санитайзеров можно зафиксировать опции для конкретных тестов через макрос `ENV()`.

### Fuzzing

Фаззинг позволяет передавать приложению на вход неправильные, неожиданные или случайные данные.
Автоматический фаззинг поддерживается через `libFuzzer`.

Пример запуска фаззинга:
```
ya test -r --sanitize=address --sanitize-coverage=trace-div,trace-gep -A --fuzzing
```
Во время фаззинга записываются различные метрики, такие как размер корпуса, количество проверенных кейсов, пиковое потребление памяти и другие.

### Запуск произвольных программ (`Exec`-тесты)
Exec-тесты предоставляют возможность выполнять произвольные команды и проверять их успешное завершение, которое считается таковым при коде возврата 0. 
Эти тесты особенно полезны для проверки отдельных скриптов, командной строки или сложных сценариев, которые трудно реализовать с помощью стандартных тестовых фреймворков.
Описание таких тестов также осуществляется в файле `ya.make`.

### Типы тестов

**Типом теста** называется выполнение проверок с использованием одного конкретного инструмента, например, фреймворка `pytest` для `Python` или утилиты проверки форматирования кода `go fmt` для `Golang`. 
Полный список типов тестов приведен в таблице:

Тип | Описание
:--- | :---
`black` | Проверка форматирования кода на python3 утилитой black.
`classpath.clash` | Проверка наличия дублирующихся классов в classpath при компиляции Java проекта.
`eslint` | Проверка стиля и типичных ошибок кода на TypeScript с использованием утилиты ESLint.
`exectest` | Выполнение произвольной команды и проверка её кода возврата
`flake8.py2` | Проверка стиля кода на Python 2 c использованием утилиты Flake8
`flake8.py3` | Проверка стиля кода на Python 3 c использованием утилиты Flake8
`fuzz` | [Fuzzing](https://en.wikipedia.org/wiki/Fuzzing) тест
`g_benchmark` | Выполнение бенчмарков на C++ библиотекой [Google Benchmark](https://github.com/google/benchmark)
`go_bench` | Выполнение бенчмарков на Go утилитой `go bench`
`gofmt` | Проверка форматирования кода на Go утилитой `go fmt`
`go_test` | Выполнение тестов на Go утилитой `go test`
`govet` | Выполнение статического анализатора кода на Go утилитой `go vet`
`gtest` | Выполнение тестов на С++ с использованием фреймворка [Google Test](https://github.com/google/googletest/)
`java` | Выполнение тестов на Java с использованием фреймворка [JUnit](https://junit.org/)
`java.style` | Проверка форматирования кода на Java утилитой [checkstyle](https://checkstyle.org/)
`ktlint` | Проверка стиля Kotlin кода с использованием утилиты [ktlint](https://ktlint.github.io)
`py2test` | Тесты на Python 2 с использованием фреймворка [pytest](https://pytest.org/)
`py3test` | Тесты на Python 3 с использованием фреймворка [pytest](https://pytest.org/)
`pytest` | Тесты на Python любой версии с использованием фреймворка [pytest](https://pytest.org/)
`unittest`| Тесты на C++ с использованием фреймворка unittest.