# Описание дизайна yt_store

1. [Класс](https://a.yandex-team.ru/arcadia/yt/python/yt/wrapper/client_impl.py?rev=r13816498#L9), вокруг которого написана собственная реализация
2. YtClientProxy - Небольшая обвязка вокруг клиента. Данная обвязка нам нужна из-за необходимости иметь контроль вокруг логики ретраев.
   - Данный класс ожидает на вход два аргумента: `client` и `retry_policy`

# Пример использования:

```python
import yalibrary.store.yt_store.retries as retries

client = get_client_from_somewhere(...)
retry_policy = retries.RetryPolicy(max_retries=10)
my_proxy = retries.YtClientProxy(client, retry_policy)
my_proxy.some_method()
```
