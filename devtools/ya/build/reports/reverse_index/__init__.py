import threading
from collections.abc import Mapping, Sequence

from ._reverse_index import ReverseIndex as _NativeReverseIndex


class ReverseIndex:
    """Share an immutable reverse index over finalized graph nodes."""

    def __init__(self, graph_nodes: Sequence[Mapping]) -> None:
        self._graph_nodes = graph_nodes
        self._index = None
        self._lock = threading.Lock()

    def materialize(self) -> _NativeReverseIndex:
        """Build once on demand; leave the graph available for retry on error."""
        if self._index is None:
            with self._lock:
                if self._index is None:
                    self._index = _NativeReverseIndex(self._graph_nodes)
                    self._graph_nodes = None
        return self._index
