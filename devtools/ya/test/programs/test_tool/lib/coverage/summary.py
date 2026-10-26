"""Go statement coverage counters shared by the resolver and the merger."""

from collections.abc import Sequence

# segment coordinates -> [num_statements, covered]
GoBlocks = dict[tuple[int, int, int, int], list[int]]


def statement_summary(covered: int, count: int) -> dict[str, dict[str, int | float]]:
    """Statement coverage of a file, weighted the same way as 'go tool cover' does."""
    percent = 100.0 * covered / count if count else 0
    return {"statements": {"covered": covered, "count": count, "percent": percent}}


def add_go_blocks(blocks: GoBlocks, segments: Sequence[Sequence[int]], statements: Sequence[int]) -> None:
    """Accumulate go profile blocks: same block from another suite only raises the covered flag.

    Args:
        blocks: accumulator keyed by segment coordinates.
        segments: unified segments [start_line, start_col, end_line, end_col, covered] of one resolved file.
        statements: number of statements per segment, aligned with segments.
    """
    for segment, num_statements in zip(segments, statements):
        block = blocks.setdefault(tuple(segment[:4]), [num_statements, 0])
        block[1] |= int(bool(segment[4]))


def go_statement_summary(blocks: GoBlocks) -> dict[str, dict[str, int | float]]:
    count = sum(num_statements for num_statements, _ in blocks.values())
    covered = sum(num_statements for num_statements, is_covered in blocks.values() if is_covered)
    return statement_summary(covered, count)
