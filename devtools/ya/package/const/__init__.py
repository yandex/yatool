class PackageFormat(object):
    DEBIAN = "debian"
    TAR = "tar"
    DOCKER = "docker"
    RPM = "rpm"
    WHEEL = "wheel"
    AAR = "aar"
    NPM = "npm"
    SQUASHFS = "squashfs"


NANNY_RELEASE_URL = "https://nanny.yandex-team.ru/api/tickets/CreateRelease/"
NANNY_RELEASE_TYPES = ["TESTING", "STABLE", "PRESTABLE", "UNSTABLE"]
DEBIAN_HOST_DEFAULT_COMPRESSION_LEVEL = 'host-default'
