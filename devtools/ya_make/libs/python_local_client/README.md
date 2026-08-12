# devtools.ya_make.libs.python_local_client

This library provides synchronous byte-stream connections to a
`local_service` without exposing UNIX-socket or Windows named-pipe naming to
callers.

Use `local:<app-name>/<relative-path>`. The application name selects the
`AppPaths` namespace and the rest identifies an endpoint inside it:

```python
import devtools.ya_make.libs.python_local_client as python_local_client

with python_local_client.connect('local:my-application/control') as connection:
    connection.sendall(b'ping')
    reply = connection.recv(4)
```

The Python client parses the `local:` scheme; `user_paths` receives the
application name and relative path separately. Names must not be empty,
absolute, or contain `.` or `..` components. Keep
them short because UNIX-domain sockets have a small platform-dependent address
limit.

`sendall()` and `recv()` operate on bytes. `shutdown_write()` performs a socket
half-close on POSIX. It is a no-op on Windows named pipes, so portable
protocols must use framing or known response lengths instead of relying on EOF.

## Test coverage

The `libs/local_service/it` echo suite is the cross-platform end-to-end suite
for this library. It covers connection acceptance, byte exchange, sequential
and concurrent clients, and the service's legacy absolute endpoint input.
Linux execution was verified during initial implementation; Windows and macOS
transport execution is expected from their corresponding CI targets.
