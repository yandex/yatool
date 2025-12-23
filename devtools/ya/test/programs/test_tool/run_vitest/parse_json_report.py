import dataclasses
import json


@dataclasses.dataclass
class TestResult:
    """Represents a processed test result from Playwright report"""

    name: str
    file: str
    suites: list[str]
    status: str
    duration_ms: int
    comment: str


def parse_json_report(report_json: str) -> list[TestResult]:
    report = json.loads(report_json)
    results = []

    for suite_result in report.get("testResults", []):
        for test_result in suite_result["assertionResults"]:
            results.append(
                TestResult(
                    name=test_result.get("title"),
                    file=suite_result.get("name"),
                    suites=test_result.get("ancestorTitles", []),
                    status=test_result.get("status"),
                    duration_ms=test_result.get("duration", 0),
                    comment="\n\n".join(test_result.get("failureMessages", [])),
                )
            )

    return results
