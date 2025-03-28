import logging
import optparse
import os

import devtools.ya.test.common
import devtools.ya.test.const
import devtools.ya.test.reports
import devtools.ya.test.result
import devtools.ya.test.test_types.common
import devtools.ya.test.util.shared

import exts.archive

logger = logging.getLogger(__name__)


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_option("--allure", dest="allure_report", help="allure file path", action='store', default=None)
    parser.add_option("--token", dest="token", help="sb token path for downloading JDK", action='store', default=None)
    parser.add_option(
        "--token-path", dest="token_path", help="sb token path for downloading JDK", action='store', default=None
    )
    parser.add_option(
        "--allure-tars", dest="allure_tars", help="list of allure report tars", action='append', default=[]
    )

    return parser.parse_args()


def main():
    options, _ = get_options()
    devtools.ya.test.util.shared.setup_logging(options.log_level, options.log_path)
    for allure_tar in options.allure_tars:
        allure_dirname = os.path.dirname(allure_tar)
        exts.archive.extract_from_tar(allure_tar, os.path.join(allure_dirname, "allure"))
        logger.info("Allure report successfully finished")
    try:
        devtools.ya.test.reports.AllureReportGenerator().create(os.getcwd(), options.allure_report)
    except devtools.ya.test.reports.AllureReportNotFoundError:
        logger.exception("Allure results not found\n")
    return 0


if __name__ == '__main__':
    main()
