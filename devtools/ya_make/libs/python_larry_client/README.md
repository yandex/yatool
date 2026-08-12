# Python Larry client

`devtools.ya_make.libs.python_larry_client` is the synchronous Python client
for Larry's bootstrap protocol. It deliberately implements only one build per
connection:

```text
client -> {"type":"start_build","source_root":"/absolute/root"}\n
client -> <compact graph JSON>\n
server -> {"type":"build_finished",...}\n
```

Handshake, `build_accepted`, progress, task/result events, ACKs, reconnect, and
an asynchronous Python API are not implemented yet. Message names and shapes
match protocol v1 so those events can be added without changing the existing
typed values.

## API

```python
from devtools.ya_make.libs.python_larry_client import LarryClient

finished = LarryClient().build(
    'sync:/path/to/larry',
    '/absolute/source/root',
    graph,
)
if not finished.succeeded:
    for failure in finished.failed_nodes:
        print(failure.uid, failure.exit_code, failure.signal)
```

The supported addresses are:

- `sync:<binary-path>` — start Larry and exchange the request over its standard
  streams. Dedicated writer and stderr-reader threads drive both pipe directions
  while stdin remains open until `build_finished`, preventing pipe-capacity
  deadlocks without turning an ordinary request completion into cancellation.
- `local:<app-name>/<relative-path>` — connect through
  `devtools.ya_make.libs.python_local_client`, which selects a Unix socket or
  Windows named pipe and resolves its platform root.

Bare `sync` intentionally raises `NotImplementedError` until release discovery
is provided.

## Codec and current limits

The pybind11 module delegates header, graph, and terminal-event validation and
serialization to `libs/larry_protocol`; Python does not maintain a second event
schema. The C++ server parses the graph directly from its input stream and does
not call `readline()` for it. The Python binding currently converts the caller's
in-memory dictionary through a complete JSON representation, so it can hold
multiple graph-sized representations temporarily. A future streaming Python
encoder can replace that conversion without changing the transport or public
result types.

Bootstrap statistics count only ordered result roots: `total` is the number of
root results, successful and failed roots partition it, and unavailable
`cache_hits` and `avoided` counters are zero. Successful root UIDs are returned
as `ready_results`; output materialization is outside this subset.

The historical `devtools.ya.build.larry_client` adapter converts the typed
result to its nine-element tuple. Its fifth item is `0` for protocol status
`success` and `1` for `failed`; the remaining not-yet-materialized maps retain
their historical empty values.
