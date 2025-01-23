"""
Example STDOUT (tsc --pretty): ./tests/ut/real_output.txt
"""

import re
import library.python.color as color
import devtools.ya.test.util.shared as shared

FILENAME_PATTERN = re.compile(r"^(.*)\:\d+\:\d+ \- ")
FOOTER_PATTERN = re.compile(r"^Found \d+ errors?")


def parse_output(stdout: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    key = ""
    for line in simplify_colors(stdout).strip().splitlines():
        clean_line = shared.clean_ansi_escape_sequences(line)
        if not clean_line.startswith(" "):
            matches = FILENAME_PATTERN.search(clean_line)
            if matches:
                key = matches[1]
        if FOOTER_PATTERN.match(clean_line):
            break

        file_result = result.get(key, [])
        file_result.append(line)
        result[key] = file_result

    return result


def simplify_colors(data):
    """
    Some tools use light-* colors instead of simple ones, this yet to be supported by ya make
    (refer FBP-999 for details)
    For now we can handle the light-* colors by transforming those into simple ones
    e.g. LIGHT_CYAN (96) -> CYAN (36)
    """

    for col in range(30, 38):
        high_col = col + 60
        data = data.replace(color.get_code(high_col), color.get_code(col))

    return data
