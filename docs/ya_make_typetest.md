## Типы тестов

**Типом теста** называется выполнение проверок с использованием одного конкретного инструмента, например, фреймворка `pytest` для Python или утилиты проверки форматирования кода `go fmt` для Golang. Полный список типов тестов приведен в таблице:

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

