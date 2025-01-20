import os

import exts.fs
import package


class YaPackagePostprocessorException(Exception):
    mute = True


class Postprocessor(object):
    def __init__(self, arcadia_root, builds, data, result_dir, global_temp, workspace, opts, formatters):
        self.arcadia_root = arcadia_root
        self.builds = builds
        self.data = data
        self.result_dir = result_dir
        self.global_temp = global_temp
        self.opts = opts
        self.workspace = workspace
        self.formatters = formatters or {}

    def get_binary(self):
        binary = self._get_binary()
        if not os.path.exists(binary):
            raise YaPackagePostprocessorException("Binary {} is not exist".format(binary))
        return binary

    def _get_binary(self):
        raise NotImplementedError

    def source_path(self):
        return self.data['source']['path']

    def run(self):
        real_arguments = self.data.get("arguments", [])
        real_arguments = [i.format(**self.formatters) for i in real_arguments]

        cwd = self.result_dir
        data_cwd = self.data.get("cwd")
        if data_cwd:
            cwd = os.path.join(self.result_dir, data_cwd.format(**self.formatters))
            exts.fs.ensure_dir(cwd)

        real_env = None
        data_env = self.data.get("env")
        if data_env:
            real_env = os.environ.copy()
            for k, v in data_env.items():
                real_env[k] = v.format(**self.formatters)

        out, err = package.process.run_process(self.get_binary(), real_arguments, cwd=cwd, env=real_env)

        if self.opts.be_verbose:
            package.display.emit_message('{}[[good]]POSTPROCESS OUTPUT[[rst]]: {}'.format(package.PADDING, out))
            package.display.emit_message('{}[[good]]POSTPROCESS ERROR[[rst]]: {}'.format(package.PADDING, err))

    def __str__(self):
        return str(self.data)


class BuildOutputPostprocessor(Postprocessor):
    def _get_binary(self):
        try:
            output_root = self.builds[self.data['source'].get('build_key', None)]["output_root"]
        except KeyError:
            raise YaPackagePostprocessorException(
                "Cannot choose BUILD_OUTPUT for '{}', set 'build_key' explicitly to one of [{}]".format(
                    ["{}={}".format(k, v) for k, v in sorted(self.data['source'].items())],
                    ", ".join(sorted(self.builds.keys())),
                )
            )

        return os.path.join(output_root, self.source_path())


class ArcadiaPostprocessor(Postprocessor):
    def _get_binary(self):
        return os.path.join(self.arcadia_root, self.source_path())


class TempPostprocessor(Postprocessor):
    def _get_binary(self):
        return os.path.join(self.result_dir, self.global_temp, self.source_path())
