import sys
from getopt import getopt
from . import hello


def main():
    opts, args = getopt(sys.argv[1:], 'Cc:')
    capitalize = False
    color = None

    for opt, val in opts:
        if opt == '-C':
            capitalize = True
        elif opt == '-c':
            color = val

    hello.say_hello(capitalize, color)
