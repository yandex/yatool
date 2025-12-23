from .consts import COUNTER_LIMIT


def merge_granular_coverage_segments(record, segments):
    if not record:
        return segments
    start_line = 0
    start_shift = 1
    end_line = 2
    end_shift = 3
    is_covered = 4

    # invariant: l_seg start <= r_seg start
    def compare_segments(l_seg, r_seg):
        if l_seg is None:
            return True
        elif l_seg[start_line] != r_seg[start_line]:
            return l_seg[start_line] < r_seg[start_line]
        elif l_seg[start_shift] != r_seg[start_shift]:
            return l_seg[start_shift] < r_seg[start_shift]
        elif l_seg[end_line] != r_seg[end_line]:
            return l_seg[end_line] < r_seg[end_line]
        elif l_seg[end_shift] != r_seg[end_shift]:
            return l_seg[end_shift] < r_seg[end_shift]
        else:
            return False

    # invariant: l_seg start <= r_seg start
    def is_equal_starts(l_seg, r_seg):
        return l_seg[start_line] == r_seg[start_line] and l_seg[start_shift] == r_seg[start_shift]

    # invariant: l_seg start <= r_seg start
    def is_equal_ends(l_seg, r_seg):
        return l_seg[end_line] == r_seg[end_line] and l_seg[end_shift] == r_seg[end_shift]

    # invariant: l_seg start <= r_seg start
    def is_equal_ranges(l_seg, r_seg):
        return is_equal_starts(l_seg, r_seg) and is_equal_ends(l_seg, r_seg)

    # invariant: l_seg start <= r_seg start
    def compare_ends(l_seg, r_seg):
        if l_seg[end_line] != r_seg[end_line]:
            return l_seg[end_line] < r_seg[end_line]
        else:
            return l_seg[end_shift] < r_seg[end_shift]

    # invariant: l_seg start <= r_seg start
    def can_be_merged(l_seg, r_seg, covered):
        """
        compare left end with right start.
        if (covered and (left end + 1 >= right start)) or (not covered and sequential) => segments can be merged
        """
        if l_seg[end_line] == r_seg[start_line]:
            return l_seg[end_shift] + 1 >= r_seg[start_shift]
        else:
            return l_seg[end_line] > r_seg[start_line]

    def segments_iterator(left_seg, right_seg):
        left_len = len(left_seg)
        right_len = len(right_seg)
        left_pos = 0
        right_pos = 0
        while left_pos < left_len or right_pos < right_len:
            if left_pos == left_len:
                yield right_seg[right_pos]
                right_pos += 1
                continue
            elif right_pos == right_len:
                yield left_seg[left_pos]
                left_pos += 1
                continue
            if compare_segments(left_seg[left_pos], right_seg[right_pos]):
                yield left_seg[left_pos]
                left_pos += 1
            else:
                yield right_seg[right_pos]
                right_pos += 1

    def check_inter(left, right):
        if left[end_line] != right[start_line]:
            return left[end_line] > right[start_line]
        else:
            return left[end_shift] > right[start_shift]

    def merge():
        uni_res = []
        sorted_segments = segments_iterator(record, segments)
        cur_seg = None
        for seg in sorted_segments:
            seg = list(seg)
            if cur_seg is None:
                cur_seg = seg
            elif check_inter(cur_seg, seg):
                if not is_equal_starts(cur_seg, seg):
                    # |-----------|
                    #     |-------|
                    if cur_seg[is_covered]:
                        uni_res.append(
                            [
                                cur_seg[start_line],
                                cur_seg[start_shift],
                                seg[start_line],
                                seg[start_shift],
                                cur_seg[is_covered],
                            ]
                        )

                    cur_seg[start_line] = seg[start_line]
                    cur_seg[start_shift] = seg[start_shift]
                if compare_ends(cur_seg, seg):
                    cur_seg, seg = seg, cur_seg
                uni_res.append(
                    [
                        seg[start_line],
                        seg[start_shift],
                        seg[end_line],
                        seg[end_shift],
                        seg[is_covered] | cur_seg[is_covered],
                    ]
                )
                if not is_equal_ranges(cur_seg, seg):
                    cur_seg[start_line] = seg[end_line]
                    cur_seg[start_shift] = seg[end_shift]
                else:
                    cur_seg = None
            else:
                if cur_seg[is_covered]:
                    uni_res.append(cur_seg)

                cur_seg = seg
        if cur_seg is not None and cur_seg[is_covered]:
            uni_res.append(cur_seg)
        return uni_res

    def compact_flatten_segments(merged_segments):
        if len(merged_segments) < 2:
            return merged_segments
        result = []
        prev_seg = merged_segments[0]
        for seg in merged_segments[1:]:
            if prev_seg[is_covered] == seg[is_covered] and can_be_merged(prev_seg, seg, prev_seg[is_covered]):
                prev_seg[end_line] = seg[end_line]
                prev_seg[end_shift] = seg[end_shift]
            else:
                result.append(prev_seg)
                prev_seg = seg
        if prev_seg:
            result.append(prev_seg)
        return result

    return compact_flatten_segments(merge())


