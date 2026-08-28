# agent_ui — поток событий для кодинг-агентов

`AgentConsole` — одна на команду, владеет выходным потоком и единственным потоком-писателем.
Report-записи попадают в неё напрямую через `_BuildSink` — in-process участника списка отчётов
`ReportGenerator`; ни временных файлов, ни `JsonLineReport` в цепочке нет. `projection.py`
превращает записи в компактные события, каждое из которых пишется отдельной JSONL-строкой
с фиксированным началом `type`, `ts`.

Включается опцией `--agent-output` или автоматически, когда `app/modules/caller_info`
определил запуск из-под агента — подробности решения см. в
[docs/agents/YALIBRARY.md](../../docs/agents/YALIBRARY.md).

## Вердикт прогона в событии `summary`

`classify.py` называет исход прогона, чтобы агент понимал следующий шаг. Событие `summary`
несёт, помимо `exit_code`, пару полей `category` (как называется исход) и `action` (что с ним
делать). Код выхода здесь не выводится второй раз: `ya` выбирает его один раз — в
`configure_exit_code_definition` (`app/__init__.py`) для прогона, умершего от исключения, и в
`YaMake._calc_exit_code` для собранного. `category` — имя, которое коду даёт
`core/error.ExitCodes`.

| код | `category` | `action` |
|---|---|---|
| 1 | `generic_error` | `fix_code` |
| 3 | `unhandled_exception` | `report` |
| 4 | `usage_error` | `fix_command` |
| 8 | `configure_error` | `fix_makefile` |
| 9 | `no_tests_collected` | `fix_command` |
| 10 | `test_failed` | `fix_code` |
| 12 | `infrastructure_error` | `rerun_as_is` |
| 13 | `not_retriable_error` | `report` |
| 14 | `yt_store_fetch_error` | `rerun_as_is` |
| прочий ненулевой | `generic_error` | `fix_code` |
| 0 или отсутствует | вердикта нет | — |

### Почему `configure_error` — исключение

`configure_error` — единственная категория, которая выводится **не из кода выхода, а из
факта**: собрал ли прогон configure-ошибки. Пока `ignore_configure_errors` по умолчанию
включён (см. [YA-1456](https://st.yandex-team.ru/YA-1456) и `_calc_exit_code` в
`build/ya_make.py`), код 8 не приезжает вовсе — прогон завершается с 1 без `--keep-going`
и с 0 с ним. Факт консоль узнаёт двумя путями:

- **жёсткий** — конфигурация умерла до открытия build-фрейма, ошибки лежат в буфере
  `buffer_configure_error`;
- **мягкий** (`--keep-going`) — сборка продолжается, а configure-ошибки доезжают до
  `_BuildSink` обычными записями отчёта (`projection.is_configure_failure`).

Поэтому совет агенту одинаков до и после переключения дефолта — меняется только `exit_code`,
— и `--keep-going`-прогон с битым `ya.make` перестаёт выглядеть зелёным.

Битая конфигурация перебивает остальные категории: тест, упавший под ней, мог упасть из-за
неё, поэтому чинить сначала нужно `ya.make`. Тот же порядок берёт `_calc_exit_code`,
возвращая 8 при одновременных configure- и test-ошибках.
