# log_viewer

Локальный HTTP-сервер для просмотра, фильтрации и поиска по `log` файлам сборки `ya`.

`log_viewer` парсит лог, сохраняет нормализованные записи в SQLite и поднимает web UI.

## Сборка

```bash
cd handlers/analyze_make/log_viewer
ya make
```

## Запуск через ya

Основной способ запуска:

```bash
ya analyze_make pretty-log
```

По умолчанию откроется последний предыдущий `.log` из логов `ya` (`$YA_LOGS_ROOT`, иначе
`$YA_CACHE_DIR/logs`, иначе `~/.ya/logs`). Лог текущего запуска `pretty-log` при этом
игнорируется. Это правило применяется только когда `--log` не указан.

Можно явно передать один или несколько логов:

```bash
ya analyze_make pretty-log \
    --log ~/.ya/logs/2026-05-05/16-49-58.jxdy6l26ofikn75m.log \
    --log ~/.ya/logs/2026-05-05/17-00-01.abcd.log
```

`--port` указывать необязательно; если он не указан, используется `8765`.
Для нестандартного порта:

```bash
ya analyze_make pretty-log --port 8766
```

Официальные опции `pretty-log`:

| Опция | Что делает |
|-------|------------|
| `--log PATH` | Файл `.log`; можно указывать несколько раз. Если не указано — берётся последний предыдущий лог `ya`, исключая лог текущего запуска `pretty-log`. Если указано — используются ровно эти файлы. |
| `--port N` | Необязательный порт web-сервера; по умолчанию `8765`. |

После старта UI доступен на `http://127.0.0.1:<port>/`, API docs — на `/docs`.

## Standalone-запуск для разработки/debug

Для отладки самого `log_viewer` можно собрать и запустить standalone-бинарь:

```bash
./log_viewer \
    --log ~/.ya/logs/2026-05-05/16-49-58.jxdy6l26ofikn75m.log \
    --port 8765 (port is optional)
```

Полезные опции:

| Опция | Что делает |
|-------|------------|
| `--log PATH` | Файл `.log`; можно указывать несколько раз. |
| `--db-path PATH` | Явный путь к SQLite DB. По умолчанию используется persistent cache. |
| `--rebuild-db` | Не переиспользовать cache, пересобрать SQLite DB заново. |
| `--log-workers N` | Количество parser workers для `.log`; по умолчанию `1`. Сейчас лучше оставлять `1`: на больших логах увеличение workers обычно не ускоряет полный ingest и может увеличить время из-за IPC/слияния результатов/последовательной записи в SQLite. |
| `--benchmark-log PATH` | Только распарсить лог и вывести throughput, без запуска UI. |
| `--host`, `--port` | Адрес web-сервера; по умолчанию `127.0.0.1:8765`. |

Для обычного использования предпочитайте `ya analyze_make pretty-log`.

## Frontend: когда собирается и как разрабатывать

Frontend лежит в `ui/`, собранные production-файлы лежат в `static/`.

Важно: `ya analyze_make pretty-log` **не запускает** `npm`/`vite` и **не пересобирает**
frontend на старте. Официальный запуск отдаёт то, что уже лежит в `static/` и было
зашито в бинарь через `RESOURCE_FILES` при сборке `ya`.

### Официальный путь после правок UI

Если менялись файлы в `ui/src`:

```bash
cd handlers/analyze_make/log_viewer/ui
npm ci  # первый раз, если node_modules ещё нет
npm run build
```

`vite` соберёт frontend прямо в `../static` (`static/index.html`,
`static/assets/app.js`). После этого нужно пересобрать `ya`, чтобы новые static-файлы
попали в бинарь:

```bash
cd devtools/ya/bin
ya make
```

После пересборки проверяйте через пересобранный `ya-bin`:

```bash
Y_PYTHON_SOURCE_ROOT=/path/to/arcadia ./ya-bin analyze_make pretty-log
```

В релизном/транковом `ya analyze_make pretty-log` пользователь увидит только уже
закоммиченные и собранные `static/`-файлы.

### Debug-режим для разработки frontend

Для hot reload можно поднять backend на `8765`, а frontend — через Vite:

```bash
# terminal 1: backend/API
ya analyze_make pretty-log --port 8765

# terminal 2: frontend dev server
cd handlers/analyze_make/log_viewer/ui
npm ci  # первый раз, если node_modules ещё нет
npm run dev
```

Открывайте `http://127.0.0.1:5173/`. Vite проксирует `/api` на
`http://127.0.0.1:8765`, поэтому API берётся из запущенного backend, а UI
пересобирается Vite на лету.

Если hot reload не нужен, можно после каждого изменения запускать `npm run build` и
обновлять страницу backend (`http://127.0.0.1:8765/`). Standalone/source-запуск умеет
читать `static/` с файловой системы, но официальный бинарь использует ресурсы,
вшитые во время `ya make`.

## Cache и производительность

- Если `--db-path` не указан, база сохраняется в `$YA_CACHE_DIR/log_viewer` или `~/.ya/log_viewer`.
- При повторном запуске с теми же файлами логов база переиспользуется без повторного парсинга.
- Cache hit определяется по пути файла, размеру и `mtime_ns`.
- Cache-файлы `.sqlite`, `.sqlite-wal`, `.sqlite-shm` старше 14 дней удаляются при старте; активная база не удаляется и обновляет `mtime` при cache hit.
- При загрузке выводятся отдельные времена: `parse`, `SQLite insert`, `other`, а затем отдельно время построения индексов.
- `--log-workers > 1` включает multiprocessing parser, но текущая реализация не рекомендуется для обычного использования: workers возвращают распарсенные chunks в main process, а запись в SQLite и построение stages всё равно идут последовательно. На практике это часто даёт больший overhead, чем выигрыш.
- Индексы строятся после bulk ingest: B-tree по времени/level/module/thread и FTS5 по `raw`.
- FTS5 индексируется только по колонке `raw`; module/thread/level фильтруются отдельными SQL-фильтрами по B-tree индексам.

