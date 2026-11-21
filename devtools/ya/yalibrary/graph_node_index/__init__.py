"""A shared UID lookup over finalized graph nodes, without copying node data."""

import threading
from collections.abc import Mapping, Sequence
from types import MappingProxyType


class GraphNodeIndex:
    def __init__(self, graph_nodes: Sequence[Mapping]) -> None:
        self._graph_nodes = graph_nodes
        self._nodes = None
        self._lock = threading.Lock()

    @property
    def nodes(self) -> Mapping[str, Mapping]:
        """Materialize once and return the same read-only mapping to every consumer.

        Node values are the original objects/views. The graph topology must be
        finalized before this lookup is first used.
        """
        if self._nodes is None:
            with self._lock:
                if self._nodes is None:
                    self._nodes = MappingProxyType({node['uid']: node for node in self._graph_nodes})
                    self._graph_nodes = None
        return self._nodes

    def __getitem__(self, uid: str) -> Mapping:
        return self.nodes[uid]
