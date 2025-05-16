import sys
import re
from xml.dom import minidom


# Python 2.X and 3.X compatibility
try:
    unichr(65)
except NameError:
    unichr = chr
try:
    unicode('A')
except NameError:
    unicode = str
try:
    long(1)
except NameError:
    long = int


from devtools.ya.test import common
from devtools.ya.test import const
from yalibrary.display import strip_markup
from library.python import strings
from devtools.ya.test import facility
from xml.sax.saxutils import escape


class JUnitReportGenerator(object):
    def create(self, file_path, suites, fix_links_func=None):
        doc = minidom.getDOMImplementation().createDocument(None, None, None)
        suites_element = self.create_element(doc, doc, 'testsuites')

        for suite in suites:
            self.add_suite(doc, suites_element, suite, fix_links_func)

        with open(file_path, 'w') as report:
            report.write(common.to_utf8(doc.toprettyxml()))

    def add_suite(self, doc, suites_element, suite, fix_links_func):
        suite_run_time = sum([test_case.elapsed for test_case in suite.tests])
        if suite.get_status() == const.Status.TIMEOUT:
            suite_run_time = max([suite.timeout, suite_run_time])
        suite_element = self.create_element(
            doc,
            suites_element,
            "testsuite",
            {
                'name': self._format_name(suite.project_path),
                'tests': len(suite.tests),
                'time': suite_run_time,
            },
        )

        stat = {"failures": 0, "skipped": 0}

        def add_test(test_case):
            test_case_attributes = {
                "name": bin_xml_escape(self._format_name(test_case.name)),
                "time": getattr(test_case, 'elapsed', 0),
            }
            test_case_element = self.create_element(doc, suite_element, "testcase", test_case_attributes)

            comment = bin_xml_escape(strings.fix_utf8(strip_markup(test_case.comment))) or ''

            if test_case.status in [const.Status.SKIPPED, const.Status.XFAIL, const.Status.NOT_LAUNCHED]:
                stat["skipped"] += 1
                skipped = self.create_element(doc, test_case_element, "skipped")
                comment = comment or "[Error message is missing or not found]"
                skipped.appendChild(self.create_text_element(doc, comment))
            elif test_case.status != const.Status.GOOD:
                stat["failures"] += 1
                if test_case.status == const.Status.MISSING and not test_case.comment:
                    test_case.comment = "cannot build one or more deps for test"
                failure = self.create_element(doc, test_case_element, "failure")
                if test_case.logs:
                    logs = []
                    for key, filename in sorted(test_case.logs.items(), key=lambda kv: kv[1]):
                        if filename:
                            if fix_links_func:
                                filename = fix_links_func(filename)
                            logs.append("{}:\n{}".format(key, filename))
                        else:
                            logs.append("{}:\n[broken link - please, report to devtools@] {}".format(key, test_case))
                    # don't add new lines for empty comment
                    if comment:
                        comment += "\n\n"
                    comment += "\n".join(logs)
                comment = comment or "[Error message is missing or not found]"
                failure.appendChild(self.create_text_element(doc, comment))

        # XXX don't mention suites without useful info
        if suite.get_comment():
            add_test(facility.TestCase(suite.get_type(), suite.get_status(), suite.get_comment(), logs=suite.logs))
        for chunk in suite.chunks:
            # Don't register chunks without tests - meaningless entries for runs with filters
            if chunk.tests or chunk.get_comment():
                add_test(
                    facility.TestCase(
                        "{} chunk".format(chunk.get_name()), chunk.get_status(), chunk.get_comment(), logs=chunk.logs
                    )
                )
            for test_case in chunk.tests:
                add_test(test_case)

        for key, value in stat.items():
            suite_element.setAttribute(key, str(value))

    @staticmethod
    def create_element(doc, parent, name, attributes=None):
        element = doc.createElement(name)
        parent.appendChild(element)
        if attributes:
            for key, value in attributes.items():
                element.setAttribute(key, common.to_utf8(value))
        return element

    @staticmethod
    def create_text_element(doc, text):
        return doc.createTextNode(escape(text))

    @staticmethod
    def _format_name(name):
        return name.replace("::", ".")


