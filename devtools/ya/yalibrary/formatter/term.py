# coding: utf-8

import threading

from . import palette
from . import formatter
from exts import func


CLEAR_TILL_END = '\033[K'


class TermSupport(formatter.BaseSupport):
    def __init__(self, scheme=None, profile=None):
        _scheme = palette.DEFAULT_TERM_SCHEME.copy()
        _scheme.update(scheme or {})
        super(TermSupport, self).__init__(_scheme, profile)
        self._skip_markers = [palette.Highlight.PATH]
        self._cur_marker = self.profile[palette.Highlight.RESET]
        self._lock = threading.Lock()

    @func.memoize()
    def get_color_code(self, name):
        from library.python import color

        return color.get_color_by_spec(name)

    @staticmethod
    def escape(txt):
        # fix for tmux <= 2.0ver
        # replace SO (Shift Out) symbol to avoid the corruption of the console.
        # safe for utf-8 - SO cannot be part of code point bytes, except itself
        return txt.replace("\x0e", " ")

    def format(self, txt):
        return TermSupport.escape(txt)

    def clear(self):
        return '\r' + CLEAR_TILL_END

    def decorate(self, marker, text):
        with self._lock:
            # do not change current highlight color for skip markers
            if marker not in self._skip_markers:
                self._cur_marker = marker
            if self.profile.get(self._cur_marker):
                return self.get_color_code(self.profile[self._cur_marker]) + text + self.get_color_code('reset')
            return text

    def colorize(self, color, text):
        return self.get_color_code(color) + text + self.get_color_code('reset')


def ansi_codes_to_markup(text):
    # type: (str) -> str
    """
    Preparing transformer to replace ANSI ESC color sequences in the text provided by markup markers
    @text: ANSI text to be converted
    """

    from library.python import color

    color_codes = {code: name for name, code in color.COLORS.items()}
    highlight_codes = {code: name for name, code in color.HIGHLIGHTS.items()}
    color_codes['default'] = 'default'
    modifiers = {code: name for name, code in {k: color.ATTRIBUTES[k] for k in ['light', 'dark']}.items()}

    # there are several specific markers that shouldn't
    # be converted from matching color
    skip_markers = [palette.Highlight.PATH]
    known_markup = {color: word for word, color in palette.DEFAULT_PALETTE.items() if word not in skip_markers}
    current_attributes = []

    def transformer(ansi_code, text):
        """
        Text processor will call transformer on every read step.
        On the read step it reads color code (@ansi_code) + all the plain text (@text) after it until next escape sequence.
        @ansi_code could contain more than one color, in this case the color codes will be in a CSV format with semicolon as a delimiter.
        """
        if ansi_code:
            codes = [int(c) for c in ansi_code.split(';')]

            for c in codes:
                if c in modifiers or c in color_codes or c in highlight_codes:
                    current_attributes.append(c)

            # No text with ansi_code might mean that we have a few markers one after another and
            # no text between those - we will combine current_attributes on the next step with
            # next marker and contiune until we have final call with empty ansi_code + empty text
            if not text:
                return ''
        elif ansi_code is not None:
            current_attributes.append(color.COLORS.get('reset'))

        # Produce the result markup
        result_text_prefix = ''

        # We could later add blink, italic, etc, so let's have a generic code
        # For each color in the list we are collecting all the attributes set before its position
        # Example: "\x1b[1m\x1b[31m SOME RED TEXT" => "[[c:light-red]] SOME RED TEXT"
        color_attr = set()
        # We need to process modifiers first, we could add two loops here, but sort() is enough
        current_attributes.sort()
        for code in current_attributes:
            if code in modifiers:
                color_attr.add(modifiers[code])
            if code in color_codes:
                result_color = '-'.join([mod_name for mod_name in color_attr] + [color_codes[code]])
                markup_color_id = palette.Highlight.RESET if result_color == 'reset' else 'c:%s' % result_color
                result_text_prefix += '[[%s]]' % known_markup.get(result_color, markup_color_id)
                color_attr.clear()
            if code in highlight_codes:
                result_text_prefix += '[[c:%s]]' % highlight_codes[code]
                color_attr.clear()

        # TODO: py2
        del current_attributes[:]

        return result_text_prefix + text

    from yalibrary.term import console

    return formatter.transform(text, transformer, lambda _: True, console.ecma_48_sgr_regex())
