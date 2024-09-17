import six

from devtools.ya.test import const

DEFAULT_CHUNK_NAME = 'sole chunk'


class Container(object):
    def __init__(self):
        self.logs = {}
        self.metrics = {}
        self._errors = []

    def add_error(self, msg, status=const.Status.FAIL):
        if isinstance(status, six.string_types):
            status = const.Status.BY_NAME[status]

        assert isinstance(msg, six.string_types), type(msg)
        if (status, msg) not in self._errors:
            self._errors.append((status, msg))

    def add_info(self, msg):
        assert isinstance(msg, six.string_types), type(msg)
        status = const.Status.GOOD
        if (status, msg) not in self._errors:
            self._errors.append((status, msg))

    def has_comment(self):
        return bool(self._errors)

    def get_comment(self):
        def get_prefix(s):
            return '[[warn]]Info:[[rst]] ' if s == const.Status.GOOD else ''

        data = "\n".join("{}{}".format(get_prefix(status), msg) for status, msg in self._errors)
        return data.strip()

    def get_errors(self):
        return [(s, m) for s, m in self._errors if s != const.Status.GOOD]

    def get_info(self):
        return [(s, m) for s, m in self._errors if s == const.Status.GOOD]

    def get_status(self):
        raise NotImplementedError()


class Chunk(Container):
    def __init__(self, nchunks=1, chunk_index=0, filename=''):
        super(Chunk, self).__init__()
        assert nchunks > 0, nchunks
        assert chunk_index >= 0, chunk_index
        self.nchunks = nchunks
        self.chunk_index = chunk_index
        self.filename = filename
        self.tests = []

    def get_name(self):
        return self.gen_chunk_name(self.nchunks, self.chunk_index, self.filename)

    @staticmethod
    def gen_chunk_name(nchunks=1, chunk_index=0, filename=None):
        assert nchunks > 0, nchunks
        assert chunk_index >= 0, chunk_index
        if filename:
            if nchunks == 1:
                return '[{}] chunk'.format(filename)
            return '[{} {}/{}] chunk'.format(filename, chunk_index, nchunks)
        if nchunks == 1:
            return DEFAULT_CHUNK_NAME
        return '[{}/{}] chunk'.format(chunk_index, nchunks)

    def get_status(self):
        # Chunk's status should not depend on tests statuses
        return get_container_status(self.get_errors(), [])

    def __str__(self):
        return repr(self)

    def __repr__(self):
        return self.get_name()

    def __hash__(self):
        return hash(str(self))


class Suite(Container):
    def __init__(self):
        super(Suite, self).__init__()
        self.chunks = []

    @property
    def tests(self):
        # return tuple to prevent modification of the temporary object
        return tuple(t for c in self.chunks for t in c.tests)

    @property
    def chunk(self):
        assert len(self.chunks) == 1, self.chunks
        return self.chunks[0]

    def register_chunk(self, nchunks=1, chunk_index=0, filename=''):
        assert len(self.chunks) == 0, self.chunks
        self.chunks.append(Chunk(nchunks, chunk_index, filename))
        return self.chunks[0]

    def get_status(self, relaxed=False):
        if relaxed:
            return get_container_status(self.get_errors(), [])
        return get_container_status(self.get_errors() + [e for c in self.chunks for e in c.get_errors()], self.tests)


def get_container_status(errors, entries):
    if not entries and not errors:
        return const.Status.GOOD

    # Don't take into account test cases if suite got errors
    status_map = {}
    if errors:
        for status, _ in errors:
            status_map[status] = True
    else:
        status_map = {x.status: True for x in entries}

    # consider skipped/xfail are good status
    # it's ok for suite to have only SKIPPED tests, for example there may by no tests for current platform
    for status in [const.Status.SKIPPED, const.Status.XFAIL, const.Status.XPASS, const.Status.DESELECTED]:
        if status in status_map:
            status_map[const.Status.GOOD] = True
            del status_map[status]

    if const.Status.INTERNAL in status_map:
        return const.Status.INTERNAL
    # Any FAIL in test cases should mark suite with FAIL status
    elif const.Status.FAIL in status_map:
        return const.Status.FAIL
    elif status_map == {const.Status.GOOD: True}:
        return const.Status.GOOD
    # NOT_LAUNCHED may occur for timedout tests when we have finished previous test
    # but had no time to start new one
    elif const.Status.NOT_LAUNCHED in status_map:
        return const.Status.TIMEOUT
    elif const.Status.TIMEOUT in status_map:
        return const.Status.TIMEOUT
    elif const.Status.FLAKY in status_map:
        return const.Status.FLAKY
    elif const.Status.MISSING in status_map:
        return const.Status.MISSING
    return const.Status.FAIL
