import logging
import os
import requests
import sys
import time
from collections import namedtuple
from copy import deepcopy
from dataclasses import dataclass, field
from hashlib import md5

import exts.yjson as json
import yalibrary.fetcher.progress_info as progress_info_lib
import yalibrary.fetcher.tool_registry as tool_registry
from devtools.libs.universal_fetcher.py import IntegrityError as UniversalFetcherIntegrityError
from exts import fs, http_client
from library.python import func
from yalibrary import guards
from yalibrary import platform_matcher
from yalibrary import toolscache

from devtools.ya.core import config

from yalibrary.fetcher.uri_parser import parse_resource_uri

from .common import BINARY, stringify_memoize, ProgressPrinter, get_sandbox_token
from .cache_helper import (
    install_symlink,
    installed,
    safe_resource_lock,
)
from .resource_fetcher import (
    fetch_resource_if_need,
)

logger = logging.getLogger(__name__)


_SB_REST_API_TIMEOUT = 1 * 60
_RESERVED_ATTRIBUTES = frozenset(
    {
        "platform",
    }
)


class ResourceNotFound(RuntimeError):
    mute = True


class UnsupportedToolRegistryQueryError(RuntimeError):
    mute = True


@dataclass(kw_only=True, frozen=True)
class ExtraQuery:
    releases: str | None = None
    attributes: dict[str, str | None] = field(default_factory=dict)

    def __post_init__(self):
        for k in self.attributes:
            if k in _RESERVED_ATTRIBUTES:
                raise ValueError(f"Attribute {k} cannot be redefined")
        # Normalize key order for self.digest() and stringify_memoize()
        object.__setattr__(
            self,
            'attributes',
            dict(sorted(self.attributes.items())),
        )

    def __bool__(self):
        return self.releases is not None or bool(self.attributes)

    def digest(self) -> str:
        return md5(str(self).encode()).hexdigest()[:10]


@stringify_memoize
def get_tool_chain_fetcher(
    root,
    toolchain_name,
    bottle_name,
    formula,
    for_platform=None,
    binname=None,
    force_refetch=False,
    download_fn=fetch_resource_if_need,
    extra_query: ExtraQuery | None = None,
):
    if for_platform is None:
        for_platform = platform_matcher.my_platform()
    formula = get_formula_value(formula)
    platform_replacements = formula.get("platform_replacements")
    if "by_platform" in formula:
        return _ToolChainByPlatformFetcher(
            root,
            toolchain_name,
            bottle_name,
            platform_replacements,
            formula["by_platform"],
            for_platform,
            binname,
            force_refetch,
            download_fn,
        )
    elif "latest_matched" in formula:
        return _ToolChainLatestMatchedResourceFetcher(
            root,
            toolchain_name,
            platform_replacements,
            formula["latest_matched"],
            for_platform,
            binname,
            force_refetch,
            download_fn,
            bottle_name,
            extra_query,
        )
    else:
        raise Exception("Unsupported formula: {}".format(formula))


def resolve_resource_id(root, toolchain_name, bottle_name, formula, platform):
    return get_tool_chain_fetcher(root, toolchain_name, bottle_name, formula, platform).resource_id_from_cache()


def get_formula_value(formula):
    '''Return the value directly or read from a file if the value is a file name'''
    if isinstance(formula, str):
        return config.config_from_arc_rel_path(formula)
    else:
        return formula


def get_formula_platforms(formula):
    formula = get_formula_value(formula)
    if "by_platform" in formula:
        return list(formula["by_platform"].keys())
    else:
        return []


_ToolChainInfo = namedtuple('_ToolChainInfo', 'where')


@dataclass(kw_only=True, frozen=True)
class _ResolvedResource:
    resource_id: int | str | None = None
    integrity: str | None = None
    url: str | None = None
    direct_uri: str | None = None

    @property
    def is_tool_registry_resource(self):
        return self.url is not None

    def public_uri(self):
        if self.direct_uri is not None:
            return self.direct_uri
        if self.url is not None:
            return '{}#integrity={}'.format(self.url, self.integrity)
        return 'sbr:{}'.format(self.resource_id)

    def download_uri(self):
        return self.public_uri()


def _normalize_registry_resource_id(resource_id):
    if isinstance(resource_id, str) and resource_id.isdecimal():
        return int(resource_id)
    return resource_id


class _ToolChainFetcherBase(object):
    def fetch_if_need(self):
        raise NotImplementedError()

    def resource_id_from_cache(self):
        raise NotImplementedError()