def merge_segments(record1, record2):
    if not record1:
        return record2

    # unified format
    start_pos = 0
    end_pos = 2
    counter = 4
    # line coverage doesn't use segment line pos - use it to store missing brach flag
    missing_branch = 1

    def next_segment(seq):
        def tip_iter():
            try:
                it = iter(seq)
                while True:
                    missed_branch = False
                    useg = next(it)
                    # Skip all empty segments (aka java-missed-branch-in-line-indicator) and put this knowledge into the flag
                    while useg[start_pos] == useg[end_pos]:
                        missed_branch = True
                        useg = next(it)

                    segment = list(useg)
                    assert not segment[missing_branch], segment
                    segment[missing_branch] = missed_branch
                    yield segment
            except StopIteration:
                return

        it = tip_iter()

        def next_item():
            try:
                return next(it)
            except StopIteration:
                return None

        return next_item

    def merge():
        next_left = next_segment(record1)
        next_right = next_segment(record2)
        left = next_left()
        right = next_right()

        while left and right:
            start_diff = left[start_pos] - right[start_pos]
            # Most common case - segment ranges are equal
            if start_diff == 0:
                end_diff = left[end_pos] - right[end_pos]
                if end_diff < 0:
                    left, right = right, left
                    next_right, next_left = next_left, next_right
                head = list(left)

                if head[missing_branch] and right[missing_branch] and end_diff:
                    head[missing_branch] = False
                    head[counter] += right[counter]
                    yield head
                    # skip uncovered segment - it's not a part of head (which is covered)
                    if end_diff:
                        next_right()
                    right = next_right()
                    left = next_left()
                    continue

                head[end_pos] = right[end_pos]
                if head[missing_branch] == right[missing_branch]:
                    head[counter] += right[counter]
                else:
                    # Don't store missing branch flag if it's merged with covered segment
                    head[missing_branch] = not (head[counter] and right[counter])
                    head[counter] = max(head[counter], right[counter])
                yield head

                if end_diff == 0:
                    right = next_right()
                    left = next_left()
                # Overlapped
                else:
                    left[start_pos] = right[end_pos]
                    right = next_right()
            else:
                if start_diff > 0:
                    left, right = right, left
                    next_right, next_left = next_left, next_right
                # Overlapped
                if left[end_pos] > right[start_pos]:
                    head = list(left)
                    head[end_pos] = right[start_pos]
                    yield head
                    left[start_pos] = right[start_pos]
                else:
                    yield left
                    left = next_left()

        while left:
            yield left
            left = next_left()
        while right:
            yield right
            right = next_right()

    def unify(s):
        s[missing_branch] = 0
        return s

    def flatten():
        curr = None
        for segment in merge():
            # process missed-branch-in-line-indicator (mbili)
            if segment[missing_branch]:
                if curr:
                    # previous segment
                    yield curr
                # gen mbili
                yield [segment[start_pos], 0, segment[start_pos], 0, 0]
                yield unify(segment)
                curr = None
                continue

            # Direct flatten (no mbili here)
            if not curr:
                curr = unify(segment)
            elif (
                curr[end_pos] >= segment[start_pos]
                and bool(curr[counter]) == bool(segment[counter])
                and curr[missing_branch] == segment[missing_branch]
            ):
                curr[end_pos] = segment[end_pos]
            else:
                yield curr
                curr = unify(segment)

        if curr:
            yield curr

    return list(flatten())


