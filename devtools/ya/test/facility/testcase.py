# coding: utf-8

import six


class TestCase(object):
    def __init__(
        self,
        name,
        status,
        comment="",
        elapsed=0.0,
        result=None,
        # TODO move to the suite level
        test_type=None,
        logs=None,
        # TODO move to the chunk level
        cwd=None,
        metrics=None,
        # TODO move to the suite level
        path=None,
        # TODO move to the suite level
        is_diff_test=False,
        started=None,
        # TODO move to the suite level
        tags=None,
    ):
        self.name = six.ensure_str(name)
        self.status = status
        self.elapsed = elapsed
        self.comment = comment
        self.result = result
        self.test_type = test_type
        if test_type:
            raise AssertionError(test_type)
        self.logs = logs or {}
        self.cwd = cwd
        self.metrics = metrics or {}
        self.path = path
        self.is_diff_test = is_diff_test
        self.started = started
        self.tags = tags or []

    def __eq__(self, other):
        if not isinstance(other, TestCase):
            return False
        return self.name == other.name and self.path == other.path

    def __str__(self):
        return self.name

    def __repr__(self):
        if self.path:
            path = " in {}".format(self.path)
        else:
            path = ""
        return "TestCase [{} - {}[{}]: {}] {}".format(self.name, self.status, self.elapsed, self.comment, path)

    def __hash__(self):
        return hash(str(self))

    def get_class_name(self):
        return self.name.rsplit("::", 1)[0]

    def get_test_case_name(self):
        return self.name.rsplit("::", 1)[1]
