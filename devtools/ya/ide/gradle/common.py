from devtools.ya.core import stage_tracer

tracer = stage_tracer.get_tracer("gradle")


class YaIdeGradleException(Exception):
    mute = True
