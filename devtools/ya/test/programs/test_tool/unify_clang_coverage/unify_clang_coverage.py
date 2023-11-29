import json

import argparse

from library.python.testing import coverage_utils as coverage_utils_library
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--raw-coverage-path', required=True)
    parser.add_argument('--prefix-filter')
    parser.add_argument('--exclude-regexp')

    args = parser.parse_args()
    return args


def setup_env(params):
    # create node's output
    with open(params.output, "w") as afile:
        afile.write("no valid output was provided")


def get_segments(segments):
    return [s.dump() for s in segments]


class Segment(object):
    def __init__(self, start_line, start_col, count, has_count, is_region_entry, is_gap):
        self.start_line = start_line - 1
        self.start_col = start_col - 1
        self.end_line = None
        self.end_col = None
        self.count = count
        self.has_count = has_count
        self.is_region_entry = is_region_entry
        self.is_gap = is_gap
        self.weight = 1

    def close(self, segment):
        self.end_line = segment.start_line
        self.end_col = segment.start_col

    def __str__(self):
        return 'Segment[{}-{}:{}-{}, c:{}, w:{}, hc:{}, ire:{}, gap: {}]'.format(
            self.start_line,
            self.start_col,
            self.end_line,
            self.end_col,
            self.count,
            self.weight,
            self.has_count,
            self.is_region_entry,
            self.is_gap,
        )

    def __repr__(self):
        return str(self)

    def dump(self):
        return [self.start_line, self.start_col, self.end_line, self.end_col, self.count]


def make_segment(s):
    assert s[0] and s[1], s
    return Segment(s[0], s[1], s[2], s[3], s[4], s[5] if len(s) > 5 else False)


def flatten_segments(raw_segments):
    def add_segment(segs, seg_cur, seg_end):
        seg_cur.close(seg_end)
        if seg_cur.count:
            seg_cur.count = 1
        segs.append(current)

    segments = []
    current = None
    for entry in raw_segments:
        seg = make_segment(entry)
        if seg.has_count and not seg.is_gap:
            # executable region
            if not current:
                current = seg
            else:
                if bool(seg.count) == bool(current.count):
                    # merge segments with "some" counts
                    current.weight += 1
                else:
                    add_segment(segments, current, seg)
                    current = seg
        else:
            if current is not None:
                add_segment(segments, current, seg)
                current = None

    return segments


def check_inter(left, right):
    if left[2] != right[0]:
        return left[2] < right[0]
    else:
        return left[3] <= right[1]


def transform_coverage_to_unify(filename, output, prefix_filter, exclude_regexp):
    file_filter = coverage_utils_library.make_filter(prefix_filter, exclude_regexp)

    unified_coverage = {}
    with open(filename, 'r') as afile:
        for cov_block_str in afile:
            cov_block = json.loads(cov_block_str)
            filename = cov_block.get("filename")
            if cov_block.get("coverage") and file_filter(filename):
                cov_block["coverage"]["segments"] = get_segments(flatten_segments(cov_block["coverage"]["segments"]))
                unified_coverage[filename] = cov_block["coverage"]

    lib_coverage.export.dump_coverage(unified_coverage, output)


def main():
    args = parse_args()
    setup_env(args)

    transform_coverage_to_unify(
        args.raw_coverage_path,
        args.output,
        args.prefix_filter,
        args.exclude_regexp,
    )


if __name__ == "__main__":
    exit(main())
