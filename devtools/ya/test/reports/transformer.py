# coding=utf-8
import re


class TextTransformer(object):

    def __init__(self, replacements, use_re=False):
        self._replacements = replacements
        self._use_re = use_re

    def substitute(self, data):
        if not data:
            return data
        for key, value in self._replacements:
            if self._use_re:
                data = re.sub(key, value, data, re.MULTILINE | re.DOTALL | re.UNICODE)
            else:
                data = data.replace(key, value)
        return data

    def __str__(self):
        return "TextTransformer({}, {})".format(self._replacements, self._use_re)

    def __repr__(self):
        return str(self)
