import logging

from yalibrary import platform_matcher

logger = logging.getLogger(__name__)


class ToolsResolver(object):
    @classmethod
    def get_python_bin(cls, params, global_resources):
        if params:
            py_ver = getattr(params, "flags", {}).get("USE_SYSTEM_PYTHON")
            if py_ver:
                host_os = platform_matcher.current_os()
                resource = cls.get_external_resource(global_resources, 'EXTERNAL_PYTHON_RESOURCE_GLOBAL')
                if host_os == "WIN":
                    return "{res}/python/python.exe".format(res=resource)
                elif host_os == "LINUX":
                    return "{res}/python/bin/python{version}".format(res=resource, version=py_ver)
                elif host_os == "DARWIN":
                    return "{res}/python/Python.framework/Versions/{version}/bin/python{version}".format(
                        res=resource, version=py_ver
                    )
            elif getattr(params, "flags", {}).get("USE_ARCADIA_PYTHON") == "no":
                return "python"
        return "$(PYTHON)/python"

    @classmethod
    def get_python_lib(cls, params, global_resources):
        if params and getattr(params, "flags", {}).get("USE_SYSTEM_PYTHON"):
            host_os = platform_matcher.current_os()
            resource = cls.get_external_resource(global_resources, 'EXTERNAL_PYTHON_RESOURCE_GLOBAL')
            if host_os == "LINUX":
                return "{res}/python/lib/x86_64-linux-gnu".format(res=resource)
            elif host_os == "DARWIN":
                return "{res}/python".format(res=resource)
        return None

    @staticmethod
    def get_external_resource(global_resource, prefix):
        found = [(k, v) for k, v in global_resource.items() if k.startswith(prefix)]
        if len(found) == 1:
            return found[0][1]
        elif not found:
            logger.warning("Target platform has requested SYSTEM_PYTHON, but no target depends on it. Is PEERDIR(build/platform/python) missing?")
            return "NO_EXTERNAL_PYTHON_DEPENDENCY_IS_REQUESTED"
        raise AssertionError(global_resource)
