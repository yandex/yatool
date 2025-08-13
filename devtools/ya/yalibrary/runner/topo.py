import collections
import threading

DEFAULT_ERR_MSG_PATTERN = "Node {node} not found in {lookup_in}"


class BaseTopoError(Exception):
    pass


class NodeNotFoundInTopoError(BaseTopoError):
    pass


class NodeFoundInTopoError(BaseTopoError):
    pass


def _assert_node_in(node, lookup_in, msg_pattern=DEFAULT_ERR_MSG_PATTERN):
    if node not in lookup_in:
        msg = msg_pattern.format(node, lookup_in)
        raise NodeNotFoundInTopoError(msg)


def _assert_node_not_in(node, lookup_in, msg_pattern=DEFAULT_ERR_MSG_PATTERN):
    if node in lookup_in:
        msg = msg_pattern.format(node, lookup_in)
        raise NodeFoundInTopoError(msg)


class DisjointSet(object):
    def __init__(self):
        self._leader = {}
        self._followers = {}
        self._values = {}

    def __contains__(self, item):
        return item in self._leader

    def __getitem__(self, item):
        return self._values[self.leader(item)]

    def __setitem__(self, key, value):
        if key not in self._leader:
            self._leader[key] = key
            self._followers[key] = [key]
        self._values[self.leader(key)] = value

    def leader(self, node):
        if self._leader[node] != node:
            self._leader[node] = self.leader(self._leader[node])

        return self._leader[node]

    def merge(self, node1, node2, value_merger):
        lead1 = self.leader(node1)
        lead2 = self.leader(node2)
        if lead1 == lead2:
            return
        merged_value = value_merger(self._values.pop(lead1), self._values.pop(lead2))
        if len(self._followers[lead1]) >= len(self._followers[lead2]):
            self._leader[lead2] = lead1
            self._followers[lead1].extend(self._followers.pop(lead2))
            self._values[lead1] = merged_value
        else:
            self._leader[lead1] = lead2
            self._followers[lead2].extend(self._followers.pop(lead1))
            self._values[lead2] = merged_value

    def group(self, node):
        return tuple(self._followers[self.leader(node)])

    def keys(self):
        return list(self._leader.keys())


class Topo(object):
    """
    Node states: (a) added (in _dsu) -> (s) scheduled (in _scheduled) -> (c) completed (in _completed).
    Dependencies can be only updated in (a).

    Node is in (c) if all nodes in its set from _dsu called notify_dependants, or _dsu[node] == 0.
    Action from _when_ready should be performed only when the node is in (s) and its dependencies are in (c).
    For each node _when_ready should be performed once.

    For convenience, node called notify_dependants is called semi-completed (sc).
    When last node in node's set calls notify, all of them become completed (c).
    notify_dependants should be called once for each node and when node is in (s) from _when_ready.
    """

    def __init__(self):
        self._deps = collections.defaultdict(list)
        self._who_awaits = collections.defaultdict(list)
        # All dependencies are fixed, and all dependants are notified.
        self._completed = set()
        # All dependencies are fixed, scheduled.
        self._scheduled = set()
        self._lock = threading.RLock()
        self._when_ready = {}
        self._dsu = DisjointSet()
        self._awaiting = {}
        self._activation_order = []

    def __contains__(self, item):
        with self._lock:
            return item in self._dsu

    def is_completed(self, node):
        with self._lock:
            return node in self._completed

    def add_node(self, node):
        with self._lock:
            if node in self._dsu:
                return
            self._dsu[node] = 1
            self._who_awaits[node] = []
            self._awaiting[node] = 0

    def add_deps(self, from_node, *to_nodes):
        with self._lock:
            for to_node in to_nodes:
                _assert_node_in(to_node, self._dsu, "Node {} is not in DSU")  # check existence
                _assert_node_in(from_node, self._dsu, "Node {} is not in DSU")  # check existence
                _assert_node_not_in(from_node, self._scheduled, "Node {} has been already scheduled")

                self._deps[from_node].append(to_node)

                if to_node not in self._completed:
                    self._who_awaits[to_node].append(from_node)
                    self._awaiting[from_node] += 1

    def merge_nodes(self, node1, node2):
        with self._lock:
            _assert_node_not_in(node1, self._completed, "Node {} has been already completed")
            _assert_node_not_in(node2, self._completed, "Node {} has been already completed")
            self._dsu.merge(node1, node2, lambda x, y: x + y)

    def schedule_node(self, node, when_ready=None, inplace_execution=False):
        with self._lock:
            _assert_node_in(node, self._dsu, "Node {} is not in DSU")  # check existence
            _assert_node_not_in(node, self._scheduled, "Node {} has been already scheduled")

            if when_ready:
                self._when_ready[node] = when_ready
            self._scheduled.add(node)
            action = self._check_ready(node, inplace_execution=inplace_execution)

        if action:
            action()

    def notify_dependants(self, node, *args, **kwargs):
        actions = []

        with self._lock:
            self._dsu[node] -= 1
            if self._dsu[node] == 0:
                self._activation_order.append(self._dsu.group(node))
                for y in self._dsu.group(node):
                    self._completed.add(y)
                    for x in self._who_awaits[y]:
                        self._awaiting[x] -= 1
                        # Redundant: _when_ready is set in schedule_node and reset in _check_ready
                        if x not in self._scheduled:
                            continue

                        action = self._check_ready(x)

                        if action:
                            actions.append(action)

        for action in actions:
            action()

    def _iter_deps(self, node):
        for x in self._deps[node]:
            for y in self._dsu.group(x):
                yield y

    def _check_ready(self, node, inplace_execution=False):
        assert self._awaiting[node] >= 0

        with self._lock:
            if self._awaiting[node] != 0:
                return

            if node in self._when_ready:
                func = self._when_ready.pop(node)
                if inplace_execution:
                    return lambda: func(node, list(self._iter_deps(node)), inplace_execution=inplace_execution)
                else:
                    return lambda: func(node, list(self._iter_deps(node)))

    def replay(self):
        for g in self._activation_order:
            yield [(x, list(self._iter_deps(x))) for x in g]

    def get_unscheduled(self):
        return set(self._dsu.keys()) - self._scheduled

    def get_uncompleted(self):
        return set(self._dsu.keys()) - self._completed