class _ToolChainFetcherImplBase(_ToolChainFetcherBase):
    def __init__(self, root, toolchain_name, platform_replacements, for_platform, binname, force_refetch, download_fn):
        self._root = root
        self._where = None
        self._toolchain_name = toolchain_name
        self._platform_replacements = platform_replacements
        self._platform = for_platform
        self._binname = binname
        self._binname_kwargs = dict(install_params=(BINARY, True), binname=binname) if binname else {}
        self._force_refetch = force_refetch
        self._download_fn = download_fn

    @stringify_memoize(cache_kwarg='cache')
    def fetch_if_need(self):
        if self._force_refetch or not self._check():
            logger.debug(
                "{0}: try to fetch by {1} for '{2}'".format(self._toolchain_name, self._details(), self._platform)
            )
            self._where = self._fetch()
            logger.debug("{0}: successfully fetched into {1}".format(self._toolchain_name, self._where))
        return _ToolChainInfo(self._where)

    def _check(self):
        if self._where is not None:
            toolscache.notify_tool_cache(self._where)
            return installed(self._where)
        return False

    def _fetch(self):
        raise NotImplementedError()

    def _details(self):
        raise NotImplementedError()


class _ToolChainByPlatformFetcher(_ToolChainFetcherImplBase):
    def __init__(
        self,
        root,
        name,
        bottle_name,
        platform_replacements,
        by_platform,
        for_platform,
        binname,
        force_refetch,
        download_fn,
    ):
        super(_ToolChainByPlatformFetcher, self).__init__(
            root, name, platform_replacements, for_platform, binname, force_refetch, download_fn
        )
        self.__by_platform = by_platform
        self.__tool_registry_enabled = tool_registry.is_enabled(bottle_name)

    def resource_id_from_cache(self):
        _, resource = self._get_resolved_resource()
        return resource.public_uri()

    def _get_resolved_resource(self):
        resource_desc = self._get_matched_resource()
        resource_uri = self._get_resource_uri(resource_desc)
        resource = self._get_tool_registry_resource(resource_uri)
        return resource_desc, resource or _ResolvedResource(direct_uri=resource_uri)

    def _get_matched_resource(self):
        best_match = platform_matcher.match_platform(
            self._platform, list(self.__by_platform.keys()), self._platform_replacements
        )
        if best_match is not None:
            logger.debug("{0}: will use '{1}' platform".format(self._toolchain_name, best_match))
            return self.__by_platform[best_match]
        else:
            raise platform_matcher.PlatformNotSupportedException(
                '{}: platform ({}) not matched to any of {}'.format(
                    self._toolchain_name, self._platform, self.__by_platform.keys()
                )
            )

    def _get_resource_uri(self, resource_desc):
        return resource_desc['uri']

    def _get_tool_registry_resource(self, resource_uri):
        if not self.__tool_registry_enabled or config.has_mapping():
            return None
        parsed_uri = parse_resource_uri(resource_uri)
        if parsed_uri.resource_type != 'sbr':
            return None
        resource = tool_registry.get_resource(parsed_uri.resource_id)
        if resource is None:
            raise ResourceNotFound(
                '{}: resource {} is not found in tool registry'.format(self._toolchain_name, parsed_uri.resource_id)
            )
        return _ResolvedResource(
            resource_id=_normalize_registry_resource_id(resource.resource_id),
            integrity=resource.integrity,
            url=resource.url,
        )

    def _details(self):
        return self.__by_platform

    def _fetch(self):
        resource_desc, resource = self._get_resolved_resource()
        resource_uri = resource.download_uri()
        strip_prefix = resource_desc.get('strip_prefix')
        fetcher, progress_callback = _get_fetcher(self._toolchain_name)
        res = self._download_fn(
            fetcher,
            self._root,
            resource_uri,
            progress_callback,
            force_refetch=self._force_refetch,
            strip_prefix=strip_prefix,
            **self._binname_kwargs,
        )
        return res.where


