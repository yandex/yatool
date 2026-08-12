import os

from devtools.ya_make.libs.python_larry_client import LarryClient as ProtocolLarryClient


class LarryClient:
    """Compatibility adapter for the historical ya build return contract."""

    def __init__(self, callback, graph, source_root=None):
        self.callback = callback
        self.graph = graph
        self.source_root = source_root

    def build(self, addr: str):
        result = ProtocolLarryClient().build(
            addr,
            self.source_root or os.getcwd(),
            self.graph,
        )
        # Legacy LocalExecutor-compatible tuple. Larry currently does not
        # materialize these maps; the fifth item is the build return code.
        return (
            {},
            {},
            None,
            {},
            0 if result.succeeded else 1,
            {},
            {},
            None,
            {},
        )
