MAKELIST_FILENAME_PREFERRED = 'ya.make'
MAKELIST_FILENAMES = ('CMakeLists.txt', 'ya.make')


def parse_make_files_dart(dart_content):
    def to_stream(data):
        dart_entry = {}
        for line in data:
            if line.startswith('==='):
                if dart_entry:
                    yield dart_entry
                    dart_entry = {}
            elif line:
                key, value = line.split(': ', 1)
                dart_entry[key.strip()] = value.strip()
        if dart_entry:
            yield dart_entry

    result = list(to_stream(dart_content))
    return result


def union_make_files(mf1, mf2):
    return mf1 + mf2