def merge_clang_segments(segments):
    # type: (tuple[list[tuple[int, int, int, bool, bool, bool]], list[tuple[int, int, int, bool, bool, bool]]]) -> list[list[int|bool]]|list[tuple[int, int, int, bool, bool, bool]]
    left, right = segments  # typing: ignore [misc]
    if not left:
        return right
    if not right:
        return left
    # there is at least one item in both

    # clang's segment format: Line, Col, Count, HasCount, IsRegionEntry, IsGapRegion
    COUNTER_FIELD = 2
    HASCOUNT_FIELD = 3
    ISREGIONENTRY_FIELD = 4
    ISGAPREGION_FIELD = 5

    def update_by(seg_to_update, seg):
        seg_to_update[COUNTER_FIELD] = min(seg_to_update[COUNTER_FIELD] + seg[COUNTER_FIELD], COUNTER_LIMIT)
        seg_to_update[HASCOUNT_FIELD] = seg_to_update[HASCOUNT_FIELD] or seg[HASCOUNT_FIELD]
        seg_to_update[ISREGIONENTRY_FIELD] = seg_to_update[ISREGIONENTRY_FIELD] or seg[ISREGIONENTRY_FIELD]
        seg_to_update[ISGAPREGION_FIELD] = seg_to_update[ISGAPREGION_FIELD] and seg[ISGAPREGION_FIELD]

    left = sorted(left)
    right = sorted(right)

    result = []  # type: list[list[int|bool]]
    l_idx = 0
    r_idx = 0
    while l_idx < len(left) or r_idx < len(right):
        if l_idx >= len(left):
            result.append(list(right[r_idx]))
            # update by last left not needed. Last left is always (*, *, 0, false, false, false)
            r_idx += 1
        elif r_idx >= len(right):
            result.append(list(left[l_idx]))
            # update by last right not needed. Last right is always (*, *, 0, false, false, false)
            l_idx += 1
        elif left[l_idx][:COUNTER_FIELD] == right[r_idx][:COUNTER_FIELD]:
            result.append(list(left[l_idx]))
            update_by(result[-1], right[r_idx])
            l_idx += 1
            r_idx += 1
        elif left[l_idx][:2] < right[r_idx][:2]:
            result.append(list(left[l_idx]))
            if r_idx > 0:
                update_by(result[-1], right[r_idx - 1])
            l_idx += 1
        else:
            result.append(list(right[r_idx]))
            if l_idx > 0:
                update_by(result[-1], left[l_idx - 1])
            r_idx += 1

    return result


def compact_segments(segments):
    segments = iter(segments)
    try:
        # See PEP 479 for more info
        start_ln, _, eln, _, start_state = next(segments)
    except StopIteration:
        return
    prev_ln = start_ln
    # empty segment at start
    if start_ln == eln:
        yield (start_ln, 0, start_ln, 0, 0)
        start_state = 1

    for sln, _, eln, _, state in segments:
        if (
            # covered state changed
            state != start_state
            # gap found
            or sln > prev_ln + 1
            # Special case for java and python
            # This empty segment represents missing branch in the line
            # and used for proper colorization - should be treated like separator
            or sln == eln
        ):
            yield (start_ln, 0, prev_ln + 1, 0, start_state)
            start_ln = sln
            start_state = state
            if sln == eln:
                assert state == 0
                yield (sln, 0, sln, 0, 0)
                # empty segments mean there will be covered after it
                start_state = 1
        prev_ln = sln

    yield (start_ln, 0, eln, 0, start_state)
