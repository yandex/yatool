## Контекстные файлы в тестах

Для корректного выполнения некоторых тестов необходимо взаимодействие с тестовой средой. 

Для каждого из поддерживаемых языков (Go, Python, C++, Java) у нас есть библиотеки, которые предоставляют информацию об окружении.

Эти библиотеки получают информацию из контекстного файла, формируемого тестовой инфраструктурой перед запуском теста. 

Путь к контекстному файлу указан в переменной окружения `YA_TEST_CONTEXT_FILE`.

### Формат файла

Контекстный файл — это `JSON`-документ, который содержит информацию, разделенную на четыре секции:

- build: Информация о типе сборки, флагах и санитайзерах.
- resources: Сведения о глобальных ресурсах, доступных во время выполнения теста, таких как инструментарий.
- runtime: Данные о тестовой среде, включая пути к корню проекта, рабочей директории и другим важным местам.
- internal: Техническая информация, необходимая для работы тестовой системы. Эта секция не должна использоваться в тестах.

Пример контекстного файла:


{
  “build”: {
    “build_type”: “debug”,
    “flags”: {
      “TESTS_REQUESTED”: “yes”
    }
  },
  “internal”: {
    “core_search_file”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/devtools/dummy/test/simple_pytest_test/test-results/pytest/core_search.txt”,
    “env_file”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/devtools/dummy/test/simple_pytest_test/test-results/pytest/env.json.txt”,
    “trace_file”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/devtools/dummy/test/simple_pytest_test/test-results/pytest/ytest.report.trace”
  },
  “resources”: {
    “global”: {
      “EXTERNAL_YMAKE_PYTHON3_RESOURCE_GLOBAL”: “/home/iaz1607/.ya/tools/v4/3046144506”,
      “LLD_ROOT_RESOURCE_GLOBAL”: “/home/iaz1607/.ya/tools/v4/2283360772”,
      “OS_SDK_ROOT_RESOURCE_GLOBAL”: “/home/iaz1607/.ya/tools/v4/1966560555”,
      “YMAKE_PYTHON3_RESOURCE_GLOBAL”: “/home/iaz1607/.ya/tools/v4/3046144506”
    }
  },
  “runtime”: {
    “atd_root”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/environment/arcadia_tests_data”,
    “build_root”: “/home/iaz1607/.ya/build/build_root/5c1v/000330”,
    “gdb_bin”: “/home/iaz1607/.ya/tools/v4/3238884116/gdb/bin/gdb”,
    “output_path”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/devtools/dummy/test/simple_pytest_test/test-results/pytest/testing_out_stuff”,
    “project_path”: “devtools/dummy/test/simple_pytest_test”,
    “python_bin”: “/home/iaz1607/.ya/tools/v4/2989598506/python”,
    “source_root”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/environment/arcadia”,
    “test_tool_path”: “/home/iaz1607/.ya/tools/v4/3322334308/test_tool”,
    “work_path”: “/home/iaz1607/.ya/build/build_root/5c1v/000330/devtools/dummy/test/simple_pytest_test/test-results/pytest”
  }
}

#### Генерация контекстного файла

Чтобы сгенерировать контекстный файл, достаточно запустить команду `ya make -A --test-prepare`. 

При завершении её выполнения контекстный файл будет находиться в директории `test-results/<тип теста>/test.context`.

#### Запуск тестов из бинарного файла

После выполнения команды `ya make -A --test-prepare` в директории с тестом появится тестовый бинарник. 

Обычно этот бинарник запускается тестовой машинерией, которая передает ему необходимые параметры и путь к контекстному файлу.

Однако при необходимости это можно сделать вручную.
