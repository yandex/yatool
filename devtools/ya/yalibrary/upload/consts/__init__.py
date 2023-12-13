import six


class UploadException(Exception):
    mute = True


class UploadTransport(object):
    Skynet = 'skynet'
    Http = 'http'
    Mds = 'mds'


TTL_INF = six.MAXSIZE

DEFAULT_TTL = 14
DEFAULT_RESOURCE_ARCH = "any"
DEFAULT_RESOURCE_TYPE = "OTHER_RESOURCE"
DEFAULT_SANDBOX_URL = "https://sandbox.yandex-team.ru"
DEFAULT_MDS_HOST = "storage.yandex-team.ru"
DEFAULT_MDS_PORT = 80
DEFAULT_MDS_NAMESPACE = "devtools"

MDS_TOKEN_SEC_KEY = "DEVTOOLS_MDS_TOKEN"
MDS_TOKEN_SEC_UUID = "sec-01ckx62bkvpee201ks4d4a1g09"

DEFAULT_RELEASE_WAIT_TIMEOUT = 240
