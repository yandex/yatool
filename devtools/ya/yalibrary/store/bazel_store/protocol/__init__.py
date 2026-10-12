"""Преобразование UID ya в AC-ключ HTTP bazel cache; формат версии 1 неизменен."""

import binascii
import hashlib

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
