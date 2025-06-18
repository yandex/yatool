# coding: utf-8

import sys
import os

import exts.yjson as json
import exts.fs
import shutil
import socket
import logging
import tempfile

if sys.version[0] == "3":
    from xmlrpc.client import ProtocolError
else:
    from xmlrpclib import ProtocolError


import exts.archive
import app_config

from exts import filelock
from exts import retry
from exts import fs
from exts import func
from exts import hashing


RESOURCE_INFO_JSON = "resource_info.json"
# this file will hold the resource and later in the test run will be renamed to the actual resource file name
RESOURCE_CONTENT_FILE_NAME = "resource"
RESOURCE_REAL_DIR_NAME = "real_resource_dir"
DIR_OUTPUTS_RESOURCE_DIR_NAME = "dir_res"
DIR_OUTPUTS_RESOURCE_TARED_CONTENT_FILE_NAME = "dir_res.tar"
DIRECTORY_MODE = "dir"
FILE_MODE = "file"

logger = logging.getLogger(__name__)


class DownloadException(Exception):
    pass


class NoResourceInCacheException(Exception):
    pass


class DirOutputsNotPreparedError(Exception):
    pass


class SandboxStorage(object):
    def __init__(
        self,
        storage_root,
        use_cached_only=False,
        download_methods=None,
        custom_fetcher=None,
        update_last_usage=True,
        fetcher_params=None,
        oauth_token=None,
    ):
        self._storage_root = storage_root
        self._sandbox = None
        self._use_cached_only = use_cached_only
        self._checked_resources = set([])
        self._custom_fetcher = custom_fetcher
        self._sandbox_client = None  # Lazy initialization
        self._update_last_usage = update_last_usage
        self._oauth_token = oauth_token
        self._fetcher_params = fetcher_params

    def get(self, resource_id, decompress_if_archive=False, rename_to=None, resource_file=None):
        """
        Return StoredResourceInfo object for the resource from storage
        :param resource_id: resource id
        :param resource_file: downloaded resource file RESOURCE_CONTENT_FILE_NAME with accompanying RESOURCE_INFO_JSON in the same directory
        """
        if not self._sandbox_client and app_config.in_house:
            import devtools.ya.yalibrary.yandex.sandbox as sandbox

            logger.debug("Initializing sandbox client")
            self._sandbox_client = sandbox.SandboxClient(
                token=self._oauth_token,
            )
        logger.debug("Getting sandbox resource id %s from storage %s", resource_id, self._storage_root)
        with filelock.FileLock(os.path.join(self._storage_root, str(resource_id) + ".lock")):
            if self._update_last_usage:
                logger.debug("Update resource last usage time")
                try:
                    self._sandbox_client.touch_resource(resource_id)
                except Exception as e:
                    logger.warning("Error while updating resource %s last usage: %s", resource_id, e)
            # Search for cached resources provided by runner
            resource = self._get_cached_resource(resource_id, decompress_if_archive)
            if not resource:
                resource = self._download_resource(
                    resource_id, decompress_if_archive, rename_to, resource_file=resource_file
                )
        return StoredResourceInfo(resource)

    def get_sandbox_fetcher_output_paths(self, resource_id):
        return [
            self._get_resource_info_path(resource_id),
            self._get_resource_content_file_path(resource_id),
        ]

    @classmethod
    def get_resource_download_log_path(cls, build_root, resource_id):
        return os.path.join(build_root, "res{}.log".format(resource_id))

    @classmethod
    def get_storage_resource_path(cls, storage_root, resource_id):
        """
        Get the expected resource path in storage
        :param storage_root: storage root path
        :param resource_id: resource id
        """
        return cls._get_storage_path(storage_root, str(resource_id))

    @classmethod
    def get_storage_tared_resource_path(cls, storage_root, resource_id):
        """
        Get the expected tared resource path in the storage
        :param storage_root: storage root
        :param resource_id: resource id
        """
        return os.path.join(cls.get_storage_resource_path(storage_root, resource_id), "resource.tar")

    def get_dir_outputs_resource_dir_path(cls, resource_id):
        """
        Get untared dir_output resource content in storage
        :param resource_path: path to resource directory in storage
        """
        return cls._get_resource_path(resource_id, DIR_OUTPUTS_RESOURCE_DIR_NAME)

    def get_dir_outputs_resource_tar_path(cls, resource_id):
        """
        Get untared dir_output resource content in storage
        :param resource_path: path to resource directory in storage
        """
        return cls._get_resource_path(resource_id, DIR_OUTPUTS_RESOURCE_TARED_CONTENT_FILE_NAME)

    def is_dir_outputs_file_mode(self, resource_info):
        return resource_info.get("dir_outputs_mode", "") == FILE_MODE

    @staticmethod
    def progress_callback(_, __):
        logger.debug("Downloading")

    def is_resource_prepared_for_dir_outputs(self, resource_id):
        return os.path.exists(self.get_dir_outputs_resource_tar_path(resource_id))

    def dir_outputs_process_prepared_resource(self, resource, build_root):
        resource_id = resource.get_id()
        resource_path = self.get_storage_resource_path(build_root, resource_id)
        resource_renamed_path = self._get_resource_content_file_path(resource_id)
        resource_tar_path = self.get_dir_outputs_resource_tar_path(resource_id)
        resource_dir_path = self.get_dir_outputs_resource_dir_path(resource_id)
        resource_content_path = self._get_resource_content_path(resource_path)
        resource_info_path = self._get_resource_info_path(resource_id)
        with open(resource_info_path, 'r') as afile:
            resource_info = json.load(afile)

        # resource is file
        if self.is_dir_outputs_file_mode(resource_info):
            os.remove(resource_tar_path)
            if os.path.exists(resource_dir_path):
                shutil.rmtree(resource_dir_path)
            return
        resource_original_path = self._get_resource_content_path(resource_path, resource_info[RESOURCE_REAL_DIR_NAME])
        if not os.path.exists(resource_dir_path):
            exts.archive.extract_from_tar(resource_tar_path, resource_content_path)
        else:
            exts.fs.copytree3(resource_dir_path, resource_original_path, copy_function=os.link)
        os.remove(resource_tar_path)
        os.remove(resource_renamed_path)

    def dir_outputs_prepare_downloaded_resource(self, resource, resource_id, dir_outputs_in_runner=True):
        resource_path = self._get_resource_content_file_path(resource_id)
        resource_dir = self._get_resource_path(resource_id)
        dir_output_tared_path = self.get_dir_outputs_resource_tar_path(resource_id)
        dir_output_path = self.get_dir_outputs_resource_dir_path(resource_id)

        resource_info_path = self._get_resource_info_path(resource_id)
        with open(resource_info_path, 'r') as afile:
            resource_info = json.load(afile)
        if resource["multifile"] and exts.archive.is_archive_type(resource_path):
            resource_info["dir_outputs_mode"] = DIRECTORY_MODE
        else:
            resource_info["dir_outputs_mode"] = FILE_MODE
        # resource is directory
        if resource_info["dir_outputs_mode"] == DIRECTORY_MODE:
            resource_info[RESOURCE_REAL_DIR_NAME] = resource["file_name"]
            os.link(resource_path, dir_output_tared_path)
            if dir_outputs_in_runner:
                # local case when dir_outputs in runner enabled
                #     <resource_num>
                #     ├── resource - empty file
                #     ├── dir_res.tar - archive with resource content
                #     ├── dir_res - directory with archive content
                #     └── resource_info.json
                self.get(resource_id)
                original_path = self.get_dir_outputs_original_path(resource, resource_id, resource_dir, resource_info)
                shutil.move(original_path, dir_output_path)
            else:
                os.remove(resource_path)
            # content of 'resource' is duplicated by dir_res.tar but we need to save 'resource' file because of graph outputs.
            # so we make 'resource' file empty
            open(resource_path, 'w').close()
        else:
            # resource is file -> dir outputs not needed
            #     <resource_num>
            #     ├── resource - resource file
            #     ├── dir_res.tar - empty tar - needed by graph outputs
            #     ├── dir_res - empty dir - needed by graph outputs
            #     └── resource_info.json
            os.mkdir(dir_output_path)
            open(os.path.join(dir_output_path, "empty"), 'w').close()
            exts.archive.create_tar(dir_output_path, dir_output_tared_path)
        with open(resource_info_path, 'w') as afile:
            json.dump(resource_info, afile)

    def _get_resource_info_from_file(self, resource_file):
        if resource_file is None or not os.path.exists(resource_file):
            return None

        logger.debug("Trying to reuse externally provided resource in %s", resource_file)

        resource_dir, resource_file = os.path.split(resource_file)
        if resource_file != RESOURCE_CONTENT_FILE_NAME:
            return None

        resource_json = os.path.join(resource_dir, RESOURCE_INFO_JSON)
        if not os.path.isfile(resource_json):
            return None

        try:
            with open(resource_json, 'r') as j:
                return json.load(j)
        except Exception:
            logging.debug('Invalid %s in %s', RESOURCE_INFO_JSON, resource_dir)

        return None

    def _get_raw_resource_data(self, resource_id, download_resource_path, resource_file):
        """
        :param resource_file: downloaded resource file RESOURCE_CONTENT_FILE_NAME with accompanying RESOURCE_INFO_JSON in the same directory
        """
        resource_info = self._get_resource_info_from_file(resource_file)
        external_file = resource_info is not None
        if not external_file:
            resource_info = self._sandbox_client.get_resource(resource_id)

        downloaded_file_path = os.path.join(download_resource_path, resource_info["file_name"])

        if external_file:
            logger.debug("Found resource %s at %s, moving to %s", resource_id, resource_file, downloaded_file_path)
            target_dir = os.path.dirname(downloaded_file_path)
            if not os.path.exists(target_dir):
                os.makedirs(target_dir)
            exts.fs.copy_tree(resource_file, downloaded_file_path, copy_function=exts.fs.hardlink_or_copy)
        else:
            if self._use_cached_only:
                raise NoResourceInCacheException(
                    "Resource {} was not found in local storages and won't be downloaded".format(resource_id)
                )

            from devtools.ya.yalibrary.yandex.sandbox import fetcher

            logger.debug("Will download resource %s to %s", resource_id, downloaded_file_path)
            fetcher.download_resource(
                resource_id,
                downloaded_file_path,
                custom_fetcher=self._custom_fetcher,
                progress_callback=self.progress_callback,
                sandbox_token=self._oauth_token,
                fetcher_params=self._fetcher_params,
            )

        return resource_info, downloaded_file_path, external_file

    # ~315 sec within 35 retries
    @retry.retrying(
        max_times=35,
        ignore_exception=lambda e: isinstance(e, (DownloadException, ProtocolError, socket.error)),
        retry_sleep=lambda i, t: i / 2.0,
    )
    def _download_resource(self, resource_id, decompress_if_archive, rename_to, resource_file):
        """
        :param resource_file: downloaded resource file RESOURCE_CONTENT_FILE_NAME with accompanying RESOURCE_INFO_JSON in the same directory
        """
        resource_path = self._get_resource_path(resource_id)
        if os.path.exists(resource_path):
            exts.fs.remove_tree(resource_path)
        os.makedirs(resource_path)
        download_resource_path = tempfile.mkdtemp(prefix="download.", dir=resource_path)
        resource_temp_content_path = tempfile.mkdtemp(prefix="content.", dir=resource_path)

        resource_info, downloaded_file_path, external_file = self._get_raw_resource_data(
            resource_id, download_resource_path, resource_file
        )

        if downloaded_file_path.endswith(".tar.gz") or downloaded_file_path.endswith(".tar"):
            logger.debug("%s is an archive - will unpack it", downloaded_file_path)
            if decompress_if_archive:
                self._extract_from_tar(downloaded_file_path, resource_temp_content_path)
                exts.fs.remove_tree(download_resource_path)
            else:
                shutil.move(downloaded_file_path, resource_temp_content_path)
        else:
            logger.debug("%s is not an archive - check if it's a correct file", downloaded_file_path)
            fs.replace(download_resource_path, resource_temp_content_path)

        resource_content_path = self._get_resource_content_path(resource_path)
        fs.replace(resource_temp_content_path, resource_content_path)

        resource_path = self._get_resource_content_path(resource_path)

        if resource_info.get("executable", False):
            try:
                exts.fs.set_execute_bits(resource_path)
            except Exception as e:
                logging.error("Failed to set execute bits '%s': %s", resource_path, e)

        resource_info["path"] = resource_path
        if rename_to:
            rename_to_path = self._get_resource_path(resource_id, rename_to)
            rename_from_path = os.path.join(resource_info["path"], resource_info["file_name"])
            if resource_info.get("multifile") and os.path.isdir(rename_from_path):
                exts.archive.create_tar([(rename_from_path, os.path.basename(rename_from_path))], rename_to_path)
                exts.fs.remove_tree(rename_from_path)
            else:
                if not external_file:
                    if "md5" in resource_info:
                        downloaded_md5 = hashing.md5_path(rename_from_path)
                        assert (
                            downloaded_md5 == resource_info["md5"]
                        ), "Downloaded resource md5 mismatch (expected {}, got {})".format(
                            resource_info["md5"], downloaded_md5
                        )
                    else:
                        logger.warning("Skipped resource verification - no checksum was provied: %s", resource_info)
                fs.replace(rename_from_path, rename_to_path)
            resource_info["rename_to"] = rename_to

        with open(self._get_resource_info_path(resource_id), "w") as info_file:
            json.dump(resource_info, info_file, indent=4)

        return resource_info

    def _get_cached_resource(self, resource_id, decompress_if_archive, verify=False):
        logger.debug("Verify if there is a valid copy of resource %s", resource_id)
        info_path = self._get_resource_info_path(resource_id)

        res_path = self._get_resource_path(resource_id)
        tar_path = self._get_resource_tared_path(resource_id)
        if not os.path.exists(info_path) and os.path.exists(tar_path):
            self._extract_from_tar(tar_path, res_path)

        if os.path.exists(info_path):
            logger.debug("Resource json data file found by '%s'", info_path)
            with open(info_path) as info_file:
                try:
                    resource_info = json.load(info_file)
                    if "md5" not in resource_info and "file_md5" in resource_info:
                        resource_info["md5"] = resource_info["file_md5"]
                    resource_info['path'] = self._get_resource_content_path(res_path)

                    if resource_info.get("rename_to"):
                        renamed_path = self._get_resource_path(resource_id, resource_info["rename_to"])
                        if os.path.exists(renamed_path):
                            if resource_info.get("multifile"):
                                logger.debug("Extracting %s to %s", renamed_path, resource_info['path'])
                                exts.archive.extract_from_tar(renamed_path, resource_info['path'])
                                fs.remove_file(renamed_path)
                            else:
                                original_path = self._get_resource_content_path(
                                    self._get_resource_path(resource_id), resource_info["file_name"]
                                )
                                if verify:
                                    logger.debug("Verifying resource checksum %s", renamed_path)
                                    stored_md5 = hashing.md5_path(renamed_path)
                                    assert (
                                        stored_md5 == resource_info["md5"]
                                    ), "Downloaded resource md5 mismatch (expected {}, got {})".format(
                                        resource_info["md5"], stored_md5
                                    )
                                logger.debug(
                                    "Resource was renamed to %s, rename it back to %s", renamed_path, original_path
                                )
                                exts.fs.create_dirs(os.path.dirname(original_path))
                                exts.fs.replace(renamed_path, original_path)
                        elif os.path.exists(self._get_resource_path(resource_id, DIR_OUTPUTS_RESOURCE_DIR_NAME)):
                            logger.debug("Resource was already untared with dir_outputs")
                        else:
                            logger.warning("Renamed path %s does not exist", renamed_path)

                    resource_file_path = os.path.join(resource_info['path'], resource_info["file_name"])

                    if os.path.exists(resource_file_path):  # it can be deleted if the resource was tar.gz
                        if (
                            resource_file_path.endswith(".tar.gz") or resource_file_path.endswith(".tar")
                        ) and decompress_if_archive:
                            logger.debug("%s is an archive - will unpack it", resource_file_path)
                            exts.archive.extract_from_tar(resource_file_path, resource_info['path'])
                            os.remove(resource_file_path)

                    return resource_info
                except ValueError as e:
                    logger.error("Cannot deserialize json by '%s': '%s'", info_path, e)
                    return None

        logger.debug("Resource json data file not found by '%s'", info_path)
        return None

    def _extract_from_tar(self, tar_path, resource_path):
        temp_dir = tempfile.mkdtemp(prefix="from_tar.", dir=resource_path)

        exts.archive.extract_from_tar(tar_path, temp_dir)

        for path in os.listdir(temp_dir):
            shutil.move(os.path.join(temp_dir, path), resource_path)

    def _get_path(self, *path):
        return os.path.join(self._get_storage_path(self._storage_root), *path)

    @classmethod
    def _get_storage_path(cls, storage_root, *path):
        return os.path.join(storage_root, "sandbox-storage", *path)

    def _get_resource_path(self, resource_id, *path):
        return os.path.join(self.get_storage_resource_path(self._storage_root, resource_id), *path)

    def _get_resource_tared_path(self, resource_id):
        return self.get_storage_tared_resource_path(self._storage_root, resource_id)

    def _get_resource_info_path(self, resource_id):
        return self._get_resource_path(resource_id, RESOURCE_INFO_JSON)

    def _get_resource_content_file_path(self, resource_id):
        return self._get_resource_path(resource_id, RESOURCE_CONTENT_FILE_NAME)

    def _get_resource_content_path(self, resource_path, *path):
        return os.path.join(resource_path, "content", *path)

    def get_dir_outputs_original_path(self, resource, resource_id, resource_dir, resource_info):
        original_path = self._get_resource_content_path(resource_dir, resource["file_name"])
        if not os.path.exists(original_path):
            logger.error(
                "file_name in resource {} description is invalid. Trying to fallback on mds filename".format(
                    resource_id
                )
            )
            mds_filename = resource_info.get("mds", {}).get("key", "")
            if mds_filename:
                _, _, mds_filename = mds_filename.partition("/")
                original_path = self._get_resource_content_path(resource_dir, mds_filename)
            if not os.path.exists(original_path):
                _, _, mds_filename = resource["file_name"].partition("/")
                original_path = self._get_resource_content_path(resource_dir, mds_filename)
            if not os.path.exists(original_path):
                mds_filename = os.path.basename(resource["file_name"])
                original_path = self._get_resource_content_path(resource_dir, mds_filename)
            if os.path.exists(original_path):
                resource_info[RESOURCE_REAL_DIR_NAME] = mds_filename
            else:
                logger.error(
                    "If you see that message please contact with DEVTOOLSSUPPORT.\nFailed to fallback on mds, trying to find resource directory in content dir..."
                )
                content_path = self._get_resource_content_path(resource_dir)
                real_path = os.listdir(content_path)[0]
                original_path = self._get_resource_content_path(resource_dir, real_path)
                resource_info[RESOURCE_REAL_DIR_NAME] = real_path
        else:
            resource_info[RESOURCE_REAL_DIR_NAME] = resource["file_name"]
        return original_path


class StoredResourceInfo(object):
    def __init__(self, sandbox_info):
        self._info = sandbox_info

    def __str__(self):
        return json.dumps(self._info, indent=4)

    def __repr__(self):
        return str(self)

    def __getitem__(self, item):
        return self._info.__getitem__(item)

    @property
    def id(self):
        return int(self._info["id"])

    @property
    def path(self, *relative_paths):
        return os.path.join(self._info["path"], *relative_paths)

    @property
    def arc_path(self):
        if "attrs" in self._info:
            return self._info.get("attrs", {}).get("arc_path")
        return self._info.get("attributes", {}).get("arc_path")


@func.lazy
def get_sandbox_storage(storage_root, custom_fetcher, oauth_token):
    return SandboxStorage(storage_root, use_cached_only=False, custom_fetcher=custom_fetcher, oauth_token=oauth_token)
