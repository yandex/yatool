# Larry C++ client

The application boundary is `IBuildService::ExecuteBuild` in
[contracts/interfaces.h](contracts/interfaces.h). The contracts target is shared
by the server and the client; it has no Python or wire-codec dependency. Transport
implementations belong to `libs/larry_client`, server integration to
`components/protocol`, and Python adaptation to `libs/larry_client/py`.

`ExecuteBuild` takes an owned `TBuildRequest`, an `IResultsConsumer`, an
`IProgressListener` and a cancellation token. All methods return Asio awaitables.
The caller keeps both callback implementations alive until the operation has
completed, including cancellation cleanup. Payloads are passed by value so their
strings and paths survive suspension. Owning a path does not own its file.

`TBuildRequest::Environment` is the complete owned caller environment. An empty
map supplies no inherited variables; it never requests service inheritance.
Names and values are UTF-8 strings, names must be nonempty without `=` or NUL,
and values must not contain NUL. Native request validation precedes transport
startup. Environment stays separate from execution policy and typed build variables.

`IResultsConsumer::Consume` returns `TConsumed` or a typed `TDeliveryFailure`.
Either return means that the consumer has stopped accessing the source view.
The server holds that view while the callback is queued and running. Installed
copies and hardlinks belong to the client and survive the return. An exception
fails the interaction, rather than becoming a node execution failure.

`IProgressListener` provides `BuildAccepted`, `UpdateProgress`, `TaskStarted`
and `TaskFinished`, plus optional `ResourceStarted`, `ResourceUpdated` and
`ResourceFinished` for tool preparations. Resource operations have distinct,
increasing request-local numeric IDs; updates carry downloading counters or an
extracting variant. Native validation rejects unknown/reused operations and a
normal completion with active preparations. Resource callbacks use the same
sequential listener and do not change graph statistics. The service completes acceptance before starting other
callbacks. Progress callbacks are sequential; results and progress are separate
interfaces. `TaskFinished` completion releases the borrowed stderr view after
its producer handles have closed. Callbacks must cooperate with cancellation;
no callback can start after `ExecuteBuild` completes.

`TBuildResult` is either `TRejectedBuild` (a reason without accepted statistics)
or `TCompletedBuild`. The latter preserves execution statistics, causal node
failures, publication and consumption histories, delivery failures and an
optional download report. Histories follow first occurrence in `graph.result`.
`Succeeded(result)` requires successful execution, no delivery failures and a
complete download report when present. Transport and unhandled callback failures
throw instead of returning a build result. Cancellation stops admission and joins
owned operations before return; it does not retry the build.

## Migration

The former Python protocol state machine and package have been removed.
`libs/larry_client/py` wraps the native implementation and the ya-bin adapter
implements its consumer and listener interfaces directly. Request-v2 variable
map semantics are retained, with no legacy token field. Download-only requires
a destination and bypasses local storage. Both binaries must be updated or
rolled back together; there is no fallback to the old dialect.

## Implementation status

`TClient` implements one ExecuteBuild over an injected duplex `ITransport`.
Every session is serialized on its own strand. Reader, writer and two callback
workers are joined before returning, including on cancellation. A slow callback
cannot stop reading an incoming service failure. Calls are bounded to one active
call per interface plus one successor during the return/write-completion race;
callback payloads have a 48 MiB budget, records a 16 MiB limit. Invalid concurrent
calls fail immediately, without parking the reader. Application callbacks receive
no wire IDs. Native validation checks graph outputs, task pairs, progress totals,
and final publication/consumption history.

`ExecuteBuild(TExecutors, TConnectionOptions, ...)` connects to a local endpoint
or launches `sync:/absolute/path/to/larry`. Local addresses use the existing
resolver. `ProcessArguments` are passed directly to the child without a shell;
they are rejected for local connections. Bare `sync` is rejected.

On Linux, `TConnectionOptions::Address = "system:larry/build.sock"` connects to
`/run/larry/build.sock`. The Python facade accepts the same address. Resolution
is independent of user identity and XDG settings; it does not launch a daemon.
The service must be configured externally as described in
[local_service endpoints](../local_service/README.md). Existing `local:` and
`sync:` choices remain unchanged.

`Blocking` must progress independently of `Io`: graph serialization runs there
and hands the writer at most two 64 KiB chunks plus its current chunk. The reader
remains active during upload, including early rejection and cancellation.

The existing process runner targets command execution with file/discard stdout;
it does not expose interactive stdin/stdout ownership. The POSIX adapter therefore
owns a socket pair for protocol I/O, a separate stderr pipe, and the child process
group. It drains stderr independently, retains at most 64 KiB of complete lines,
omits oversized lines and redacts request secrets before retaining diagnostics.
Transport failure cancels I/O and joins graph production; cleanup waits for the
child, escalating SIGTERM to SIGKILL. Final results require exit zero. Callback
exceptions retain their identity; remote errors retain their code. No reconnect
or automatic retry occurs. Linux integration covers both transports; macOS was
not available for verification. Windows is outside the current scope.

The Python facade uses the same native operation. The sole ya-bin consumer uses
the new API directly, with no legacy import shim. The RPC method remains `build.execute`, with an updated contract and no compatibility guarantee for older ya-bin/Larry combinations. No downgrade or replay is attempted. Requested node-completion reports must cover every evaluated node, checked against total minus avoided at completion.

## Pending calls

