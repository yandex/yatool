import os
import re
import logging

import json
from devtools.ya.yalibrary.yandex.sandbox.misc import consts as sandbox_const

from .exc import ExternalFileException

RESOURCE_LINK_PATTERN = r"{}/resource/[0-9]+/view".format(re.escape(sandbox_const.DEFAULT_SANDBOX_URL))
DOWNLOAD_LINK_PATTERN = r"{}/[0-9]+".format(re.escape(sandbox_const.DEFAULT_SANDBOX_PROXY_URL))


# https://st.yandex-team.ru/DEVTOOLS-7768
FILE_NAME_FIELD_REQUIRED = True


class ExternalFile:
    EXTENSION = '.external'

    def __init__(self, project_path, file_name):
        """
        @type file_name: str
        @type project_path: str
        @param file_name: Relative path to file
        @param project_path: Path to project
        """
        assert os.path.abspath(project_path), "project_path must be absolute path"
        assert os.path.relpath(file_name), "file_name must be relative path"

        self.project_path = project_path

        self.file_path = file_name[:-len(self.EXTENSION)] if file_name.endswith(self.EXTENSION) else file_name
        """ Relative or absolute path to original file """

        self.orig_file_path = os.path.join(self.project_path, self.file_path) if self.project_path else self.file_path
        """ Path to original file """

        self.local_file_name = os.path.basename(self.orig_file_path)
        """ Original file name """

        self.external_file_path = "{}{}".format(self.orig_file_path, self.EXTENSION)
        """ Path to .external file"""

        self.logger = logging.getLogger("ExternalFile")
        self.logger.debug("Init from {}".format(self.external_file_path))

    @property
    def relative_file_path(self):
        """Relative path to original file"""
        if self.project_path:
            return self.file_path
        raise ValueError("Can't determine relative path because parameter `project_path` not specified")

    @staticmethod
    def _search_project_path(path):
        ya_make_file = "ya.make"
        while True:
            if os.path.exists(os.path.join(path, ya_make_file)):
                return path
            path_ = os.path.dirname(path)
            if path == path_:
                return None
            path = path_

    @classmethod
    def find_externals(cls, path):
        """
        @type path: str
        @rtype: typing.Iterable[ExternalFile]
        """
        for entry in os.listdir(path):
            entry = os.path.join(path, entry)
            if os.path.isfile(entry) and entry.endswith(cls.EXTENSION):
                ext_file = cls.create_from_path(entry)
                if ext_file:
                    yield ext_file

    @classmethod
    def create_from_path(cls, abs_path_to_file):
        """

        @type abs_path_to_file: str
        @return: ExternalFile | None
        """
        project_path = cls._search_project_path(abs_path_to_file)
        if project_path is None:
            return None

        ext_file_path = os.path.join(os.path.relpath(abs_path_to_file, project_path))
        return ExternalFile(project_path, ext_file_path)

    @property
    def resource_file_name(self):
        return self.external_info().get('file_name', None) or self.local_file_name

    @property
    def orig_exist(self):
        return os.path.exists(self.orig_file_path)

    @property
    def external_exist(self):
        return os.path.exists(self.external_file_path)

    def _validate_external_content(self, data):
        try:
            storage = data["storage"]
            for k in ["resource_id", "download_link"]:
                if k not in data:
                    raise ExternalFileException("Key {} not found".format(k))

            if storage == "SANDBOX":
                for k in ["resource_link", "md5", "skynet_id"]:
                    if k not in data:
                        raise ExternalFileException("Key {} not found".format(k))

                if not data['skynet_id'].startswith("rbtorrent:"):
                    raise ExternalFileException("Parameter `skynet_id` should start with `rbtorrent:`")

                for k in ["resource_id"]:
                    if not isinstance(data[k], int) or (isinstance(data[k], str) and not data[k].isdigit()):
                        raise ExternalFileException("Parameter `resource_id` should be integer")

                if not FILE_NAME_FIELD_REQUIRED:
                    if 'file_name' in data:
                        if data['file_name'] != self.local_file_name:
                            raise ExternalFileException(
                                "Parameter `file_name` should be equal to "
                                ".external file name (`{}` / `{}`)".format(
                                    data['file_name'], self.local_file_name
                                ))
                else:
                    if 'file_name' not in data:
                        raise ExternalFileException("Parameter `file_name` is required")
                    if data['file_name'] != self.local_file_name:
                        raise ExternalFileException(
                            "Parameter `file_name` should be equal to "
                            ".external file name (`{}` / `{}`)".format(
                                data['file_name'], self.local_file_name
                            ))

            elif storage == "MDS":
                for k in ["namespace", "sha1"]:
                    if k not in data:
                        raise ExternalFileException("Key {} not found".format(k))

            if storage not in ("SANDBOX", "MDS"):
                raise ExternalFileException("Unknown storage type: {}".format(storage))

            patterns = []

            if storage == "SANDBOX":
                patterns.extend((
                    ('resource_link', RESOURCE_LINK_PATTERN),
                    ('download_link', DOWNLOAD_LINK_PATTERN)
                ))
            elif storage == "MDS":
                download_link_pattern_mds = r"https:\/\/.*/get-{}/{}".format(data["namespace"], data["resource_id"])
                patterns.append(
                    ('download_link', download_link_pattern_mds)
                )
            else:
                raise NotImplementedError()

            for field, pattern in patterns:
                if not (re.match(pattern, data[field])):
                    raise ExternalFileException("Parameter `{}` should match the pattern `{}`".format(
                        field, pattern
                    ))

        except ExternalFileException as e:
            e.path = self.external_file_path
            raise

    def external_info(self):
        """

        @rtype: dict
        """
        if not self.external_exist:
            raise ExternalFileException("External file doesn't exist", path=self.external_file_path)

        with open(self.external_file_path) as f:
            js = json.load(f)

        self._validate_external_content(js)

        return js

    def update_external(self, data):
        """
        @type data: dist
        """

        self._validate_external_content(data)

        with open(self.external_file_path, 'w') as f:
            json.dump(data, f, indent=4, sort_keys=True, separators=(',', ': '))
            f.write('\n')

    def __repr__(self):
        return "<LargeFile:{}>".format(self.external_file_path)
