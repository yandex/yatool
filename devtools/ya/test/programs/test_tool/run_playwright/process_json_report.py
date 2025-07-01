import json
from typing import List, Dict, Tuple
from dataclasses import dataclass
from devtools.ya.test.util import shared


@dataclass
class TestResult:
    """Represents a processed test result from Playwright report"""

    name: str
    file: str
    suites: List[str]
    project_name: str
    project_id: str
    status: str
    duration: int
    comment: str
    tries: int


def process_json_report(json_report: str) -> Tuple[List[TestResult], Dict[str, str]]:
    """
    Convert Playwright JSON report into a list of TestResult objects.

    Args:
        json_report: JSON string of Playwright report

    Returns:
        Tuple containing:
        - List of TestResult objects with typed attributes
        - Dictionary of project directories
    """
    report = json.loads(json_report)
    project_dirs = {p["id"]: p.get("testDir", "") for p in report.get("config", {}).get("projects", [])}
    tests_list = []

    def traverse_suites(suites: List[Dict], parent_suites: List[str] = None, is_top_level: bool = True) -> None:
        """Recursively traverse suites and extract tests"""
        if parent_suites is None:
            parent_suites = []

        for suite in suites:
            # For top-level suites, don't include their title in the path
            current_suites = parent_suites if is_top_level else parent_suites + [suite['title']]

            # Process specs in this suite
            for spec in suite.get('specs', []):
                for test_run in spec.get('tests', []):
                    total_duration = 0
                    comment_parts = []
                    results = test_run.get('results', [])
                    has_multiple_results = len(results) > 1

                    for i, result in enumerate(results):
                        duration = result.get('duration', 0)
                        total_duration += duration

                        if has_multiple_results:
                            comment_parts.append(
                                f"=== TRY #{i+1}: {result.get('status', 'unknown')} in {duration} ms ==="
                            )

                        for error in result.get('errors', []):
                            if isinstance(error, dict):
                                comment_parts.append('')
                                if 'message' in error:
                                    comment_parts.append(shared.clean_ansi_escape_sequences(error['message']))
                                if 'snippet' in error:
                                    comment_parts.append(shared.clean_ansi_escape_sequences(error['snippet']))

                        if comment_parts:
                            comment_parts.append('')
                            comment_parts.append('')

                    # Combine all comment parts
                    comment = "\n".join(comment_parts).strip()

                    # Create TestResult object instead of dictionary
                    tests_list.append(
                        TestResult(
                            name=spec['title'],
                            file=spec['file'],
                            suites=current_suites,
                            project_name=test_run.get('projectName'),
                            project_id=test_run.get('projectId'),
                            status=test_run.get('status'),
                            duration=total_duration,
                            comment=comment,
                            tries=len(results),
                        )
                    )

            if 'suites' in suite:
                traverse_suites(suite['suites'], current_suites, is_top_level=False)

    if 'suites' in report:
        traverse_suites(report['suites'], is_top_level=True)

    return tests_list, project_dirs
