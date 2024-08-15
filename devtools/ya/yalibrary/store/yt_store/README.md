# Описание дизайна yt_store

1. [Класс](https://a.yandex-team.ru/arcadia/yt/python/yt/wrapper/client_impl.py?rev=r13816498#L9), вокруг которого написана собственная реализация
2. YtClientProxy - Небольшая обвязка вокруг клиента. Данная обвязка нам нужна из-за необходимости иметь контроль вокруг логики ретраев.
   - Данный класс ожидает на вход два аргумента: `client` и `retryer`
   - Все кастомные прокси обязаны наследоваться от YtClientProxy (если вам это не подходит, значит пора переделывать дизайн)
   - Имеет метод `handle_retry`, который вызывает метод `handle` у ретраера. Такое хочется иметь, чтобы была возможность сделать что-то до и после ретрая. <br />
   Да, это можно сделать и в ретраере, если нам не нужно знать о чем либо, НО, если нам надо, то хочется просто сделать так, как будет показано в примере
3. Ретраер - Можно сказать, что это хендлер, который обязан наследоваться от `BaseOnRetryHandler` <br />
Необходимо реализовать метод `handle`, который, в свою очередь, имеет два аргумента: `err` (ошибка из [клиента](https://a.yandex-team.ru/arcadia/yt/python/yt/wrapper/client_impl.py?rev=r13816498#L9)) и `attempt` (номер попытки)

# Пример кастомизации:

С Кастомным прокси
```python
import yalibrary.store.yt_store.proxy as proxy
import yalibrary.store.yt_store.configuration as yt_configuration

class MyCustomYtProxy(proxy.YtClientProxy):
    def __init__(self, client, retryer, *args, **kwargs):
        # do whatever you want with args and kwargs
        super().__init__(client, retryer)
        self._num_of_total_retries = 0
        self._num_of_successful_retries = 0

    @property
    def num_of_bad_retries(self):
        return self._num_of_total_retries - self._num_of_successful_retries

    def handle_retry(self, err, attempt):
        self._num_of_total_retries += 1
        super().handle_retry(err, attempt)
        # it's not guaranteed that we reach here 'cause an error could occur
        self._num_of_successful_retries += 1


class CustomRetryer(yt_configuration.BaseOnRetryHandler):
    def handle(self, err, attempt):
        if attempt == 10:
            self._send_report(attempt)
            raise

        print(f"attempt number {attempt}")

        def _send_report(self, *args, **kwargs):
            ...


client = get_client_from_somewhere(...)
retryer = CustomRetryer()
my_proxy = MyCustomYtProxy(client, retryer)
my_proxy.some_method()
```

Без кастомного прокси, когда нам не нужно знать что-либо о состоянии прокси
```python
import yalibrary.store.yt_store.proxy as proxy
import yalibrary.store.yt_store.configuration as yt_configuration

class CustomRetryer(yt_configuration.BaseOnRetryHandler):
    def handle(self, err, attempt):
        if attempt == 10:
            self._send_report(attempt)
            raise
        print(f"attempt number {attempt}")

        def _send_report(self, *args, **kwargs):
            ...


client = get_client_from_somewhere(...)
retryer = CustomRetryer()
my_proxy = proxy.YtClientProxy(client, retryer)
my_proxy.some_method()
```

# Пара слов в заключение

Данная реализация заточена под кастомные ретраи <br />
Если в будущем Вам будет необходимо сделать что-то, например, "до" и "после" вызова метода [клиента](https://a.yandex-team.ru/arcadia/yt/python/yt/wrapper/client_impl.py?rev=r13816498#L9),
то стоит пересмотреть реализацию, например, вместо Ретраера, сделать что-то вроде общего Хендлера

Или, например, когда нужно будет сделать "что-то" при получении результата, то можно будет расширить текущий функционал, добавил колбэк в хендлер
