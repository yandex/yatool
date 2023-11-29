import exts.hashing


class Cmd(object):
    def __init__(self, cmd, cwd, inputs=None, resources=None, env=None):
        self.cmd = cmd
        self.cwd = cwd
        self.inputs = inputs or []
        self.resources = resources or []
        self.env = env or {}

    def __str__(self):
        return ' '.join(self.cmd) + ' | ' + str(self.cwd) + ' | {}'.format(self.env) if self.env else ''

    def uid(self):
        return str(exts.hashing.fast_hash('#'.join(sorted(self.cmd) + [str(self.cwd)] + [str(self.env)])))

    def to_dict(self):
        return {'cmd_args': self.cmd, 'cwd': self.cwd or "$(BUILD_ROOT)", 'env': self.env}
