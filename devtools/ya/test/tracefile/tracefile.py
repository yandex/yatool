# coding: utf-8

import base64
import collections
import json
import logging
import os
import io

import six

from library.python import strings
from test import const
from devtools.ya.test import facility
from yalibrary.formatter import term

logger = logging.getLogger(__name__)


class TestEventParser(object):
    def __init__(self, suite=None, reporter=None):
        self.reporter = reporter
        self.suite = suite or facility.Suite()
        self.current_chunk = None
        self.testcases = collections.OrderedDict()
        self.chunks = {}

    def add_test(self, chunk, test):
        chunk_name = chunk.get_name()
        test_name = "-".join([_f for _f in [test.name, test.test_type, test.path] if _f])
        name = (chunk_name, test_name)

        if name not in self.testcases:
            self.testcases[name] = test
            return

        # save original started timestamp, because time when test was mentioned
        # for the first time is actual started timestamp from subtest_started event,
        # actual data comes from subtest_finished event,
        # but it's 'timestamp' field specifies time when test was finished
        started = self.testcases[name].started

        if test_name == "fuzz::test":
            # XXX extra steps to merge fuzz::test chunks to the single test
            self._merge_fuzz_test(self.testcases[name], test)
            if self.testcases[name].status in [
                const.Status.NOT_LAUNCHED,
                const.Status.CRASHED,
                const.Status.DESELECTED,
            ]:
                self.testcases[name] = test
            else:
                if test.status not in [const.Status.GOOD, const.Status.DESELECTED]:
                    test.metrics = self.testcases[name].metrics
                    self.testcases[name] = test
        else:
            if self.testcases[name].status in [
                const.Status.NOT_LAUNCHED,
                const.Status.CRASHED,
                const.Status.DESELECTED,
            ]:
                # replace some previously set statuses
                self.testcases[name] = test
            else:
                if test.status != const.Status.DESELECTED:
                    self.testcases[name] = test

        if started and self.testcases[name].status not in [const.Status.NOT_LAUNCHED, const.Status.DESELECTED]:
            self.testcases[name].started = started

    @staticmethod
    def _merge_fuzz_test(stored, newrec):
        if 'baseunit' in newrec.logs and 'baseunit' not in stored.logs:
            stored.logs['baseunit'] = newrec.logs['baseunit']
        if 'slowest_baseunit' in newrec.logs:
            # There was no slowest_baseunit or we found a slower one
            if 'slowest_baseunit' not in stored.logs or int(newrec.metrics.get('slowest_unit_time_sec', '0')) > int(
                stored.metrics.get('slowest_unit_time_sec', '0')
            ):
                stored.logs['slowest_baseunit'] = newrec.logs['slowest_baseunit']

        for func, metrics in [
            (
                sum,
                [
                    'corpus_size',
                    'number_of_executed_units',
                ],
            ),
            (
                max,
                [
                    'peak_rss_mb',
                    'pcs_',
                    'slowest_unit_time_sec',
                ],
            ),
            (
                min,
                [
                    'fuzz_iterations_per_second',
                ],
            ),
        ]:
            for name in metrics:
                for m in newrec.metrics:
                    if m.startswith(name):
                        if m in stored.metrics:
                            stored.metrics[m] = func((stored.metrics[m], newrec.metrics[m]))
                        else:
                            stored.metrics[m] = newrec.metrics[m]

    def subtest_started(self, event):
        """
        subtest entry must be closed and contain all the necessary data about belonging to a chunk or suite
        """

        self.setup_chunk(event)
        testname = self.extract_test_name(event)

        self.add_test(
            self.current_chunk,
            # this fake test will be removed if the corresponding subtest-finished event comes
            facility.TestCase(
                testname,
                const.Status.CRASHED,
                "Test crashed",
                0,
                None,
                None,
                event.get('logs'),
                event.get('cwd', ''),
                path=event.get('path'),
                started=event['timestamp'],
            ),
        )

        if self.reporter:
            self.reporter.on_test_case_started(testname)

    def setup_chunk(self, event):
        self.current_chunk = self.chunks.setdefault(
            self.get_chunk_name(event),
            facility.Chunk(event.get('nchunks') or 1, event.get('chunk_index') or 0, event.get('chunk_filename')),
        )

    def get_chunk_name(self, event):
        return facility.Chunk.gen_chunk_name(
            event.get('nchunks') or 1,
            event.get('chunk_index') or 0,
            event.get('chunk_filename'),
        )

    def extract_test_name(self, event):
        return "{}::{}".format(event['class'], event['subtest'])

    def subtest_finished(self, event):
        self.setup_chunk(event)

        comment = event.get('comment', '').strip()
        # replace escape codes with markup in comment
        comment = term.ansi_codes_to_markup(comment)

        test = facility.TestCase(
            self.extract_test_name(event),
            const.Status.BY_NAME[event['status']],
            comment,
            event.get('time', 0.0),
            event.get('result'),
            event.get('type'),
            event.get('logs'),
            event.get('cwd', ''),
            event.get('metrics'),
            event.get('path'),
            event.get('is_diff_test', False),
            tags=event.get('tags'),
        )
        self.add_test(self.current_chunk, test)

        if self.reporter and test.status != const.Status.NOT_LAUNCHED:
            self.reporter.on_test_case_finished(test)

    def message(self, event):
        if self.reporter:
            self.reporter.on_message(event["text"])

    def chunk_event(self, event):
        self.setup_chunk(event)
        self.process_container(self.current_chunk, event)

    def suite_event(self, event):
        self.process_container(self.suite, event)

    @staticmethod
    def process_container(container, event):
        errors = event.get('errors', [])
        errors = [(const.Status.BY_NAME[status], msg) for status, msg in errors]
        container._errors.extend(errors)
        container.logs.update(event.get('logs', {}))
        container.metrics.update(event.get('metrics', {}))

    def test_started(self, event):
        pass

    def test_finished(self, event):
        pass

    def finalize(self):
        for (chunk_name, _), testcase in self.testcases.items():
            assert chunk_name in self.chunks, (chunk_name, self.chunks.keys())
            self.chunks[chunk_name].tests.append(testcase)
        self.testcases.clear()

        # TODO move to results_accumulator node
        # self.suite.metrics['chunks_count'] = len(self.chunks)

        self.suite.chunks = []
        for name in sorted(self.chunks):
            self.suite.chunks.append(self.chunks[name])
        self.chunks.clear()

        if self.reporter:
            self.reporter.on_test_suite_finish(self.suite)


