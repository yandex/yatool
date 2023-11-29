from __future__ import print_function
import sys


def main():
    assert len(sys.argv) == 2
    path = sys.argv[1]
    if not path.endswith('.vet.txt'):
        path += '.vet.txt'

    with open(path, 'r') as f:
        report = f.read()
    if len(report) > 0:
        print(report, file=sys.stderr)
        return 1
    else:
        return 0
