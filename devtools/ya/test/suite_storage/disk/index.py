"""Index mutable suite snapshots without retaining complete model payloads."""

import json
import os
import sqlite3
import tempfile

import devtools.ya.test.suite_storage.base.metadata as metadata

_FIELDS = ('uid', 'project_path', 'name', 'platform', 'size', 'ci_type', 'skipped', 'result_uids', 'output_uids')
_UPSERT = (
    'INSERT INTO suites VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) '
    'ON CONFLICT(suite_id) DO UPDATE SET '
    + ', '.join(f'{field} = excluded.{field}' for field in _FIELDS)
    + ' WHERE ('
    + ', '.join('suites.' + field for field in _FIELDS)
    + ') IS NOT ('
    + ', '.join('excluded.' + field for field in _FIELDS)
    + ')'
)


class SQLiteIndex:
    """Access stable-ID keyed rows under the owning storage state lock.

    Reuse the immutable CI index's bounded page cache and temporary database,
    with journaling so a rejected mutable snapshot can retain its previous row.
    """

    def __init__(self, directory: str | None = None) -> None:
        self._directory = tempfile.TemporaryDirectory(prefix='suite-index-', dir=directory)
        self._closed = False
        connection = None
        try:
            connection = sqlite3.connect(os.path.join(self._directory.name, 'metadata.sqlite'), check_same_thread=False)
            self._db = connection
            for pragma in (
                'journal_mode=MEMORY',
                'synchronous=OFF',
                'cache_size=-1024',
                'temp_store=FILE',
                'mmap_size=0',
            ):
                connection.execute('PRAGMA ' + pragma)
            connection.execute(
                'CREATE TABLE suites (suite_id INTEGER PRIMARY KEY, uid TEXT, '
                'project_path TEXT, name TEXT, platform TEXT, size TEXT, ci_type TEXT, '
                'skipped INTEGER, result_uids TEXT, output_uids TEXT)'
            )
        except BaseException:
            if connection is not None:
                connection.close()
            self._directory.cleanup()
            raise

    def get(self, suite_id: int) -> metadata.SuiteInfo | None:
        row = self._db.execute(
            'SELECT uid, project_path, name, platform, size, ci_type, skipped, result_uids, output_uids '
            'FROM suites WHERE suite_id = ?',
            (suite_id,),
        ).fetchone()
        if row is None:
            return None
        return metadata.SuiteInfo(*row[:6], bool(row[6]), tuple(json.loads(row[7])), tuple(json.loads(row[8])))

    def put(self, suite_id: int, info: metadata.SuiteInfo) -> None:
        # Trace/result edits usually leave routing and reporting metadata intact.
        # Do not dirty SQL pages for those edits; keep the equality check in SQL
        # rather than retaining another heap copy of every suite's metadata.
        with self._db:
            self._db.execute(
                _UPSERT,
                (suite_id, *info[:7], json.dumps(info.result_uids), json.dumps(info.output_uids)),
            )

    def delete(self, suite_id: int) -> None:
        with self._db:
            self._db.execute('DELETE FROM suites WHERE suite_id = ?', (suite_id,))

    def close(self) -> None:
        if not self._closed:
            self._db.close()
            self._closed = True
        self._directory.cleanup()