class ParsingError(Exception):
    def __init__(self, data):
        self.data = data


class TestTraceParser(object):
    @staticmethod
    def parse_from_file(filename, reporter=None, suite=None, relaxed=False):
        logger.debug('Read trace data from %s', filename)
        if not os.path.exists(filename):
            logger.debug('Trace file %s is not found', filename)
            return None

        with io.open(filename, errors='ignore', encoding='utf-8') as afile:
            return TestTraceParser.parse(afile, reporter, suite, relaxed)

    @staticmethod
    def parse_from_string(data, reporter=None, suite=None, relaxed=False):
        return TestTraceParser.parse(data.splitlines(), reporter, suite, relaxed)

    @staticmethod
    def parse(it, reporter=None, suite=None, relaxed=False):
        event_parser = TestEventParser(suite, reporter)
        error = None

        try:
            for entry in it:
                TestTraceParser.process_event(event_parser, entry, relaxed)
        except ParsingError as e:
            error = e.data

        event_parser.finalize()

        if error:
            event_parser.suite.chunk.add_error(
                "[[bad]]Test run information is incomplete. Did you run out of space? Unable to load line b64: '{}'".format(
                    error
                )
            )

        return event_parser.suite

    @staticmethod
    def process_event(event_parser, line, relaxed=False):
        if line:
            line = strings.to_unicode(line)
            line_striped = line.strip('\r\t\n\x00 ')
            if not line_striped:
                return

            if len(line_striped) + 2 < len(line):
                logger.debug("%d trash bytes found in tracefile", len(line) - len(line_striped))

            try:
                event = strings.ensure_str_deep(json.loads(line_striped))
            except ValueError:
                b64data = base64.b64encode(six.ensure_binary(line))
                if relaxed:
                    raise ParsingError(b64data)
                logger.error('Incorrect json - unable to load line b64:"%s"', b64data)
                raise

            try:
                data = event['value']
                data['timestamp'] = event['timestamp']
                event_parser.__getattribute__(event['name'].replace('-', '_'))(data)
            except Exception:
                logger.error('Failed to process event, b64:"%s"', base64.b64encode(six.ensure_binary(line)))
                raise
