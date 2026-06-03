from devtools.ya.test.reports import trace_comment
from devtools.ya.test import const


def truncate_snippet(record, limit=const.REPORT_SNIPPET_LIMIT):
    """Truncate ``rich-snippet`` of a single report entry in place.

    ``limit`` is the maximum length passed through to
    ``trace_comment.truncate_comment``; a non-positive ``limit`` disables truncation.
    """
    if not isinstance(record, dict):
        return record

    rich_snippet = record.get("rich-snippet")
    if not rich_snippet or limit <= 0:
        return

    record["rich-snippet"] = trace_comment.truncate_comment(record["rich-snippet"], limit)
