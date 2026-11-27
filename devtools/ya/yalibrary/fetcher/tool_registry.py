from dataclasses import dataclass
import logging
import os
import threading

import requests

import devtools.ya.core.config
from yalibrary.fetcher.uri_parser import parse_http_uri

logger = logging.getLogger(__name__)

_REGISTRY_CFG_PATH = "tools/internal"
_REGISTRY_ENV_NAME = "YA_TOOL_REGISTRY_NAME"
_REQUEST_TIMEOUT = 10


@dataclass(kw_only=True, frozen=True)
class Resource:
    resource_id: str | None
    integrity: str
    url: str


def is_enabled(bottle_name=None) -> bool:
    return _get_tool_registry().is_enabled(bottle_name)


def find_resource(bottle_name, query, platforms) -> Resource | None:
    return _get_tool_registry().find_resource(bottle_name, query, platforms)


def get_resource(resource_id) -> Resource | None:
    return _get_tool_registry().get_resource(resource_id)


class _ToolRegistry:
    _CFG_BASE_URL = "base_url"
    _CFG_RESOURCE_PATH_TEMPLATE = "resource_path_template"
    _CFG_BOTTLE_PATH_TEMPLATE = "bottle_path_template"
    _CFG_BOTTLES = "bottles"

    _INTEGRITY_HEADER = "x-amz-meta-integrity"
    _RESOURCE_ID_HEADER = "x-amz-meta-resource-id"

    def __init__(self):
        if not (registry_name := os.environ.get(_REGISTRY_ENV_NAME)):
            self._enabled = False
            return
        registry_cfg_path = _REGISTRY_CFG_PATH + "/" + registry_name + ".json"
        logger.debug("Try to read registry config: %s", registry_cfg_path)
        self._config = devtools.ya.core.config.get_tool_config(registry_cfg_path)
        logger.debug("Registry config is read successfully: %s", registry_cfg_path)

        self._enabled = True
        self._base_url = self._config[self._CFG_BASE_URL]
        self._resource_path_template = self._config[self._CFG_RESOURCE_PATH_TEMPLATE]
        self._bottle_path_template = self._config[self._CFG_BOTTLE_PATH_TEMPLATE]

    def is_enabled(self, bottle_name=None) -> bool:
        if not self._enabled:
            return False
        return bottle_name is None or bottle_name in self._config.get(self._CFG_BOTTLES, {})

    def find_resource(self, bottle_name, query, platforms) -> Resource | None:
        assert self.is_enabled(bottle_name)
        bottle_path_template = self._bottle_path_template
        bottle_config = self._config.get(self._CFG_BOTTLES, {}).get(bottle_name, {})
        if override_template := bottle_config.get(self._CFG_BOTTLE_PATH_TEMPLATE):
            bottle_path_template = override_template
        attributes = query.get("attributes", {}).copy()
        attributes.setdefault("version", "latest")
        for platform in platforms or ("any",):
            path = bottle_path_template.format(
                bottle=bottle_name, release=query.get("releases", "stable"), platform=platform, **attributes
            )
            if (result := self._get_resource_info(path)) is not None:
                return result
        return None

    def get_resource(self, resource_id) -> Resource | None:
        assert self._enabled
        path = self._resource_path_template.format(resource_id=resource_id)
        return self._get_resource_info(path, expected_resource_id=str(resource_id))

    def _get_resource_info(self, path: str, expected_resource_id=None) -> Resource | None:
        url = "/".join([self._base_url.rstrip("/"), path.lstrip("/")])
        response = requests.head(url, timeout=_REQUEST_TIMEOUT)
        if response.status_code == requests.codes.not_found:
            logger.debug("Resource is not found at '%s'", url)
            return None
        response.raise_for_status()

        integrity = response.headers.get(self._INTEGRITY_HEADER)
        parse_http_uri('{}#integrity={}'.format(url, integrity or ''))

        resource_id = response.headers.get(self._RESOURCE_ID_HEADER)
        if expected_resource_id is not None and resource_id != expected_resource_id:
            raise ValueError(
                "Resource id metadata mismatch for '{}': expected {}, got {}".format(
                    url, expected_resource_id, resource_id
                )
            )

        return Resource(resource_id=resource_id, integrity=integrity, url=url)


_tool_registry = None
_tool_registry_lock = threading.Lock()


def _get_tool_registry() -> _ToolRegistry:
    global _tool_registry
    if _tool_registry is not None:
        return _tool_registry
    with _tool_registry_lock:
        if _tool_registry is None:
            _tool_registry = _ToolRegistry()
        return _tool_registry