class _ToolChainLatestMatchedResourceFetcher(_ToolChainFetcherImplBase):
    _DEFAULT_UPDATE_INTERVAL = 24 * 3600
    _DISABLE_AUTO_UPDATE_FILE_NAME = '.disable.auto.update'
    _MAX_TOOL_REGISTRY_RESOURCE_VERSIONS = 10
    _updated_toolchains = set()

    def __init__(
        self,
        root,
        toolchain_name,
        platform_replacements,
        params,
        for_platform,
        binname,
        force_refetch,
        download_fn,
        bottle_name,
        extra_query: ExtraQuery | None = None,
    ):
        super(_ToolChainLatestMatchedResourceFetcher, self).__init__(
            root, toolchain_name, platform_replacements, for_platform, binname, force_refetch, download_fn
        )
        self.__params = params
        self.__bottle_name = bottle_name
        self.__update_interval = params.get('update_interval', self._DEFAULT_UPDATE_INTERVAL)
        self.__force_update = bool(os.environ.get('YA_TOOL_FORCE_UPDATE'))
        self.__tool_registry_enabled = tool_registry.is_enabled(bottle_name)
        if self.__tool_registry_enabled and not config.has_mapping() and extra_query:
            raise UnsupportedToolRegistryQueryError(
                '{}: --release, --attribute and --no-attribute are not supported by tool registry'.format(
                    toolchain_name
                )
            )
        self.__extra_query_digest = extra_query.digest() if extra_query else None
        if params.get('ignore_platform', False):
            self.__platforms = []
            self.__tool_registry_platforms = ['any']
        else:
            platforms = [for_platform] + platform_matcher.get_platform_replacements(
                for_platform, self._platform_replacements
            )
            self.__platforms = self._add_default_arch(platforms)
            self.__tool_registry_platforms = self._get_full_platforms(platforms)
        if extra_query:
            # Patch query
            self.__params = deepcopy(self.__params)
            query = self.__params['query']
            if extra_query.releases is not None:
                if extra_query.releases == '':
                    query.pop('releases', None)
                else:
                    query['releases'] = extra_query.releases
            if extra_query.attributes:
                attrs = query.pop('attributes', {})
                for k, v in extra_query.attributes.items():
                    if v is None:
                        attrs.pop(k, None)
                    else:
                        attrs[k] = v
                if attrs:
                    query['attributes'] = attrs

    def _add_default_arch(self, platforms):
        default_arch = 'x86_64'
        result = []
        for p in platforms:
            if '-' not in p:
                result += [p, '{}-{}'.format(p, default_arch)]
            else:
                result.append(p)
        return result

    def _get_full_platforms(self, platforms):
        default_arch = 'x86_64'
        result = []
        for platform in platforms:
            full_platform = platform if '-' in platform else '{}-{}'.format(platform, default_arch)
            if full_platform not in result:
                result.append(full_platform)
        return result

    def resource_id_from_cache(self):
        return self._get_matched_resource().public_uri()

    def _get_symlink_path(self):
        bottle_name = self.__bottle_name
        if self.__extra_query_digest:
            bottle_name += '@' + self.__extra_query_digest
        return os.path.join(self._root, '-'.join([bottle_name, self._platform]))

    def _get_info_path(self):
        return self._get_symlink_path() + '.info'

    def _write_info(self, info):
        info_path = self._get_info_path()
        tmp = info_path + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(info, f)
        fs.replace(tmp, info_path)
        logger.debug('{}: write info "{}" to {}'.format(self._toolchain_name, info, info_path))

    def _read_info(self):
        info_path = self._get_info_path()
        with open(info_path) as f:
            info = json.load(f)
        logger.debug('{}: info "{}" was read from {}'.format(self._toolchain_name, info, info_path))
        return info

    def _get_matched_resource(self, force_update=False):
        if config.has_mapping():
            return _ResolvedResource(resource_id=self._get_resource_id_from_mapping_config())

        resource = None if force_update else self._get_resource_from_info_file()
        if resource is None:
            link_path = self._get_symlink_path()
            # Use symlink path for lock file as install_symlink() does
            with safe_resource_lock(link_path):
                resource = self._find_resource()
                info = {
                    'update_time': int(time.time()),
                    'resource_id': resource.resource_id,
                }
                if resource.url is not None:
                    info['resource_url'] = resource.url
                    info['resource_integrity'] = resource.integrity
                self._write_info(info)
        return resource

    def _get_resource_id_from_mapping_config(self):
        mapping = config.mapping()
        bottle = mapping.get('bottles')
        if bottle is None:
            raise Exception("'bottles' section is missing in mapping config file")
        platforms = self.__platforms or ['any']
        for platform in platforms:
            if platform in bottle:
                return bottle[platform]
        raise ResourceNotFound('{}: no resource is found in bottle mapping: {}'.format(self._toolchain_name, bottle))

    def _get_resource_from_info_file(self):
        info_path = self._get_info_path()
        update_time_threshold = time.time() - self.__update_interval
        if os.path.exists(info_path):
            info = self._read_info()
            update_time = info['update_time']
            if (
                self.__force_update
                and self._toolchain_name not in _ToolChainLatestMatchedResourceFetcher._updated_toolchains
            ):
                logger.debug('{}: force update'.format(self._toolchain_name))
                _ToolChainLatestMatchedResourceFetcher._updated_toolchains.add(self._toolchain_name)
            elif bool(info.get('resource_url')) != self.__tool_registry_enabled:
                if self.__is_auto_update_disabled:
                    raise ResourceNotFound(
                        '{}: cached resource source does not match the current tool registry configuration'.format(
                            self._toolchain_name
                        )
                    )
                logger.debug('{}: cached resource source has changed'.format(self._toolchain_name))
            elif update_time > update_time_threshold or self.__is_auto_update_disabled:
                resource = _ResolvedResource(
                    resource_id=info['resource_id'],
                    integrity=info.get('resource_integrity'),
                    url=info.get('resource_url'),
                )
                logger.debug('{}: use cached toolchain resource: {}'.format(self._toolchain_name, resource))
                return resource
            else:
                logger.debug('{}: it is time to check toolchain for update'.format(self._toolchain_name))
        elif self.__is_auto_update_disabled:
            raise Exception('File {} is mandatory if auto update is disabled'.format(info_path))
        return None

    def _compact_res_info(self, res):
        # XXX 'links' field might be really huge
        limit = 10

        links = res.get('http', {}).get('links', [])
        if len(links) > limit:
            res = res.copy()
            res['http'] = res['http'].copy()
            res['http']['links'] = links[:limit] + ['<{} more links>'.format(len(links) - limit)]

        return res

    def _find_resource(self):
        if self.__tool_registry_enabled:
            return self._find_tool_registry_resource()
        return self._find_sandbox_resource()

    def _find_tool_registry_resource(self):
        query = self.__params['query']
        logger.debug(
            '%s: lookup resource in tool registry with query %s for platforms %s',
            self._toolchain_name,
            query,
            self.__tool_registry_platforms,
        )
        resource = tool_registry.find_resource(self.__bottle_name, query, self.__tool_registry_platforms)
        if resource is None:
            raise ResourceNotFound(
                '{}: no resource is found. Tool registry query: {}, platforms: {}'.format(
                    self._toolchain_name, json.dumps(query), self.__tool_registry_platforms
                )
            )
        if not isinstance(resource.resource_id, str) or not resource.resource_id.isdecimal():
            raise ValueError(
                '{}: tool registry object must have a numeric x-amz-meta-resource-id'.format(self._toolchain_name)
            )
        logger.debug('%s: found tool registry resource: %s', self._toolchain_name, resource)
        return _ResolvedResource(
            resource_id=_normalize_registry_resource_id(resource.resource_id),
            integrity=resource.integrity,
            url=resource.url,
        )

    def _find_sandbox_resource(self):
        failed_queries = []
        for sb_api_query in self._get_sb_api_queries():
            logger.debug('%s: lookup resources with query: %s', self._toolchain_name, sb_api_query)
            resources = _list_resources_no_links(**sb_api_query)
            if resources:
                assert len(resources) == 1
                resource = resources[0]
                logger.debug('%s: found resource: %s', self._toolchain_name, self._compact_res_info(resource))
                return _ResolvedResource(resource_id=resource['id'])
            failed_queries.append(json.dumps(sb_api_query))
        raise ResourceNotFound(
            '{}: no resource is found. Sandbox queries made:\n{}'.format(
                self._toolchain_name, ',\n'.join(failed_queries)
            )
        )

    def _get_sb_api_queries(self, platforms=None, limit=1):
        from sandbox.common.types.resource import State as ResourceState

        sb_api_query = {
            'type': self.__params['query']['resource_type'],
            'owner': self.__params['query']['owner'],
            'state': ResourceState.READY,
            'order': '-id',
            'limit': limit,
        }
        if 'releases' in self.__params['query']:
            sb_api_query['releases'] = self.__params['query']['releases']
        if 'attributes' in self.__params['query']:
            sb_api_query['attrs'] = self.__params['query']['attributes'].copy()
        if platforms is None:
            platforms = self.__platforms
        if platforms:
            for platform in self.__platforms:
                sb_api_query.setdefault('attrs', {})['platform'] = platform
                yield deepcopy(sb_api_query)
        else:
            yield sb_api_query

    def _details(self):
        return "params: {}".format(self.__params)

    def _fetch(self):
        resource = self._get_matched_resource()
        seen_resources = {resource}
        while True:
            try:
                return self._fetch_resource(resource)
            except (http_client.BadMD5Exception, UniversalFetcherIntegrityError):
                if not resource.is_tool_registry_resource or self.__is_auto_update_disabled:
                    raise
                self._invalidate_info(resource)
                if len(seen_resources) >= self._MAX_TOOL_REGISTRY_RESOURCE_VERSIONS:
                    raise

                # A registry URL may be overwritten after lookup; only an integrity error proves this race,
                # so retry only if resolving it again yields a resource version not seen before.
                resource = self._get_matched_resource(force_update=True)
                if resource in seen_resources:
                    self._invalidate_info(resource)
                    raise
                seen_resources.add(resource)

    def _fetch_resource(self, resource):
        resource_uri = resource.download_uri()
        fetcher, progress_callback = _get_fetcher(self._toolchain_name)
        res = self._download_fn(
            fetcher,
            self._root,
            resource_uri,
            progress_callback,
            force_refetch=self._force_refetch,
            **self._binname_kwargs,
        )
        return self._install_symlink(res.where)

    def _invalidate_info(self, resource):
        link_path = self._get_symlink_path()
        with safe_resource_lock(link_path):
            info_path = self._get_info_path()
            if not os.path.exists(info_path):
                return
            info = self._read_info()
            if (
                info.get('resource_id') != resource.resource_id
                or info.get('resource_integrity') != resource.integrity
                or info.get('resource_url') != resource.url
            ):
                return
            info['update_time'] = 0
            self._write_info(info)

    def _install_symlink(self, resource_path):
        link_path = self._get_symlink_path()
        toolscache.notify_tool_cache(link_path)
        return install_symlink(resource_path, link_path)

    @func.lazy_property
    def __is_auto_update_disabled(self):
        return os.path.exists(os.path.join(self._root, self._DISABLE_AUTO_UPDATE_FILE_NAME))


