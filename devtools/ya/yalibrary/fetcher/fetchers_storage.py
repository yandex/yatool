class FetchersStorage(object):
    def __init__(self):
        self._fetchers = []
        self._default_fetcher_instance = None

    def register(self, schemas, fetcher_instance, default=False):
        self._fetchers.append((schemas, fetcher_instance))
        if default:
            self._default_fetcher_instance = fetcher_instance

    def get_by_type(self, schema):
        for schemas, instance in self._fetchers:
            if schema in schemas:
                return instance
        return None

    def get_default(self):
        return self._default_fetcher_instance

    def accepted_schemas(self):
        res = set()
        for schemas, _ in self._fetchers:
            res |= set(schemas)
        return res
