"""Stream trusted context records into mutable disk storage and its index."""

import contextlib
import tempfile
import typing as tp

import devtools.ya.test.suite_storage.context as context
import devtools.ya.test.suite_storage.disk as disk
import devtools.ya.test.suite_storage.suites as suites


class SuiteCollector:
    """Own loading until complete, then transfer ordered selections to the context.

    Reuse the SAX callback contract and the existing mutable backend. Index one
    restored descriptor at a time, instead of retaining all JSON pickle strings
    and then doing a separate index-building pass at report initialization.
    """

    def __init__(self, directory: str | None = None) -> None:
        self._resources = contextlib.ExitStack()
        self._closed = self._finished = self._failed = False
        self._references: dict[tuple[str, ...], dict[str, suites.SuiteRef]] = {path: {} for path in context.SUITE_PATHS}
        try:
            self.directory = self._resources.enter_context(
                tempfile.TemporaryDirectory(prefix='mutable-suite-context-', dir=directory)
            )
            self.tests = suites.Suites(disk.DiskStorage(self.directory))
            self._resources.callback(self.tests.close)
            self.stripped_tests = suites.Suites(disk.DiskStorage(self.directory))
            self._resources.callback(self.stripped_tests.close)
            self._collections = dict(zip(context.SUITE_PATHS, (self.tests, self.stripped_tests)))
        except BaseException:
            self.close()
            raise

    def _check_loading(self) -> None:
        if self._closed:
            raise ValueError('Suite collector is closed')
        if self._failed:
            raise RuntimeError('Suite collection failed; close the owner')
        if self._finished:
            raise RuntimeError('Suite collection is finished')

    def consume(self, path: tuple[str, ...], uid: str, serialized_suite: str) -> None:
        self._check_loading()
        try:
            selection = self._collections[path]
            payload = serialized_suite.encode('latin-1')
            ref = selection.register_serialized(uid, payload)
            selection.metadata_for(ref, serialized_payload=payload)
            # Match dict loading: duplicate keys replace the value but retain
            # the first insertion position. Dropping the old ref releases its ID.
            self._references[path][uid] = ref
        except BaseException:
            self._failed = True
            raise

    def finish_loading(self) -> None:
        if self._finished and not self._closed:
            return
        self._check_loading()
        self.tests = self.tests.select_refs(self._references[('tests',)].values())
        self.stripped_tests = self.stripped_tests.select_refs(self._references[('stripped_tests',)].values())
        self._references.clear()
        self._finished = True

    def close(self) -> None:
        if not self._closed:
            self._closed = True
            try:
                self._resources.close()
            finally:
                self._references.clear()

    def __enter__(self) -> tp.Self:
        self._check_loading()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
