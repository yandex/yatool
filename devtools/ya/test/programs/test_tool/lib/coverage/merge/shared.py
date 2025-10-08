from collections.abc import Sequence
from typing import Protocol, Literal, Self


class MergeableRecord(Protocol):
    """Protocol for records that can be compared, added, and have a descriptor."""

    @property
    def local_descriptor(self) -> tuple[int, ...]: ...

    @property
    def full_descriptor(self) -> tuple[int, ...]: ...

    @property
    def file_id_descriptor(self) -> tuple[int, ...]: ...

    def __add__(self, other: Self) -> Self: ...


def compare_records(left: MergeableRecord, right: MergeableRecord) -> Literal[-1, 0, 1]:
    """
    returns
    0  - left == right (=)
    -1 - left is earlier (<)
    1  - left is later (>)
    """

    if left.full_descriptor == right.full_descriptor:
        return 0

    if left.full_descriptor < right.full_descriptor:
        return -1

    return 1


def dedup_and_sort(records: Sequence[MergeableRecord]) -> list[MergeableRecord]:
    """
    Generic function to deduplicate and sort a sequence of MergeableRecord objects.
    - Sorts based on local_descriptor.
    - Merges duplicates (where compare_with == 0) using __add__.

    Important note:
    In some cases llvm could return result with duplicates (for branches and mcdc)
    It happens due to macros like util/system/compiler.h:L92-93
    Seems like llvm is not ideal and we have to cover such cases.

    In order to reproduce it check this target out:
    1) run tests with coverage here devtools/executor/tests/fat
    2) check out results for util/system/compiler.h
    """
    if not records:
        return []

    sorted_records = sorted(records, key=lambda r: r.full_descriptor)

    result: list[MergeableRecord] = []
    current = sorted_records[0]

    for next_record in sorted_records[1:]:
        compare_stat = compare_records(current, next_record)
        if compare_stat == 0:
            current = current + next_record
        else:
            result.append(current)
            current = next_record

    result.append(current)

    return result
