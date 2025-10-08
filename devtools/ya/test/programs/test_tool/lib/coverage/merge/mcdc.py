import typing as tp
import itertools

from collections.abc import Sequence, Generator

from .consts import MCDC_EXECUTED_TEST_VECTORS_IDX
from .shared import dedup_and_sort, compare_records

type Condition = bool | None  # true, false, null (null when it's been short circuited)


class ExecutedTestVector(tp.NamedTuple):
    conditions: tuple[Condition, ...]
    executed: bool
    result: bool


class MCDCRecord(tp.NamedTuple):
    """
    Example of mcdc record (which llvm-cov export gives you) looks like this:
    [
              2,
              12,
              2,
              34,
              0,
              5,
              [
                true,
                true,
                true
              ],
              [
                {
                  "conditions": [
                    false,
                    false,
                    null
                  ],
                  "executed": true,
                  "result": false
                },
                ...
              ]
            ]
    """

    line_start: int
    column_start: int
    line_end: int
    column_end: int
    expanded_file_id: int
    region_kind: int  # Could be: 4 BranchRegion, 6 MC/DC branch region, 5 MC/DC decision region, 0 CodeRegion
    conditions: list[bool]  # list of covered conditions inside a region
    executed_test_vectors: set[ExecutedTestVector]

    @classmethod
    def from_raw(cls, raw_record: list) -> tp.Self:
        """Parse a raw llvm-cov MC/DC record into an MCDCRecord."""

        if (n := len(raw_record)) != 8:
            raise ValueError(f"Invalid raw MC/DC record: actual len = {n}, expected = 8")

        executed_test_vectors: set[ExecutedTestVector] = {
            ExecutedTestVector(conditions=tuple(tv["conditions"]), executed=tv["executed"], result=tv["result"])
            for tv in raw_record[MCDC_EXECUTED_TEST_VECTORS_IDX]
        }

        return cls(
            *raw_record[:6],  # line_start to region_kind
            conditions=raw_record[6][:],  # list[bool]
            executed_test_vectors=executed_test_vectors,
        )

    def __add__(self, other):
        if self.full_descriptor != other.full_descriptor:
            raise AssertionError(
                f"MC/DC Record mismatch left={self}, right={other}. Please contact DEVTOOLSSUPPORT with reproducer."
            )

        merged_test_vectors = self.executed_test_vectors | other.executed_test_vectors

        new_instance = self._replace(
            executed_test_vectors=merged_test_vectors,
        )

        new_instance._recalc_conditions()

        return new_instance

    @property
    def percentage(self) -> float:
        n = len(self.conditions)
        if not n:
            return 0.0

        true_count = sum(self.conditions)
        return true_count / n * 100

    @staticmethod
    def condition_proves_independence(tv1: ExecutedTestVector, tv2: ExecutedTestVector, cond_idx: int) -> bool:
        """
        https://github.com/llvm/llvm-project/blob/0a0d4979935cc13ecafdb8c9b00dd74779651781/llvm/lib/ProfileData/Coverage/CoverageMapping.cpp#L249-L252
        """
        conds1 = tv1.conditions
        conds2 = tv2.conditions

        if conds1[cond_idx] is None or conds2[cond_idx] is None:
            return False

        if conds1[cond_idx] == conds2[cond_idx]:
            return False

        if tv1.result == tv2.result:
            return False

        # Check if all other evaluated conditions are the same
        for idx in range(len(conds1)):
            if idx == cond_idx:
                continue
            # Skip if either is None (not evaluated due to short-circuit)
            if conds1[idx] is None or conds2[idx] is None:
                continue
            if conds1[idx] != conds2[idx]:
                return False

        return True

    def is_condition_covered(self, cond_idx: int) -> bool:
        if self.conditions[cond_idx]:
            return True

        for tv1, tv2 in itertools.combinations(self.executed_test_vectors, 2):
            # we found the right pair
            if self.condition_proves_independence(tv1, tv2, cond_idx):
                return True

        return False

    def _recalc_conditions(self) -> None:
        for cond_idx in range(len(self.conditions)):
            self.conditions[cond_idx] = self.is_condition_covered(cond_idx)

    @property
    def local_descriptor(self) -> tuple[int, ...]:
        return (
            self.line_start,
            self.column_start,
            self.line_end,
            self.column_end,
        )

    @property
    def file_id_descriptor(self) -> tuple[int, ...]:
        return self.expanded_file_id, self.region_kind

    @property
    def full_descriptor(self) -> tuple[int, ...]:
        """
        Some macros are dynamic... For example: util/system/compiler.h:L92-93
        We have to keep that in mind -> add len(conditions) to descriptor
        """
        return self.local_descriptor + self.file_id_descriptor + (len(self.conditions),)

    def dump(self) -> list:
        return [
            self.line_start,
            self.column_start,
            self.line_end,
            self.column_end,
            self.expanded_file_id,
            self.region_kind,
            self.conditions,
            [tv._asdict() for tv in self.executed_test_vectors],
        ]


def merge_clang_mcdc_records_generator(
    left: Sequence[MCDCRecord], right: Sequence[MCDCRecord]
) -> Generator[MCDCRecord]:
    l_idx = 0
    r_idx = 0

    while l_idx < len(left) and r_idx < len(right):
        left_record = left[l_idx]
        right_record = right[r_idx]

        compare_stat = compare_records(left_record, right_record)

        if compare_stat == 0:
            yield left_record + right_record
            l_idx += 1
            r_idx += 1
        elif compare_stat == -1:
            yield left_record
            l_idx += 1
        else:
            yield right_record
            r_idx += 1

    while l_idx < len(left):
        yield left[l_idx]
        l_idx += 1

    while r_idx < len(right):
        yield right[r_idx]
        r_idx += 1


def merge_clang_mcdc_records(mcdc_records: Sequence) -> list[MCDCRecord]:
    """
    mcdc_records must be size of 2
    each of them is supposed to be sorted https://llvm.org/docs/CoverageMappingFormat.html#sub-array-of-regions
    """
    left = dedup_and_sort([MCDCRecord.from_raw(record) for record in mcdc_records[0]])
    right = dedup_and_sort([MCDCRecord.from_raw(record) for record in mcdc_records[1]])

    return list(merge_clang_mcdc_records_generator(left, right))
