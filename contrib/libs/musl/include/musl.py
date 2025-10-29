#!/usr/bin/env python3


import os
import sys
import json


MUSL_LIBS = ['-lc', '-lcrypt', '-ldl', '-lm', '-lpthread', '-lrt', '-lutil', '-lresolv']


def fix_cmd_for_musl(cmd):
    for flag in cmd:
        if flag not in MUSL_LIBS:
            yield flag


if __name__ == '__main__':
    sys.stdout.write(json.dumps(list(fix_cmd_for_musl(sys.argv[1:]))))
