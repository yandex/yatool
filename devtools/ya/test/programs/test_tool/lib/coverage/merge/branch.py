import collections
import typing as tp

from .consts import COUNTER_LIMIT
from .shared import dedup_and_sort, compare_records


class ClangBranch(tp.NamedTuple):
    """
    Example of clang branch record (which llvm-cov export gives you) looks like this:
    [2, 12, 2, 16, 1, 5, 0, 0, 6]
    """

    line_start: int
    column_start: int
    line_end: int
    column_end: int
    true_execution_count: int
    false_execution_count: int

    # metadata
    file_id: int  # internal llvm field that is used to resolve conflicts between filenames
    expanded_file_id: int  # same as above BUT with macro handling
    region_kind: int  # Could be: 4 BranchRegion, 6 MC/DC branch region, 5 MC/DC decision region, 0 CodeRegion

    def __add__(self, other) -> tp.Self:
        if self.full_descriptor != other.full_descriptor:
            raise AssertionError(
                f"Branch mismatch left={self}, right={other}. Please contact DEVTOOLSSUPPORT with reproducer."
            )

        true_count = self.true_execution_count + other.true_execution_count
        false_count = self.false_execution_count + other.false_execution_count

        new_branch = self._replace(
            true_execution_count=min(true_count, COUNTER_LIMIT),
            false_execution_count=min(false_count, COUNTER_LIMIT),
        )

        return new_branch

    @property
    def full_descriptor(self) -> tuple[int, ...]:
        return self.local_descriptor + self.file_id_descriptor

    @property
    def local_descriptor(self) -> tuple[int, ...]:
        return self.line_start, self.column_start, self.line_end, self.column_end

    @property
    def file_id_descriptor(self) -> tuple[int, ...]:
        return self.file_id, self.expanded_file_id, self.region_kind

    def is_covered(self) -> bool:
        return self.true_execution_count and self.false_execution_count


def merge_clang_branches_generator(
    left: tp.Sequence[ClangBranch], right: tp.Sequence[ClangBranch]
) -> collections.abc.Generator[ClangBranch]:
    l_idx = 0
    r_idx = 0

    while l_idx < len(left) and r_idx < len(right):
        lb = left[l_idx]
        rb = right[r_idx]

        compare_stat = compare_records(lb, rb)

        if compare_stat == 0:
            yield lb + rb
            l_idx += 1
            r_idx += 1
        elif compare_stat == -1:
            yield lb
            l_idx += 1
        else:
            yield rb
            r_idx += 1

    while l_idx < len(left):
        yield left[l_idx]
        l_idx += 1

    while r_idx < len(right):
        yield right[r_idx]
        r_idx += 1


def merge_clang_branches(branches: tp.Sequence[list[int]]) -> list[ClangBranch]:
    """
    branches must be size of 2
    each of them is supposed to be sorted https://llvm.org/docs/CoverageMappingFormat.html#sub-array-of-regions
    """
    left = dedup_and_sort([ClangBranch(*branch) for branch in branches[0]])
    right = dedup_and_sort([ClangBranch(*branch) for branch in branches[1]])

    return list(merge_clang_branches_generator(left, right))
