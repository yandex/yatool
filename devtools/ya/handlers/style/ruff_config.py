import os
import tempfile

import exts


DEFAULT_CONFIG_FILENAME = 'ruff.toml'


@exts.func.memoize()
def load_ruff_config(filename=None, content=None):
    # type(typing.Optional[str], typing.Optional[bytes]) -> RuffConfig
    return RuffConfig(filename=filename, content=content)


class RuffConfig:
    def __init__(self, filename=None, content=None):
        assert (filename is not None or content is not None) and not all(
            (filename is not None, content is not None),
        )
        self.content = content
        self.filename = filename
        self._save_to_file()

    @property
    def path(self):
        # type() -> str
        return self.filename

    def _save_to_file(self):
        # type() -> None
        if self.content is None:
            return
        tmp_dir = tempfile.mkdtemp()
        full_path = os.path.join(tmp_dir, DEFAULT_CONFIG_FILENAME)
        with open(full_path, 'wb') as f:
            f.write(self.content)
        self.filename = full_path
