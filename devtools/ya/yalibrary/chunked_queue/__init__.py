__all__ = ['ChunkedQueue']

import os
import time
import logging
import threading

from pytz import UTC
import datetime as dt
import exts.yjson as json

from exts import fs, filelock, uniq_id

try:
    from pathlib import Path
except ImportError:
    from pathlib2 import Path

import typing as tp

logger = logging.getLogger(__name__)


ConsumerType = tp.Callable[[tp.List[tp.Any]], None]


def uniq_name():
    return "{}_{}".format(dt.datetime.now(tz=UTC).strftime('%Y_%m_%d_%H_%M_%S'), uniq_id.gen8())


class FileChunk(object):
    UNKNOWN = None

    def __init__(self, data_path, lock_path, name):
        # type: (Path, Path, str) -> None
        self.name = name

        self._data_path = data_path / name
        self._lock_path = lock_path / name

        self._stream = None
        self._exclusive_chunk_access_lock = filelock.FileLock(str(self._lock_path))

        self._file_operation_lock = threading.Lock()

        self._count = FileChunk.UNKNOWN  # type: None | int

    def acquire(self, blocking=True):
        # type: (bool) -> bool
        return self._exclusive_chunk_access_lock.acquire(blocking=blocking)

    def release(self):
        # type: () -> None
        return self._exclusive_chunk_access_lock.release()

    def touch(self):
        self._data_path.touch()

    def add(self, dct):
        # type: (dict) -> None
        with self._file_operation_lock:
            if not self._stream:
                self._count = 0
                self._stream = self.__open_stream(str(self._data_path))

            self._stream.write(json.dumps(dct) + '\n')
            self._stream.flush()

            if self._count is None:
                self._count = 0

            self._count += 1

    def read(self):
        with self._file_operation_lock:
            self._raw_close()

            items = [json.loads(x) for x in self._data_path.read_text().splitlines()]

            self._count = len(items)

            return items

    def exists(self):
        return self._data_path.exists() and self._lock_path.exists()

    def _raw_close(self):
        if self._stream:
            self._stream.close()
            self._stream = None

    def close(self):
        with self._file_operation_lock:
            self._raw_close()

    def __eq__(self, value):
        if not isinstance(value, FileChunk):
            return False

        return self._data_path == value._data_path and self._lock_path == value._lock_path

    def clear_and_release(self):
        try:
            self._data_path.unlink()
        except OSError:
            logger.debug("While removing data file", exc_info=True)

        try:
            self.release()
            self._lock_path.unlink()
        except OSError:
            logger.debug("While removing lock file", exc_info=True)

    @staticmethod
    def __open_stream(data_path):
        if os.path.exists(data_path):
            os.remove(data_path)
        f = open(data_path, 'w')
        os.chmod(data_path, 0o600)
        return f

    def __repr__(self):
        return "<{} with {} items for {}>".format(
            type(self).__name__, self._count if self._count is not None else "?", self.name
        )

    @property
    def items_count(self):
        # type: () -> int | None
        """This value didn't show real values when we reopen already existing file WITHOUT reading it first"""
        return self._count


class StopConsume(Exception):
    pass


class BaseQueue(object):
    def __init__(self):
        self._active_chunk_items = 0

    def consume(self, consumer):
        # type: (ConsumerType) -> bool
        raise NotImplementedError()

    def continuous_consume(self, consumer):
        # type: (ConsumerType, threading.Event) -> None
        raise NotImplementedError()

    def stop(self):
        raise NotImplementedError()


class UrgentQueue(BaseQueue):
    _STEP_S = 1

    def __init__(self, backup_queue):
        # type: (BaseQueue) -> None
        self.logger = logging.getLogger("UrgentQueue")

        self._chunk_lock = threading.Lock()
        self._chunk = []  # protected by self._lock
        self._condition = threading.Condition()
        self._work = None

        self.backup_queue = backup_queue

    def cleanup(self):
        with self._chunk_lock:
            self._chunk = []

    def add(self, dct):
        with self._chunk_lock:
            self._chunk.append(dct)

        with self._condition:
            self._condition.notify_all()

    def release(self):
        with self._chunk_lock:
            chunk = self._chunk
            self._chunk = []
        return chunk

    def consume(self, consumer):
        # type: (ConsumerType) -> bool
        urgent_chunk = []
        try:
            urgent_chunk = self.release()
            if urgent_chunk:
                consumer(urgent_chunk)
        except (Exception, StopConsume) as e:
            if not isinstance(e, StopConsume):
                self.logger.debug("While consume urgent chunk", exc_info=True)

            # return events to main queue
            self.backup_queue.add(urgent_chunk)

            if isinstance(e, StopConsume):
                self.logger.debug("Consumer asks to stop")
                return False

        return True

    def continuous_consume(self, consumer, first_check=True):
        # type: (ConsumerType, threading.Event, bool) -> None
        with self._condition:
            if self._work is False:
                self.logger.debug("Queue was stopped, will be rerun")

            if self._work is False:
                self.logger.debug("Queue already works")
                return

            self._work = True

        if first_check:
            if not self.consume(consumer):
                return

        while True:
            with self._condition:
                self._condition.wait(timeout=self._STEP_S)

            if not self.consume(consumer):
                break

            with self._condition:
                if self._work is False:
                    return

    def stop(self):
        with self._condition:
            if self._work is None:
                self.logger.debug("Queue not running")
                return

            if self._work is False:
                self.logger.debug("Queue was already stopped")
                return

            if self._work is True:
                self._work = False
                self._condition.notify_all()

    def __repr__(self):
        return "<{} with {} items>".format(type(self).__name__, len(self._chunk))


