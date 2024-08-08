from abc import ABCMeta, abstractmethod
from six import add_metaclass

# from yalibrary.fetcher.uri_parser import ParsedResourceUri


@add_metaclass(ABCMeta)
class ResourceDisplay:
    @staticmethod
    def create(parsed_uri):
        # type: (ParsedResourceUri) -> ResourceDisplay
        if parsed_uri.resource_type == "http":
            _, _, domain, url_path = parsed_uri.resource_url.split("/", 3)
            if domain in NpmResourceDisplay.NPM_DOMAINS:
                return NpmResourceDisplay(domain, url_path)

            return DefaultHttpResourceDisplay(domain, url_path)

        return DefaultResourceDisplay(parsed_uri.resource_uri)

    @abstractmethod
    def get_short_display(self, color=False):
        pass


class DefaultResourceDisplay(ResourceDisplay):
    def __init__(self, resource_uri):
        # type: (str) -> None

        self._short_display = resource_uri[0:20]

    def get_short_display(self, color=False):
        return self._short_display


class DefaultHttpResourceDisplay(ResourceDisplay):
    def __init__(self, domain, url_path):
        # type: (str, str) -> None
        url = domain + "/" + url_path

        if len(url) > 60:
            url = url[0:30] + "…" + url[-20:]

        self._short_display = url[0:60]

    def get_short_display(self, color=False):
        return self._short_display


class NpmResourceDisplay(ResourceDisplay):
    NPM_DOMAINS = ("npm.yandex-team.ru", "registry.npmjs.org")
    TGZ_EXT_LEN = len(".tgz")

    def __init__(self, domain, url_path):
        # type: (str, str) -> None
        self.domain = domain

        parts = url_path.split("/")  # ['@types%2flodash', '-', 'lodash-4.14.182.tgz']
        self.package_full_name = parts[0].replace("%2f", "/")

        package_name = self.package_full_name.split("/")[1] if "/" in self.package_full_name else self.package_full_name
        self.package_version = parts[2][len(package_name) + 1 : -self.TGZ_EXT_LEN]

    def get_short_display(self, color=False):
        pre = "[[imp]][[c:yellow]]" if color else ""
        post = "[[rst]]" if color else ""
        return "{domain}: {pre}{package_full_name}{post} @ {pre}{package_version}{post}".format(
            domain=self.domain,
            package_full_name=self.package_full_name,
            package_version=self.package_version,
            pre=pre,
            post=post,
        )
