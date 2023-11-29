import logging
import os

import exts.yjson as json


class ChangeListException(Exception):
    def __init__(self, msg, args=None, e=None):
        self.message = msg
        self.args = args
        self.e = e


class ChangeList:
    def __init__(self, filename):
        self.filename = filename
        self.logger = logging.getLogger("ChangeList:{}".format(self.filename))
        if not os.path.exists(self.filename):
            self.logger.warning("Changle list file not found: %s", self.filename)
            raise ChangeListException("File not found", args=(self.filename,))

        with open(self.filename, "rt") as f:
            self.raw_data = json.load(f)

    @property
    def paths_iter(self):
        try:
            items = self.raw_data[0]['names']
            for item in items:
                yield item['path']
        except Exception as e:
            self.logger.exception("Wrong change list structure")
            raise ChangeListException("Wrong change list structure", args=(self.filename,), e=e)

    @property
    def paths(self):
        return tuple(self.paths_iter)