Пример вывода:

```text
Loading 1 file(s)...
  [log] log500.txt: 2157368 records, 500.42 MB in 6.70s (parse 1.19s; SQLite insert 2.22s; other 3.29s; 321823 rec/s, 74.65 MB/s)
SQLite: ~/.ya/log_viewer/<hash>.sqlite (indexes built in 9.96s)
Ready: http://127.0.0.1:8765/  (API docs at /docs)
```

## Как пользоваться UI

### Список логов

Основная таблица показывает:

- `time` — timestamp записи;
- `level`;
- `module`;
- `thread`;
- `line` — номер первой строки записи в исходном файле;
- `preview` — начало сообщения без стандартного log-префикса `time level module thread`.

`raw` при этом не теряется: полный текст записи, включая дату, уровень, модуль, поток и continuation-строки, доступен в Details.

### Фильтры

Верхняя панель фильтров:

- `Time` — диапазон времени. При старте выставляется диапазон от первой до последней записи с небольшим запасом, чтобы не потерять крайние логи.
- `Level`, `Module`, `Thread` — multi-select фильтры с операторами `=` и `!=`.
  - Можно сначала выбрать оператор, потом значение.
  - `Clear all` очищает выбранные значения.
- `Search` — полнотекстовый поиск по `raw` через FTS5. Крестик справа очищает запрос.
- `Reset filters` — сбрасывает фильтры и возвращает time range к диапазону всего лога.

Дополнительные удобства:

- Клик по `module` или `thread` в таблице включает/выключает соответствующий фильтр.
- Состояние фильтров и пагинации синхронизируется с URL.
- Пагинация фиксированная: `limit=100` в UI.

### Details

Клик по строке открывает Details:

- полный `raw` записи;
- `id`, файл, timestamp, level, module, thread;
- кликабельный `line` — переход на страницу, где находится эта запись, чтобы увидеть контекст до/после;
- список stage hierarchy, если запись находится внутри stage.

`raw` форматируется для читаемости: большие словари/списки внутри лога показываются структурированно, но исходный текст записи остаётся полным.

### Stages

`log_viewer` строит служебный индекс stage-диапазонов по stage-маркерам в логах.

- В UI показываются только stage, у которых есть и start, и finish.
- Неполные stage считаются обычными записями вне stage-фильтра.
- В Details отображается иерархия stage: outer → inner.
- Вложенность помечается `L1`, `L2`, ... — это уровень вложенности, а не уникальный номер stage. Поэтому `L6` может встречаться несколько раз у разных веток иерархии.
- Открытие stage показывает весь диапазон логов stage и выставляет time range от первого до последнего лога stage с небольшим запасом.
- Закрытие stage возвращает предыдущие фильтры.

## API

| Endpoint | Что делает |
|----------|------------|
| `GET /api/logs/overview` | Общие счётчики и метаданные загруженных логов: total, files, by_level, top_modules, top_threads, stage_count, first/last timestamp. |
| `GET /api/logs` | Пагинированный список логов с preview и фильтрами. |
| `GET /api/logs/{id}` | Details записи: полный `raw` и stage hierarchy для записи. |
| `GET /api/logs/{id}/position` | Позиция записи в полном порядке логов и offset страницы для перехода к контексту. |
| `GET /api/stages` | Список complete stages. |
| `GET /api/stages/{id}` | Один stage по id. |
| `GET /api/logs/filter-values` | Значения для фильтров `level`, `module`, `thread`. |
| `GET /docs` | Swagger UI. |

Параметры `GET /api/logs`:

| Параметр | Описание |
|----------|----------|
| `level=...` | Repeatable level-фильтр. |
| `level_op=eq|ne` | Оператор для level; по умолчанию `eq`. Имеет смысл только если есть `level`. |
| `module=...` | Repeatable module-фильтр. |
| `module_op=eq|ne` | Оператор для module; по умолчанию `eq`. |
| `thread=...` | Repeatable thread-фильтр. |
| `thread_op=eq|ne` | Оператор для thread; по умолчанию `eq`. |
| `q=text` | Полнотекстовый поиск по `raw` через SQLite FTS5. Соседние слова ищутся в указанном порядке. |
| `time_from=ISO` | Нижняя граница времени, например `2026-05-05T10:00:00`. |
| `time_to=ISO` | Верхняя граница времени. |
| `stage_id=N` | Показать записи внутри выбранного complete stage. |
| `offset=N`, `limit=N` | Пагинация; default `100`, max `500`. |

Параметры `GET /api/logs/filter-values`:

| Параметр | Описание |
|----------|----------|
| `field=level|module|thread` | Для какого фильтра загрузить значения. |
| `q=text` | Подстрока для поиска значения фильтра. |
| `limit=N` | Максимум значений; default `100`, max `500`. |

```

## Внутренности

- `cli.py` — аргументы командной строки, cache lifecycle, запуск сервера.
- `app.py` / `api.py` — FastAPI app и API-ручки.
- `ingest.py` — загрузка `.log` в SQLite, замеры parse/insert/throughput.
- `parsers/log.py` — single-process parser и экспериментальный multiprocessing parser; рекомендуемый режим — `--log-workers 1`.
- `store.py` — SQLite schema, ingest, query, stages, FTS5.
- `static.py` — отдача собранного web UI.
- `ui/` — React/Vite frontend; production build складывается в `static/`.