`libs/larry_protocol/detail/pending_calls.h` implements thread-safe correlation for either endpoint.
A registered call is queued until `BeginWrite`; only then may a response arrive.
`FinishWrite` must run for every started write, including failed/cancelled writes.
An early response is retained until successful write completion. A failed write
wins over that response, and operation failure preserves the first exception.
Queued calls fail immediately; writing calls remain pending until I/O completion.
Completion handlers are posted to their associated executors and never invoked
under the table mutex. The owning session must cancel transport I/O and join it;
this table alone is neither an admission queue nor a transport implementation.

## Build CPU limits

`TBuildRequest::Limits.Threads` is an unsigned 32-bit limit. Zero (the default)
selects the service CPU budget; positive values are clamped to that budget.
Node CPU requirements are weighted: a build with limit 6 can run demands 4+1+1.
A node demanding more than the build limit can run alone, reserving its full
CPU/RAM globally and charging the build at most its effective CPU limit.

The service supplies `TBuildAccepted::CpuLimit`. A positive requested limit
requires a positive acknowledgement no greater than that request, checked before
the listener runs or acceptance is acknowledged. Missing/excessive confirmation
fails the interaction without retrying an unrestricted build. Zero requests allow this optional field to be omitted within the current RPC generation.

These are upper bounds for independent requests, without CPU-share guarantees.
Identical nodes in different builds still execute and consume resources separately.

## Additional build limits

`Limits.TestThreads` bounds concurrently executing test nodes (suites/chunks),
independently of weighted CPU. Zero follows the effective build CPU limit.
A test requiring CPU `all` uses test threads capped by service CPU. On an
eight-CPU service, threads 2/test threads 6 reserves six globally and two for
the build; threads 6/test threads 2 can run two such tests plus two CPU-1
ordinary nodes. Ordinary `all` and every `half`/`quarter` remain host-relative.

These policies are baseline prototype functionality with no new applied-limit
acknowledgements, capability probes or retry. The existing positive Threads CPU
acknowledgement remains required. Shared-execution attribution is still deferred.

After accepting BuildResult (including semantic rejection), ExecuteBuild joins
its API workers and runs transport cleanup: wait for server control FIN, flush
client control FIN, then close and join the physical connection. This phase is
cancellable and bounded by 30 seconds. Its failures are reported, not hidden by
the previously received result. RPC errors and cancellation abort owned work.
BuildResult requires graph upload to have reached the server; bootstrap rejection
before graph transfer uses a JSON-RPC error.

## Forced execution

`TBuildRequest::BuildCachePolicy` defaults to `Use`. Select `Ignore` to execute
reachable commands despite warm local/YT artifact caches and retained or active
cross-build executions. Python exposes the same choice as `BuildCachePolicy.IGNORE`.
The ya adapter selects it for both `--ignore-build-cache` and `--rebuild`.
Cache modes remain independent: successful forced execution replaces the local
static-UID entry when local caching is enabled. Output digests are retained,
while forced nodes do not calculate or publish a content UID.

All requests use `build.execute` with mandatory `params.execution_policy`:
`reuse` for ordinary execution and `rebuild` for `BuildCachePolicy::Ignore`.
This is an intentional pre-release wire break; update client and service together.
Missing or invalid policies and the removed forced method are rejected by the
updated service without fallback. The local/YT modes remain independent, and
shared tool preparation remains reusable. Forced execution with YT download-only is
invalid. An ordinary neighbouring build may read the old local entry before the
replacement commits. Existing materializations remain usable afterward. Cache
publication failures retain the existing bounded diagnostics and build-success
policy; they do not count as a successful refresh.


## Bootstrap and failure facts

The native client sends `build.execute`, waits for `bootstrap.accepted`, sends its
null acknowledgement and then starts bounded graph serialization/upload. Admission
is connection protocol state; it does not invoke `IProgressListener`. Callback IDs
continue at `s:2`. Cancellation remains active while waiting for admission.

Native failures retain `EFailureStage` (Connect, BootstrapBeforeAcceptance,
AcceptedSession). A validated admission moves to AcceptedSession before writing
the acknowledgement. `TTransportError` additionally exposes
`ETransportFailureReason` (EndpointMissing, ConnectionRefused, PermissionDenied,
Other); `TRemoteError` preserves Code and Stage. `TClientProtocolError` derives from
the existing protocol error and adds Stage. Callback exceptions remain primary.

A stage is an observation, not retry permission. EOF before admission does not
prove rejection; a later `method_not_found` is not an entry-method rejection.
The native client performs no automatic retry or service installation/update.

## Build status text

`progress.update_progress` snapshots may include optional `status_text` (native
`TProgress::StatusText`, Python `Progress.status_text`). Text is opaque presentation
data, not a reason enum or execution policy. A nonempty value replaces the current
build-wide explanation; omission or an empty string clears it. Counter-only
snapshots from older services remain valid. Status-only changes may repeat all
counters unchanged. Every later snapshot includes the current text.

The wire value must be a string containing valid UTF-8, at most 4096 decoded bytes;
null, other JSON types, invalid encoding and larger values fail before dispatch.
Existing record bounds and RPC acknowledgements still apply. Text is rendered
without interpreting markup or terminal controls and removed when the call ends.

A new client accepts an old service with no text. The current pre-change native
codec ignores the additional progress member, so a new service remains usable by
that client. Neither this field nor new explanation wording requires a new RPC
method, protocol-version bump or forced service refresh. Unrelated incompatible
protocol changes retain their existing failure and deployment policies.
