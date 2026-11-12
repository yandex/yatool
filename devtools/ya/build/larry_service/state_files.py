"""Private atomic publication inside an already locked, owner-only directory."""

import os
import tempfile
from typing import Callable


def write_private_file(path: str, contents: bytes, *, check_cancel: Callable[[], None]) -> None:
    # exts.fs.write_file inherits umask for its temporary file. Credentials need
    # mode 0600 from creation, without mutating the calling process's umask.
    check_cancel()
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode='wb', prefix='.state-', dir=os.path.dirname(path), delete=False
        ) as output:
            temporary = output.name
            output.write(contents)
        check_cancel()
        os.replace(temporary, path)
    finally:
        if temporary is not None and os.path.lexists(temporary):
            os.unlink(temporary)
