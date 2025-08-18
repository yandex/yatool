from libcpp.utility cimport move as std_move

from devtools.libs.acdigest.acdigest cimport (
    TFileDigest,
    GetFileDigest,
    GetBufferDigest,
    DIGEST_GIT_LIKE_VERSION,
    DIGEST_XXHASH_VERSION,
    DIGEST_CURRENT_VERSION
)
from util.folder.path cimport TFsPath
from util.generic.string cimport TString

import six


class FileDigest(object):
    def __init__(self, content_digest: str, uid: str, size: int):
        self.content_digest: str = content_digest
        self.uid: str = uid
        self.size: int = size

    def to_json(self) -> dict:
        return {"content_digest": self.content_digest, "uid": self.uid, "size": self.size}

    @staticmethod
    def from_json(data: dict) -> "FileDigest":
        return FileDigest(six.ensure_str(data["content_digest"]), six.ensure_str(data["uid"]), data["size"])


def get_file_digest(file_name: str, content_digest: str = "") -> FileDigest:
    cdef TString file_name_cpp = six.ensure_binary(file_name)
    cdef TString content_digest_cpp = six.ensure_binary(content_digest)

    cdef TFileDigest digest
    with nogil:
        digest = GetFileDigest(TFsPath(file_name_cpp), std_move(content_digest_cpp))

    return FileDigest(six.ensure_str(digest.ContentDigest), six.ensure_str(digest.Uid), digest.Size)


def combine_hashes(hashes: list[str]):
    """
        Combine hashes into a single hash.

        Note: this function differ from devtools/ya/exts/hashing.py::sum_hashes() in the following aspects:
         - ignores hashes order (sorts before combining)
         - uses the xxh3_128 hash algorithm instead of the md5 one
         - doesn't add trailing '#'
    """
    s = b'#'.join(sorted(six.ensure_binary(s) for s in hashes))
    cdef TString v = GetBufferDigest(s, len(s))
    return six.ensure_str(v)


digest_git_like_version = DIGEST_GIT_LIKE_VERSION
digest_xxhash_version = DIGEST_XXHASH_VERSION
digest_current_version = DIGEST_CURRENT_VERSION
