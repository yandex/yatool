# Larry gRPC wire and runtime

Server result delivery is an optional extension. `Start.required_capabilities`
requests `server-result-delivery-v1`; `Ready.capabilities` acknowledges it before
graph production. An absent capability fails before upload without fallback or
replay. `BuildRequest.server_delivery.nodes` maps every result UID to named
destinations (logical output, absolute root, relative path, explicit/source link
kind and none/directory/contents archive expansion). The server installs each
ready result and returns `CompletedBuild.delivered_results` with final paths and
warnings. These files do not borrow a result view or require a result ACK.
Execution and delivery failures remain separate. Without the extension, the
callback/ACK behavior below remains unchanged.

`server_delivery.replaceable_symres_roots` explicitly identifies old ya-owned
stores for source-link migration. Their links may be replaced, but Larry GC
never traverses or deletes data in those stores.

`proto/session.proto` defines the optional native build session. It is generated
with `PROTO_LIBRARY` and `GRPC`; `codec.*` explicitly maps native values and
protobuf values. `bytes` fields preserve native paths and command diagnostics;
JSON-RPC envelopes and JSON payload conversion are not used. The graph retains
Larry's existing compact EDL serialization.

Requests preserve the ordered native cache layers (local, YT read-through,
YT download-only, Bazel), including store credentials and eligibility options.
Cache callbacks carry their backend, and completed builds include per-backend
restoration statistics. Removed standalone cache-mode field numbers are reserved;
cache configuration is represented by the typed `CacheLayer` oneof.

The bidi `Execute` RPC has this order:

1. Client `Start(version=1, request)`; server `Ready`. No graph production before Ready.
2. Client graph chunks of at most 64 KiB, followed by `GraphEnd`.
3. Server callbacks, monotonically numbered from one. Client ACKs the same ID
   after completing its callback; only Consume ACKs carry a typed consume result.
4. Server BuildResult; client half-close; server gRPC status.

Messages are capped at 16 MiB. Client graph production holds at most two queued
64 KiB chunks plus its current buffer and in-flight write. The server currently
buffers the complete compact graph before parsing, as the previous implementation
does. This is not a total per-build memory limit.

The initial implementation admits one server callback at a time, including its
ACK, across progress and results. Admission precedes prepared-result creation;
Release is awaited on success and failure. This deliberately limits callback
throughput and will be evaluated during the acceptance step. The independent
reader continues receiving ACKs and detects disconnect while execution waits.

`TCqRuntime` pumps a separate CompletionQueue thread. Callers must join all RPC
operations before its destructor releases the work guard and joins the thread.
Stopping the CQ alone is not cancellation. Server shutdown runs outside the I/O
strand and CQ thread, then joins registered handlers.

POSIX descriptor import lives in `transport_posix.cpp`. Imported descriptors are
nonblocking and close-on-exec. Sync uses a parent-created socketpair and spawn
file actions to install only its protocol descriptor at fd 3. Standard streams
are inherited diagnostics; they never carry protocol bytes. The parent owns
termination escalation and waitpid. The runtime APIs used here are the vendored
POSIX `CreateCustomInsecureChannelFromFd` and `AddInsecureChannelFromFd`.

Windows build/runtime is intentionally deferred to the final migration step.
