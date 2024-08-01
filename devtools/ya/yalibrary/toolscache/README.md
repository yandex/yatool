# Набор заметок про взаимодействие с ya-tc

Общее описание кэширования есть в [документации](https://docs.yandex-team.ru/ya-make/general/how_it_works#build_caches).

В приложение ya-tc встроен код для обслуживания двух типов кэшей: тулового (tc) и сборочного (ac). То, какой кэш будет обслуживаться конкретным экземпляром приложения ya-tc, зависит от его параметров запуска. В yt-tc реализована сложная логика по обеспечению уникальности экземпляров процессов для каждого из типов кэшей (psingleton). Это необходимо в случае нескольких сборок, выполняющихся параллельно, каждая из которых пытается запустить свой экземпляр ya-tc. Процесс ya-tc после запуска "демонизируется", чтобы уметь переживать сборку, которая его породила. Это даёт возможность чистить кэши от устаревших данных в фоне и не тормозить сборочный процесс. Через некоторый таймаут бездействия ya-tc автоматически завершается.

Запущенный процесс ya-tc слушает на случайном генерируемом в момент запуска unix-сокете, координаты которого записываются в lock-файл. Клиент, чтобы узнать координаты этого сокета, замысловатым образом получают его через функцию
`get_server_info()` модуля `devtools.local_cache.psingleton.python.systemptr`.

Почему всё это сделано так сложно, а не через тривиальный лок-файл и unix-сокет, имена которых фиксированы и заранее известны всем сторонам обмена (клиенту и серверу), история умалчивает.

Запуск и перезапуск ya-tc полностью лежит на плечах клиента. Таким "продвинутым" клиентом является синглтон [`_SERVER`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L710), являющийся экзепляром класса [`_Server`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L102). Идея состоит в том, чтобы клиент мог максимально переживать неожиданные остановки ya-tc (а с учётом упомянутого ранее таймаута это вполне штатная ситуация), поэтому все обращения к ya-tc в клиенте завёрнуты в retry и между попытками (в зависимости от кода GRPC-ошибки) делается как повторное получение нового адреса ya-tc, так и его перезапуск, если понадобится (см. методы класса `_ac_cache_address` и `_tc_cache_address`). Класс `_Server` "прячет" от остального кода все сложности по обеспечению работоспособности ya-tc и только в случае неустранимой ошибки эскалирует проблему наверх.
В качестве протокола обмена между ya-tc и клиентом выбран GRPC. Низкоуровневые подробности обмена инкапсулированы в python-клиентах (`devtools/local_cache/ac/python/ac.pyx` - для AC, `devtools/local_cache/toolscache/python/toolscache.pyx` - для tools cache и `devtools/local_cache/psingleton/python/systemptr.pyx` - для общей логики вокруг psingleton). Так как адрес ya-tc может меняться в процессе работы, то в этих клиентах есть логика, реагирующая на смену адреса и пересоздающая служебные GRPC-объекты.

Цепочка прохождения запроса к туловому кэшу:
- одна из функций модуля, например, [`notify_tool_cache`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L731)
- метод в `_Server`, например, [`_Server.notify_tool_cache`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L575)
- [`_Server.unary_method`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L565), задача которого завернуть последующий вызов в retry с потенциальным перезапуском ya-tc при ошибке
- `_Server._tc_unary_method_no_retry` получает адрес сервера и делает попытку вызвать GRPC-клиента. Так как ya-tc, являясь тулом, тоже хранится в туловом кэше, то в этом методе предусмотрено подавление ошибки, когда ya-tc пытается зарегистрировать в кэше себя.
- один из методов [`toolscache.pyx`](https://a.yandex-team.ru/arcadia/devtools/local_cache/toolscache/python/toolscache.pyx)

Сборочный кэш (AC) предоставляет унифицированное для всех типов сборочных кэшей API, реализованное в классе [`ACCache`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L803). Цепочка выглядит так:
- `ACCache`
- метод `_Server`, например, [`_Server.has_uid`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L627)
- [`_Server.unary_method`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L565), задача которого завернуть последующий вызов в retry с потенциальным перезапуском ya-tc при ошибке
- [`_Server._ac_unary_method_no_retry`](https://a.yandex-team.ru/arcadia/devtools/ya/yalibrary/toolscache/__init__.py?rev=r14545317#L515) получает адрес сервера и делает попытку вызвать GRPC-клиента.
- один из методов [`ac.pyx`](https://a.yandex-team.ru/arcadia/devtools/local_cache/ac/python/ac.pyx)

# Дополнительные ссылки
Анонсы:
- https://clubs.at.yandex-team.ru/arcadia/21079
- https://clubs.at.yandex-team.ru/arcadia/21771

Ещё полезные ссылки:
- [design proposal](https://a.yandex-team.ru/arcadia/devtools/proposals/designs/2019-06-14-local_cache.md)
- [перенос на Windows](https://wiki.yandex-team.ru/users/dvshkurko/perenos-na-windows/)