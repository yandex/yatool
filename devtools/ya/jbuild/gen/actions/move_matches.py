import os
import optparse

from yalibrary import rglob


def parse_args():
    parser = optparse.OptionParser()
    parser.add_option('--src')
    parser.add_option('--dest')
    parser.add_option('--includes')
    parser.add_option('--excludes')
    parser.add_option('--log')

    opts, args = parser.parse_args()

    opts.includes = opts.includes.split(':') if opts.includes else []
    opts.excludes = opts.excludes.split(':') if opts.excludes else []

    return opts


def main():
    opts = parse_args()

    include_matches = []
    for p in opts.includes:
        matches = [os.path.relpath(x, opts.src) for x in rglob.rglob(os.path.join(opts.src, p)) if os.path.isfile(x)]
        include_matches.extend(matches)

    if not opts.includes:
        include_matches = sum(
            [[os.path.relpath(os.path.join(r, f), opts.src) for f in fs] for r, ds, fs in os.walk(opts.src)], []
        )

    exclude_matches = []
    for p in opts.excludes:
        matches = [os.path.relpath(x, opts.src) for x in rglob.rglob(os.path.join(opts.src, p)) if os.path.isfile(x)]
        exclude_matches.extend(matches)

    excluded = frozenset(exclude_matches)

    to_move = [p for p in sorted(set(include_matches)) if p not in excluded]

    for p in to_move:
        src = os.path.join(opts.src, p)
        dst = os.path.join(opts.dest, p)

        if not os.path.exists(os.path.dirname(dst)):
            os.makedirs(os.path.dirname(dst))

        if not os.path.exists(dst):
            os.rename(src, dst)


if __name__ == '__main__':
    main()
