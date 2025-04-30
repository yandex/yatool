import logging
import threading
import time

import exts.yjson as json


class BatchEventProcessor:
    _logger = logging.getLogger('BatchEventProcessor')

    def __init__(self, func, delay=10):
        assert func
        self._func = func
        self._delay = delay
        self._lock = threading.RLock()
        self._queue = []
        self._thread = None
        self._stop_ev = threading.Event()
        self._stat = {
            'entries_processed': 0,
            'max_queue_size': 0,
            'total_process_calls': 0,
            'total_processing_duration': 0.0,
        }
        self._start_thread_loop()

    def add_entry(self, item):
        with self._lock:
            self._queue.append(item)

    def add_entries(self, items):
        with self._lock:
            self._queue.extend(items)

    def process_queue(self):
        with self._lock:
            if not self._queue:
                return

            ts = time.time()
            qsize = len(self._queue)

            self._func(self._queue)
            # Do not delete objects, but replace the list - the underlying _process
            # implementation can process data in asynchronous mode.
            self._queue = []

            self._update_stat(time.time() - ts, qsize)

    def stop(self):
        self._logger.debug('Stop requested')
        self._stop_ev.set()
        self._thread.join()
        self.process_queue()
        self._logger.debug('Finished processor (%s) statistics: %s', self._func, self._stat)

    def _update_stat(self, duration, qsize):
        self._stat['total_process_calls'] += 1
        self._stat['total_processing_duration'] += duration
        self._stat['entries_processed'] += qsize
        if qsize > self._stat['max_queue_size']:
            self._stat['max_queue_size'] = qsize

    def _start_thread_loop(self):
        self._thread = threading.Thread(target=self._thread_loop)
        self._thread.daemon = True
        self._thread.start()

    def _thread_loop(self):
        try:
            while not self._stop_ev.wait(self._delay):
                self.process_queue()
        finally:
            self._logger.debug("Thread loop is terminated")


class BatchReportBase:
    _logger = logging.getLogger('BatchReportBase')

    def __init__(self, func, delay=10):
        self._processor = BatchEventProcessor(func=func, delay=delay)
        self._progress_channel = None

    def set_progress_channel(self, functor):
        self._progress_channel = functor

    def trace_stage(self, build_stage):
        pass

    def finish_style_report(self):
        pass

    def finish_configure_report(self):
        pass

    def finish_build_report(self):
        pass

    def finish_tests_report(self):
        pass

    def finish_tests_report_by_size(self, size):
        pass

    def finish(self):
        self._processor.stop()

    def __call__(self, entries):
        self._processor.add_entries(entries)

    def _add_entry(self, item):
        self._processor.add_entry(item)

    def _process_queue(self):
        self._processor.process_queue()


class JsonLineReport(BatchReportBase):
    _logger = logging.getLogger('JsonLineReport')

    def __init__(self, filename, delay=5, report_progress=True, progress_delay=180):
        super().__init__(func=self._process, delay=delay)
        self._file = open(filename, 'w')
        self._lock = threading.Lock()
        self._report_progress = report_progress
        self._progress_delay = progress_delay
        # Dump progress with first events
        self._progress_timestamp = 0
        self._test_report_closed = False

    def _process(self, entries):
        assert self._file, entries[0]

        lines = []
        for x in entries:
            data = {
                "time": int(time.time()),
                "type": "result",
                "data": x,
            }
            lines.append(json.dumps(data))

        with self._lock:
            for line in lines:
                self._file.write(line)
                self._file.write("\n")
            self._file.flush()

        if self._report_progress:
            ts = time.time()
            if ts > self._progress_timestamp + self._progress_delay:
                self._progress_timestamp = ts
                self._log_progress()

    def _log_progress(self):
        if self._report_progress and self._progress_channel:
            self._process_event(
                type="progress",
                value=self._progress_channel(),
            )

    def trace_stage(self, build_stage):
        if build_stage and build_stage.get("name"):
            self._process_event(
                type="action",
                name="trace_stage",
                stage=build_stage["name"],
            )

    def finish_configure_report(self):
        self._finish_test_type_report("configure")

    def finish_build_report(self):
        self._finish_test_type_report("build")

    def finish_style_report(self):
        self._finish_test_type_report("style")

    def finish_tests_report(self):
        self._finish_test_type_report("test")

    def finish_tests_report_by_size(self, size):
        self._finish_test_type_report("test", size)

    def _process_event(self, **kw):
        assert self._file, kw
        assert "type" in kw, kw
        kw["time"] = int(time.time())
        with self._lock:
            json.dump(kw, self._file)
            self._file.write("\n")
            self._file.flush()

    def _finish_test_type_report(self, test_type, test_size=None):
        self._process_queue()

        kw = {}
        if test_size:
            assert test_type == "test", locals()
            kw['test_size'] = test_size

        self._process_event(type="action", name="finish_test_type", test_type=test_type, **kw)
        self._logger.debug(
            "%s%s report is finished", test_type.capitalize(), ' ({})'.format(test_size) if test_size else ''
        )

    def finish(self):
        super().finish()
        self._log_progress()
        self._file.close()
        self._file = None


# TODO we need to migrate users to JsonLineReport and get rid of StoredReport
class StoredReport:
    _logger = logging.getLogger('StoredReport')

    def __init__(self):
        self._results = []
        self._progress_channel = lambda: None

    def set_progress_channel(self, functor):
        self._progress_channel = functor

    def trace_stage(self, build_stage):
        self._logger.debug('Trace build stage %s', build_stage)

    def finish_style_report(self):
        self._logger.debug('Finish style report')

    def finish_configure_report(self):
        self._logger.debug('Finish configure report')

    def finish_build_report(self):
        self._logger.debug('Finish build report')

    def finish_tests_report(self):
        self._logger.debug('Finish tests report')

    def finish_tests_report_by_size(self, size):
        self._logger.debug('Finish tests report by size %s', size)

    def finish(self):
        self._logger.debug('Finish report')

    def __call__(self, entries):
        self._results.extend(entries)

    def make_report(self):
        return {
            'results': self._results,
            'static_values': {},
            'progress': self._progress_channel(),
        }


def safe_read_report_config(path):
    if path:
        with open(path) as fp:
            return json.load(fp)
    else:
        return {}


def transform_toolchain(report_config, toolchain):
    return report_config.get('toolchain_transforms', {}).get(toolchain, toolchain)


def is_toolchain_ignored(report_config, toolchain):
    return toolchain in report_config.get('ignored_toolchains', [])
