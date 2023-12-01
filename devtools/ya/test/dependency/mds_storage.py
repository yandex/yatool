import os
import logging

import exts.http_client

CANON_OUTPUT_STORAGE = 'canondata_storage'

logger = logging.getLogger(__name__)


class NoResourceInCacheException(Exception):
    pass


class MdsStorage(object):
    mds_prefix = "https://storage.yandex-team.ru/get-devtools/"

    def __init__(self, storage_root, use_cached_only=True):
        self._storage_root = storage_root
        self._use_cached_only = use_cached_only

    def get_resource_download_log_path(self, resource_id):
        return self.get_storage_resource_path(resource_id) + ".log"

    def get_storage_resource_path(self, resource_id):
        return os.path.join(self._storage_root, CANON_OUTPUT_STORAGE, resource_id)

    def get_resource(self, resource_id):
        resource_file = self.get_storage_resource_path(resource_id)

        if not os.path.exists(resource_file):
            if self._use_cached_only:
                raise NoResourceInCacheException("There is no suitable resource {} in cache".format(resource_id))
            exts.http_client.download_file(self.get_mds_url_by_key(resource_id), resource_file, mode=0o0644)

        for tar_archive_suffix in (".tar.gz", ".tar"):
            if resource_file.endswith(tar_archive_suffix):
                resource_dir = resource_file[: -len(tar_archive_suffix)]
                if not os.path.exists(resource_dir):
                    logger.debug("Extracting %s to %s", resource_id, resource_dir)
                    exts.archive.extract_from_tar(resource_file, resource_dir)
                return resource_dir

        return resource_file

    @classmethod
    def get_mds_url_by_key(cls, resource_id):
        return cls.mds_prefix + resource_id