class ChunkedQueue(BaseQueue):
    def __init__(self, store_dir):
        # type: (Path) -> None

        self.logger = logging.getLogger("ChunkedQueue")

        store_dir = Path(store_dir)

        self._data_dir = store_dir / 'data'
        self._locks_dir = store_dir / 'locks'

        self._data_dir.mkdir(parents=True, exist_ok=True)
        self._locks_dir.mkdir(parents=True, exist_ok=True)

        self._active_chunk_value_lock = threading.Lock()

        with self._active_chunk_value_lock:
            self._active_chunk = self._generate_new_chunk()  # type: FileChunk

        self._condition = threading.Condition()
        self._work = None

    @property
    def _active_chunk_items(self):
        # type: () -> int
        return (self._active_chunk.items_count or 0) if self._active_chunk else 0

    def _generate_new_chunk(self):
        # type: () -> FileChunk
        name = uniq_name()
        chunk = FileChunk(self._data_dir, self._locks_dir, name)
        chunk.acquire()
        chunk.touch()
        logger.debug("New active chunk: %s", chunk)
        return chunk

    def cleanup(self, max_items=None):
        data_lst = os.listdir(self._data_dir)
        lock_lst = os.listdir(self._locks_dir)
        dangling_locks = set(lock_lst) - set(data_lst)

        for x in dangling_locks:
            self._try_remove(os.path.join(self._locks_dir, x))

        if max_items is not None:
            for x in sorted(data_lst)[:-max_items]:
                self._try_remove(os.path.join(self._data_dir, x))
                self._try_remove(os.path.join(self._locks_dir, x))

    def add(self, dct):
        with self._active_chunk_value_lock:
            self._active_chunk.add(dct)

        with self._condition:
            self._condition.notify_all()

    def consume(self, consumer):
        # type: (ConsumerType) -> bool
        logger.debug("Check for new messages...")

        with self._active_chunk_value_lock:
            if self._active_chunk:
                if not self._active_chunk.exists():
                    logger.debug("Somebody stole my data chunk %s", self._active_chunk)
                    self._active_chunk = self._generate_new_chunk()

        for name in [x for x in os.listdir(str(self._data_dir))]:
            chunk = FileChunk(self._data_dir, self._locks_dir, name)
            if not chunk.exists():
                logger.debug("Chunk %s doesn't exists", chunk)
                continue

            # Aquire lock for found chunk
            if chunk == self._active_chunk:
                with self._active_chunk_value_lock:
                    self._active_chunk.close()
                    # No need to free this chunk; we hold it and can work with it as needed.
                    logger.debug("Do not take lock for (previously) active chunk %s", chunk)
                    self._active_chunk = self._generate_new_chunk()
            else:
                if chunk.acquire(blocking=False):
                    # We can work with this chunk, nobody hold it
                    pass
                else:
                    logger.debug("Chunk %s not free, skip", chunk)
                    continue

            # Read data from chunk and process it
            try:
                items = chunk.read()

                if items:
                    logger.debug("Consume %d items from %s", len(items), chunk)
                    # vvvvvvvvvvvvv Usefull work here
                    consumer(items)  # <<<<<<<
                    # ^^^^^^^^^^^^^
                    logger.debug("Consumed %d items from %s", len(items), chunk)
                else:
                    logger.debug("No items found in %s, skip", chunk)
            except StopConsume:
                logger.debug("Consumer asks to stop")
                return False
            except Exception:
                logger.debug("While consume chunk %s", chunk, exc_info=True)
            else:
                chunk.clear_and_release()  # will free flock here
            finally:
                try:
                    chunk.release()  # Free flock in any case; specifically when StopConsume comes
                except Exception:
                    # We do not need to raise exception after failed to release removed file lock
                    pass

        return True

    def continuous_consume(self, consumer, chunk_size=5, send_time_s=60, check_time_s=1, first_check=True):
        # type: (ConsumerType, threading.Event, int, float, float, bool) -> None
        with self._condition:
            if self._work is False:
                self.logger.debug("Queue was stopped, will be rerun")

            if self._work is False:
                self.logger.debug("Queue already works")
                return

            self._work = True

        last_send_time = time.time()

        if first_check:
            if not self.consume(consumer):
                return

        while True:
            with self._condition:
                self._condition.wait(timeout=check_time_s)
                if self._work is False:
                    return

            if chunk_size <= self._active_chunk_items or last_send_time + send_time_s < time.time():
                last_send_time = time.time()
                if not self.consume(consumer):
                    break

    def stop(self):
        try:
            with self._condition:
                if self._work is None:
                    self.logger.debug("Queue not running")
                    return

                if self._work is False:
                    self.logger.debug("Queue was already stopped")
                    return

                if self._work is True:
                    self._work = False
                    self._condition.notify_all()
        finally:
            self._release_chunk()

    def _release_chunk(self):
        with self._active_chunk_value_lock:
            if self._active_chunk:
                self._active_chunk.release()
                self._active_chunk.close()
                self._active_chunk = None

    @staticmethod
    def _try_remove(path):
        try:
            fs.remove_file(path)
        except OSError:
            pass
