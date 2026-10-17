"""Преобразование UID ya в AC-ключ HTTP bazel cache; формат версии 1 неизменен."""

import binascii
import hashlib
import posixpath

META_VERSION = '1'
SHA256_LENGTH = 64


def uid_to_uri(uid):
    """Короткий UID кодируется hex, длинный — SHA-256; последние символы задают формат."""
    # Предпоследний символ: 0 — hex без хеширования, 1 — SHA-256.
    # Последний символ: версия формата META_VERSION.
    pseudo_hash = binascii.hexlify(uid.encode('utf-8')).decode('utf-8')
    padding_length = SHA256_LENGTH - len(pseudo_hash)
    if padding_length < 2:
        pseudo_hash = hashlib.sha256(pseudo_hash.encode('utf-8')).hexdigest()[:-2]
        pseudo_hash += "1" + META_VERSION
    else:
        pseudo_hash += '0' * (padding_length - 1) + META_VERSION
    return pseudo_hash


def resolve_symlinks(paths, symlinks):
    """Resolve relative file links within one bundle, without touching the filesystem."""
    paths = set(paths)
    directories = {''}
    for path in paths:
        parent = posixpath.dirname(path)
        while parent:
            if parent in paths:
                raise ValueError('A bundle file cannot be a parent directory')
            directories.add(parent)
            parent = posixpath.dirname(parent)
    resolved = {}
    visiting = set()

    def resolve(path):
        if path in resolved:
            return resolved[path]
        if path in visiting:
            raise ValueError('Symlink cycle in bundle')
        target = symlinks[path]
        if not isinstance(target, str) or not target or '\0' in target or posixpath.isabs(target):
            raise ValueError('Symlink target must be a nonempty relative path')
        visiting.add(path)
        current = posixpath.dirname(path)
        for part in target.split('/'):
            if current not in directories:
                raise ValueError('Symlink target traverses a file or missing directory')
            if part == '..':
                if not current:
                    raise ValueError('Symlink target escapes the bundle')
                current = posixpath.dirname(current)
            elif part not in ('', '.'):
                current = posixpath.join(current, part)
        if current not in paths:
            raise ValueError('Symlink target is not a bundle file')
        result = resolve(current) if current in symlinks else current
        visiting.remove(path)
        resolved[path] = result
        return result

    for path in symlinks:
        if path not in paths:
            raise ValueError('Symlink is not a bundle file')
        resolve(path)
    return resolved
