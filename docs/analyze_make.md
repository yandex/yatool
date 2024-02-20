# ya analyze-make timeline

Посмотреть профиль сборки.

`ya analyze-make timeline [options]` : что и сколько времени заняло при сборке

Каждый запуск ya make пишет eventlog в `~/.ya/evlogs/`. Команда `ya analyze-make timeline` команда строит трейс, в одном из двух форматов. По умолчанию он генерирует трейсы, которые умеет показывать Яндекс Браузер (через `browser://tracing`) и Chrome (через `chrome://tracing/`). Альтернативный формат совместим с maptplotlib.

По умолчанию берётся последний трейс из `~/.ya/evlogs/` на текущую дату, но можно подать файл явно.


## Опции
```
--evlog=ANALYZE_EVLOG_FILE   Анализировать лог из файла
--format=OUTPUT_FORMAT       Формат вывода, по умолчанию chromium_trace 
--plot                       Формат вывода plot (matplotlib)
-h, --help                   Справка
```
