import json
import subprocess


class LarryClient:
    def __init__(self, callback, graph):
        self.callback = callback
        self.graph = graph

    def build(self, addr: str):
        connection_type, target = self._parse_addr(addr)
        if connection_type == 'sync':
            return self._build_sync(target)

        raise NotImplementedError('Connecting to Larry through a local socket is not implemented yet')

    def _build_sync(self, binary_path: str):
        process = subprocess.Popen(
            [binary_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        process.communicate(json.dumps(self.graph).encode('utf-8'))
        return {}, {}, None, {}, process.returncode, {}, {}, None, {}

    def _parse_addr(self, addr: str):
        if addr == 'sync':
            return 'sync', self._get_latest_release()

        connection_type, separator, target = addr.partition(':')
        if separator and connection_type in ('local', 'sync') and target:
            return connection_type, target

        raise ValueError(
            "Unsupported Larry address {!r}; expected 'local:<socket path>', "
            "'sync', or 'sync:<binary path>'".format(addr)
        )

    @staticmethod
    def _get_latest_release():
        raise NotImplementedError('Getting the latest Larry release is not implemented yet')