def _get_fetcher(name, resource_type=None):
    def default_progress_callback(_, __):
        sys.stderr.write('.')
        sys.stderr.flush()

    def default_finish_callback():
        sys.stderr.write('\n')
        sys.stderr.flush()

    progress_callback = default_progress_callback
    finish_callback = default_finish_callback

    try:
        import app_ctx

        if getattr(app_ctx, 'state') and getattr(app_ctx, 'display'):
            progress_info = progress_info_lib.ProgressInfo()

            use_universal_fetcher_everywhere = (
                hasattr(app_ctx, 'use_universal_fetcher_everywhere') and app_ctx.use_universal_fetcher_everywhere
            )

            def display_progress(downloaded, total):
                if not use_universal_fetcher_everywhere:
                    app_ctx.state.check_cancel_state()

                progress_info.set_total(total)
                progress_info.update_downloaded(downloaded)

                app_ctx.display.emit_status(
                    'Downloading [[imp]]{}[[rst]] - [[imp]]{}[[rst]]'.format(name, progress_info.pretty_progress)
                )

            def display_finish():
                app_ctx.display.emit_status('')

            progress_callback = display_progress
            finish_callback = display_finish

        progress_printer = ProgressPrinter(
            progress_callback=progress_callback,
            finish_callback=finish_callback,
        )
        return app_ctx.legacy_sandbox_fetcher, progress_printer
    except (ImportError, AttributeError):
        progress_printer = ProgressPrinter(
            progress_callback=default_progress_callback, finish_callback=default_finish_callback
        )

    try:
        from devtools.ya.yalibrary.yandex.sandbox import fetcher

        return fetcher.SandboxFetcher(), progress_printer
    except ImportError:
        return None, progress_printer


def _list_resources_no_links(**kwargs):
    # A widely held resource has thousands of host links, and the lookup needs only its id
    from devtools.ya.yalibrary.yandex.sandbox import SandboxClient

    return _do_sandbox_method(SandboxClient.list_resources_no_links, **kwargs)


def _do_sandbox_method(method, **kwargs):
    rest_params = {'total_wait': _SB_REST_API_TIMEOUT, 'check_for_cancel': _get_cancel_checker()}
    guards.update_guard(guards.GuardTypes.FETCH)
    from devtools.ya.yalibrary.yandex.sandbox import SandboxClient

    try:
        return method(SandboxClient(token=get_sandbox_token(), rest_params=rest_params), **kwargs)
    except requests.HTTPError as e:
        logger.debug("Error connecting to sandbox: %s", e)
        if e.response.status_code != 403:
            raise e
    logger.warning("Incorrect sandbox token, trying anonymous access")

    return method(SandboxClient(rest_params=rest_params), **kwargs)


def _get_cancel_checker():
    try:
        import app_ctx

        return app_ctx.state.check_cancel_state
    except (ImportError, AttributeError):
        return None