class JUnitReportGeneratorV2(object):
    def create(self, file_path, suites, fix_links_func=None):
        doc = minidom.getDOMImplementation().createDocument(None, None, None)
        suites_element = self.create_element(doc, doc, 'testsuites')

        for suite in suites:
            self.add_suite(doc, suites_element, suite, fix_links_func)

        with open(file_path, 'w') as report:
            report.write(common.to_utf8(doc.toprettyxml()))

    def add_suite(self, doc, suites_element, suite, fix_links_func):
        suite_run_time = sum([test_case.elapsed for test_case in suite.tests])
        if suite.get_status() == const.Status.TIMEOUT:
            suite_run_time = max([suite.timeout, suite_run_time])
        suite_element = self.create_element(
            doc,
            suites_element,
            "testsuite",
            {
                'name': self._format_name(suite.project_path),
                'tests': len(suite.tests),
                'time': suite_run_time,
            },
        )

        properties_element = self.create_element(doc, suite_element, "properties")
        self.create_element(
            doc, properties_element, "property", {"name": "result-type", "value": suite.get_ci_type_name()}
        )
        self.create_element(doc, properties_element, "property", {"name": "test-type", "value": suite.get_type()})
        self.create_element(doc, properties_element, "property", {"name": "test-size", "value": suite.test_size})

        stat = {"failures": 0, "skipped": 0}

        def add_test(test_case):
            classname = bin_xml_escape(self._format_name(test_case.get_class_name()))
            name = bin_xml_escape(self._format_name(test_case.get_test_case_name()))

            test_case_attributes = {
                "classname": classname,
                "name": name,
                "time": getattr(test_case, 'elapsed', 0),
            }
            test_case_element = self.create_element(doc, suite_element, "testcase", test_case_attributes)

            comment = bin_xml_escape(strings.fix_utf8(strip_markup(test_case.comment))) or ''

            if test_case.status in [const.Status.SKIPPED, const.Status.XFAIL, const.Status.NOT_LAUNCHED]:
                stat["skipped"] += 1
                skipped = self.create_element(doc, test_case_element, "skipped")
                comment = comment or "[Error message is missing or not found]"
                skipped.appendChild(self.create_text_element(doc, comment))
            elif test_case.status != const.Status.GOOD:
                stat["failures"] += 1
                if test_case.status == const.Status.MISSING and not test_case.comment:
                    test_case.comment = "cannot build one or more deps for test"
                failure = self.create_element(doc, test_case_element, "failure")
                if test_case.logs:
                    logs = []
                    for key, filename in sorted(test_case.logs.items(), key=lambda kv: kv[1]):
                        if filename:
                            if fix_links_func:
                                filename = fix_links_func(filename)
                            logs.append("{}:\n{}".format(key, filename))
                        else:
                            logs.append("{}:\n[broken link - please, report to devtools@] {}".format(key, test_case))
                    # don't add new lines for empty comment
                    if comment:
                        comment += "\n\n"
                    comment += "\n".join(logs)
                comment = comment or "[Error message is missing or not found]"
                failure.appendChild(self.create_text_element(doc, comment))

        # XXX don't mention suites without useful info
        if suite.get_comment():
            add_test(facility.TestCase(suite.get_type(), suite.get_status(), suite.get_comment(), logs=suite.logs))
        for chunk in suite.chunks:
            # Don't register chunks without tests - meaningless entries for runs with filters
            if chunk.tests or chunk.get_comment():
                add_test(
                    facility.TestCase(
                        "{}::{}".format(suite.get_type(), chunk.get_name()),
                        chunk.get_status(),
                        chunk.get_comment(),
                        logs=chunk.logs,
                    )
                )
            for test_case in chunk.tests:
                add_test(test_case)

        for key, value in stat.items():
            suite_element.setAttribute(key, str(value))

    @staticmethod
    def create_element(doc, parent, name, attributes=None):
        element = doc.createElement(name)
        parent.appendChild(element)
        if attributes:
            for key, value in attributes.items():
                element.setAttribute(key, common.to_utf8(value))
        return element

    @staticmethod
    def create_text_element(doc, text):
        return doc.createTextNode(escape(text))

    @staticmethod
    def _format_name(name):
        return name.replace("::", ".")


_legal_chars = (0x09, 0x0A, 0x0D)
_legal_ranges = (
    (0x20, 0x7E),
    (0x80, 0xD7FF),
    (0xE000, 0xFFFD),
    (0x10000, 0x10FFFF),
)
_legal_xml_re = [
    unicode("%s-%s") % (unichr(low), unichr(high)) for (low, high) in _legal_ranges if low < sys.maxunicode
]
_legal_xml_re = [unichr(x) for x in _legal_chars] + _legal_xml_re
illegal_xml_re = re.compile(unicode('[^%s]') % unicode('').join(_legal_xml_re))


def _xml_escape(ustring):
    """
    Taken from py module
    """
    escape = {
        unicode('"'): unicode('&quot;'),
        unicode('<'): unicode('&lt;'),
        unicode('>'): unicode('&gt;'),
        unicode('&'): unicode('&amp;'),
        unicode("'"): unicode('&apos;'),
    }
    charef_rex = re.compile(unicode("|").join(escape.keys()))

    def _replacer(match):
        return escape[match.group(0)]

    try:
        ustring = unicode(ustring)
    except UnicodeDecodeError:
        ustring = unicode(ustring, 'utf-8', errors='replace')
    return charef_rex.sub(_replacer, ustring)


def bin_xml_escape(arg):
    def repl(match):
        i = ord(match.group())
        if i <= 0xFF:
            return '#x%02X' % i
        else:
            return '#x%04X' % i

    return illegal_xml_re.sub(repl, arg)
