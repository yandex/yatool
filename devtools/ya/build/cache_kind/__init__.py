import enum


class CacheKind(enum.StrEnum):
    parser = 'parser'
    parser_json = 'parser_json'
    parser_deps_json = 'parser_deps_json'

    @staticmethod
    def get_ymake_option(kind):
        if not isinstance(kind, CacheKind):
            kind = CacheKind(kind)
        return CacheKind._ymake_option_by_kind[kind]

    def get_ya_make_option(self):
        return '-x' + CacheKind.get_ymake_option(self)


CacheKind._ymake_option_by_kind = {
    CacheKind.parser: 'CC=f:r,d:n,j:n',
    CacheKind.parser_json: 'CC=f:r,d:n,j:r',
    CacheKind.parser_deps_json: 'CC=f:r,d:r,j:r',
}
# All cache kinds must have the appropriate options
assert {k for k in CacheKind._ymake_option_by_kind.keys()} == {k for k in CacheKind}

DEFAULT_CACHE_KIND = CacheKind.parser
