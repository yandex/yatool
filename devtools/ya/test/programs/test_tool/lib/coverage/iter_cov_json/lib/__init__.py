from iter_cov_json import _iter_cov_json


# XXX quickfix to prevent memory exhaustion
# TODO rewrite resolve_clang_coverage node using library/python/json
# we proceed from the assumption that json is valid and filenames (the only field with type string)
# doesn't contain spaces, tabs and []{}",: symbols
def iter_coverage_json(afile):
    read_size = 256 * 1024

    _iter_cov_json.init_state()

    while True:
        data = afile.read(read_size)
        if not data:
            break

        for path, block in _iter_cov_json.iter_cov(data):
            yield (path, block)

    yield (None, _iter_cov_json.get_tail())
    return
