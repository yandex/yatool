# asio-grpc для Larry

Импорт [Tradias/asio-grpc](https://github.com/Tradias/asio-grpc), релиз **3.7.0**, commit `57069c53f9c5877153da29a94c28f2138a77dbf1`, Apache-2.0. Источник и SHA-256 архива — в [UPSTREAM.json](UPSTREAM.json), лицензия — [LICENSE](LICENSE), исходное руководство — [README.upstream.md](README.upstream.md).

Включены все 135 upstream headers из src/agrpc. CMake, examples, upstream tests/dependencies и опциональный C++ module не импортированы. Используется backend Boost.Asio, экспортируемый зависимостью:

```yamake
PEERDIR(devtools/ya_make/contrib/asio-grpc)
```

```cpp
#include <agrpc/asio_grpc.hpp>
```

Цель экспортирует include path и `AGRPC_BOOST_ASIO`, использует уже имеющиеся `contrib/libs/grpc` и `contrib/restricted/boost/asio`. Не подключайте одновременно standalone Asio, unifex или stdexec backend.

## Локальные адаптации

[patches/arcadia.patch](patches/arcadia.patch) содержит полный diff относительно закреплённого upstream:

- `// Y_IGNORE` у include standalone Asio, unifex, stdexec и optional Boost.Cobalt. Ymake сканирует также неактивные ветки препроцессора; это позволяет не добавлять неиспользуемые зависимости. Комментарии не меняют обработку C++ компилятором.
- Явное преобразование std::string → TString на трёх границах generic CompletionQueue client API. Аркадийный gRPC 1.61.3 использует TString; публичный API asio-grpc сохраняет std::string.

Для воспроизведения импорта скачать архив из UPSTREAM.json, проверить его SHA-256, скопировать указанные файлы и применить `patch -p1 < patches/arcadia.patch` в корне копии библиотеки. Не форматировать upstream headers вместе с локальным кодом. При обновлении заново проверить патч и совместимость.

## Граница ответственности

GrpcContext владеет CompletionQueue и требует явного pumping. Запуск существующего Asio io_context сам по себе не обслуживает эту очередь. При интеграции выбрать способ исполнения обоих contexts, явно управлять executor/strand и завершением операций. `stop()` контекста не является заменой отмены RPC и ожидания callbacks. Сервер, RPC context, сообщения и session state должны жить до завершения использующих их операций; shutdown должен включать cancel и drain/join.

Эта библиотека адаптирует async API gRPC. Она не реализует импорт унаследованного Windows SOCKET, socketpair, управление процессом или прикладной ACK владения результатами.

## Проверка и границы поддержки

Из devtools/ya_make:

```sh
ya make contrib/asio-grpc/ut -t --test-stderr
```

Локально проверены на Linux с vendored gRPC 1.61.3 и Boost.Asio 1.32.0:

- компиляция публичного umbrella header;
- C++20 coroutine через Asio completion token, завершённая событием CompletionQueue;
- отмена pending alarm через Asio cancellation slot и доставка завершения до уничтожения объектов.

Эти проверки не подтверждают все шаблонные RPC API. Реальная bidi-сессия, переходы между Larry executors и порядок server shutdown проверяются при внедрении протокола. Optional health-check adapter и отдельный callback/reactor backend не входят в проверенную поверхность; для них могут потребоваться дополнительные зависимости или адаптации vendored gRPC. Windows build/runtime и native Windows CI этими проверками не подтверждены.

В двух upstream-заголовках добавлен завершающий перевод строки для обязательной проверки Arc; изменение включено в `patches/arcadia.patch`.
