from . import mk_common
import logging

logger = logging.getLogger(__name__)

STATE_MACRO_START = 10
STATE_MACRO_END = 20
STATE_VALUE = 30
STATE_VAL = 35
STATE_COMMENT = 40
STATE_WH = 60
STATE_VALUE_STRING = 70
STATE_NEXT_CHAR = 80


class ParserException(mk_common.MkLibException):
    pass


class Parser(object):
    def __init__(self, builder):
        self.builder = builder

    def parse_file(self, path):
        try:
            self.parse(read(path))
        except Exception:
            logger.error('Unable to parse file %s', path)
            raise

    def parse(self, raw_data):
        state = STATE_MACRO_START
        state_stack = []
        string = ''
        comment = ''
        bracer = ''
        i = 0
        while i < len(raw_data):
            c = raw_data[i]
            if c in ['\r', '\t']:
                i += 1
                continue

            while True:
                if state == STATE_MACRO_START:
                    if c in [' ', '\n']:
                        if c == '\n':
                            self.builder.on_new_line()
                        state_stack.append(state)
                        state = STATE_WH
                        break
                    elif c == '#':
                        state_stack.append(state)
                        state = STATE_COMMENT
                    elif c == '(':
                        if len(string) > 0:
                            self.builder.on_macro_start(string)
                            string = ''
                        else:
                            raise ParserException('Macros without name')
                        state = STATE_VALUE
                        break
                    else:
                        string += c
                        break
                elif state == STATE_VALUE:
                    if c in [' ', '#', '\n', ')']:
                        if len(string) > 0:
                            self.builder.on_value(string)
                            string = ''
                            bracer = ''

                        if c == '#':
                            state_stack.append(state)
                            state = STATE_COMMENT
                        else:
                            if c == '\n':
                                self.builder.on_value_new_line()
                            elif c == ')':
                                self.builder.on_macro_end()
                                state = STATE_MACRO_END
                            elif c == ' ':
                                state_stack.append(state)
                                state = STATE_WH
                            break
                    else:
                        state = STATE_VAL
                elif state == STATE_MACRO_END:
                    if c == ' ':
                        state_stack.append(state)
                        state = STATE_WH
                        break
                    elif c == '#':
                        state_stack.append(state)
                        state = STATE_COMMENT
                    else:
                        self.builder.on_new_line()
                        state = STATE_MACRO_START
                        if c == '\n':
                            break
                elif state == STATE_WH:
                    if c != ' ':
                        state = state_stack.pop()
                    else:
                        break
                elif state == STATE_VAL:
                    if c in [' ', '#', '\n', ')']:
                        state = STATE_VALUE
                    elif c in ['\'', '"']:
                        state = STATE_VALUE_STRING
                        bracer = c
                        string += c
                        break
                    elif c in ['\\']:
                        state_stack.append(state)
                        state = STATE_NEXT_CHAR
                        string += c
                        break
                    else:
                        string += c
                        break
                elif state == STATE_COMMENT:
                    if c == '\n':
                        state = state_stack.pop()
                        if state == STATE_VALUE:
                            self.builder.on_value_comment(comment)
                        else:
                            self.builder.on_comment(comment)
                        comment = ''
                    else:
                        comment += c
                        break
                elif state == STATE_VALUE_STRING:
                    if c == bracer:
                        state = STATE_VAL
                        string += c
                        break
                    if c == '\\':
                        state_stack.append(state)
                        state = STATE_NEXT_CHAR
                        string += c
                        break
                    else:
                        string += c
                        break
                elif state == STATE_NEXT_CHAR:
                    string += c
                    state = state_stack.pop()
                    break
            i += 1
        self.builder.on_global_end()


def read(path):
    with open(path, 'r') as f:
        lines = f.read()

    return lines
