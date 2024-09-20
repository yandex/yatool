import enum


class FetcherType(enum.StrEnum):
    HTTP = "http"
    SANDBOX = "sandbox"
    DOCKER = "docker"
    CUSTOM = "custom"


class SandboxTransportType(enum.StrEnum):
    HTTP = "http"
    SKYNET = "skynet"
    MDS = "mds"


class NetworkType(enum.StrEnum):
    BACKBONE = "Backbone"
    FASTBONE = "Fastbone"
