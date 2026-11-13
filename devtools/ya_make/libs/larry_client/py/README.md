# Python facade

For explicitly selected gRPC transports, `BuildRequest.server_delivery` accepts
`ServerDeliveryOptions(nodes={uid: (ResultDestination(...), ...)})`. Each result
UID must be present, including outputless nodes. Destinations select a declared
logical output, absolute root, relative path, `kind='explicit'|'source_link'` and
`expansion='none'|'directory'|'contents'`. A contents destination may use `'.'`
to merge archive children into its root. Support is checked before graph upload;
legacy rejects this option before connecting. Installed results are reported in
`BuildResult.delivered_results` as `DeliveryReport` values with final paths and
warnings. The result consumer is not called for server installation. Existing
result consumers retain their callback/ACK lifetimes when the option is absent.

`ServerDeliveryOptions.replaceable_symres_roots` identifies previous owned ya
stores whose public links may be replaced during migration. Larry never collects
those old stores; unrelated links remain protected.

`LarryClient().execute_build(address, BuildRequest(...), results_consumer,
progress_listener)` returns `BuildResult`. Rejection is an ordinary result with
`execution == 'rejected'`, a reason, and no statistics. Execution, consumption and
download outcomes contribute independently to `result.succeeded`.

Implement `consumer.consume(ResultView)` and the `ProgressListener` methods.
`consume` returns `None`/`Consumed` or `DeliveryFailure`. Listener methods return
`None`. Files are borrowed until the callback returns: install outputs and copy
stderr there. A missing consumer discards results; it does not install them.

All callbacks execute synchronously on the thread calling `execute_build`. Native
I/O continues on a separate thread while the caller waits without the GIL. The
binding checks Python signals during idle waits. Cancellation joins native work
before returning and never interrupts an arbitrary executing Python callback.
The original callback exception and traceback are re-raised after cleanup.
Coroutine callbacks and re-entry on the same active client are rejected.

The optional keyword-only `on_tick` callback refreshes local UI during quiet
builds. The existing native dispatch loop calls it at most once per 100 ms on
the calling thread, including when RPC events stop arriving. It takes no
arguments and returns `None`; async callbacks are rejected. No thread, RPC or
per-resource timer is added. Missed ticks are not queued. Tick failures use the
same cancellation and exception-preservation path as other callbacks, and no
tick runs after the call returns.

Python owns value adaptation and application policy only. It does not import a
transport, parse JSONL, correlate IDs, or implement protocol ordering checks.
Build variables are copied into native owned values before I/O starts. There is
no compatibility forwarding API for the previous client package.

`BuildRequest(source_root, graph, environment, ...)` requires an explicit
build environment dictionary. `test_environment` defaults to an independent empty
dictionary; both maps are copied on construction and never fill one another. For example:

```python
request = BuildRequest('/source', graph, environment={}, test_environment={'VALUE': ''})
```

An empty dictionary means no caller inheritance. The ya adapter captures the caller environment once and applies two independent policies. The build policy is initially empty; the test policy is built into ya-bin in `devtools/ya/build/larry_client/environment.py`, with per-name reasons. Global repository configs are unchanged.

The config-only `larry_environment_whitelist` and `larry_test_environment_whitelist` options in a user TOML `ya.conf` add exact names to the respective built-in policy, for example:

```toml
larry_environment_whitelist = ["CUSTOM_BUILD_ENV"]
larry_test_environment_whitelist = ["CUSTOM_TEST_ENV"]
```

These additions do not replace built-in defaults: test commands still inherit PATH, `_YA_LLD_THREADS` and the other built-in test names. Missing config or `[]` adds nothing. Standard config ordering and `[make]` sections apply to the additions; a later list replaces earlier additions, while defaults remain. Duplicate names have no effect. A custom name can therefore be enabled for the next invocation without a binary release or service restart. Changing or removing a built-in name requires updating ya-bin.

Only present exact names are copied, including empty values. Invalid config list/name shapes fail option validation. The initial test policy covers program lookup, user/system paths and identity, proxy/CA, TZ, SSH_AUTH_SOCK and `_YA_LLD_THREADS`. Unknown/token and injection names are omitted unless explicitly added. Graph env and explicit SDK request dictionaries are independent of this caller-side policy.

The server selects the test map only for `kv.p` TS/TM/TL/YT; compilation,
linking and test-preparation/reporting nodes use the build map. There is no
cross-map fallback. LC_ALL=C, LANG=en and a build-owned WINEPREFIX are added to
both bases before the server selects
nonempty command env or node env. These graph maps are alternatives, not merged
layers. Existing typed graph substitution applies to the selected values.
Node TMPDIR/TEMP/TMP are assigned last. The server never fills omitted names from
its own process environment and never mutates that environment for a build.

Requests use `build.execute` with mandatory `params.execution_policy` (`reuse` or `rebuild`),
`params.environment` and `params.test_environment`. Client and service must be updated together. The existing
pre-release method is extended in place; there is no separate versioned method.
Missing execution policy or either environment map is rejected. Use matching client/server binaries.
Both maps are sent once per build, not once per node.

Snapshots do not participate in UID-based sharing or artifact-cache identity.
If two builds join one UID, the physical execution uses its producer's environment,
even when the subscriber supplied different values. The producer's session storage
keeps its WINEPREFIX alive until shared work and its remaining holds complete.

The ya-bin adapter implements both callback interfaces directly. It materializes
outputs before returning from `consume`, translates expected filesystem and
invalid-output failures into `DeliveryFailure`, and maps `BuildResult` to the
existing nine-element runner tuple. A rejected request raises a client error with
its reason. A delivery failure sets a failing exit status while retaining other
successfully delivered results. Cache defaults remain read-write/read-through.
For sync, the adapter owns a private temporary YT token file through native child
cleanup; local builds use the service's credentials.

Use matching ya-bin and Larry binaries. Earlier unreleased servers may ignore
the environment field; compatibility with them is not guaranteed. The client
does not retry another dialect or replay a build. No transitional server or
old-package import shim is provided.

Use `BuildRequest(..., limits=BuildLimits(threads=6))` for a per-build CPU limit.
`threads` defaults to zero (service-only admission) and accepts exact Python ints
in 0..4294967295. Invalid types/ranges fail before transport startup. Both sync
and local transports carry the same owned options without another graph copy.
`BuildAccepted.cpu_limit` exposes the applied limit after native validation;
positive requests fail against peers that do not acknowledge it.

The ya adapter forwards the already resolved `opts.build_threads`, preserving ya's
flag/config/environment/default precedence. Direct SDK zero requests execution
with the service budget; ya's existing zero-thread graph-only path does not
execute a build. CPU accounting is weighted, including progress for a heavy node
at `-j1`, and is an admission bound rather than OS thread enforcement or a CPU share.

Additional options share the same `BuildLimits` value:

```python
BuildLimits(threads=6, test_threads=2)
```

`test_threads` counts executing test nodes, not cases or native process threads.
Zero follows effective build CPU. CPU remains weighted: with threads 6/test
threads 2, two CPU-1 tests leave room for four CPU-1 ordinary nodes. A test's
CPU `all` resolves against test threads capped by service CPU, so test threads 6
with threads 2 reserves six globally and two for the build on an eight-CPU host.

Debug options preserve their existing one-test policy. No new limit acknowledgements
or feature probes are required; the existing positive CPU acknowledgement stays.
Cross-request execution attribution remains deferred.

## Failure metadata and transport generation

The package exports `TRANSPORT_VERSION` from the C++ protocol constant, plus
`FailureStage` and `TransportFailureReason` from the native binding. It contains no
Python protocol state machine or duplicate transport-generation value.

Exchange exceptions carry `stage`: `FailureStage.CONNECT`,
`BOOTSTRAP_BEFORE_ACCEPTANCE` or `ACCEPTED_SESSION`. Endpoint `TransportError`
exceptions additionally carry `reason`: `TransportFailureReason.ENDPOINT_MISSING`,
`CONNECTION_REFUSED`, `PERMISSION_DENIED` or `OTHER`. `RemoteError.code` remains the
remote code. Input validation and original Python callback exceptions retain their
existing behavior; callback exceptions are re-raised with their traceback.

Only the service orchestration layer may decide on recovery. Neither an absent
admission response nor a bare error code proves that retry is safe. The native
client does not retry, launch an installer, query versions or control a service.

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

## Verbose and timing diagnostics

Select `BuildRequest(..., command_reporting=CommandReporting.ATTEMPTS,
timing_reporting=TimingReporting.STAGES)` to receive typed `NodeCompleted`
observations through `ProgressListener.node_completed`. Each evaluated node is
reported once, including cache restoration and first-launch failure. Defaults are
off. The bootstrap method remains `build.execute`; its contract changes in place without compatibility guarantees for older peers. Requested completion callbacks are mandatory for all evaluated nodes; incomplete coverage is a protocol error. Individual optional measurements may remain unavailable.

`commands` describes actual attempts; each `CommandDiagnostic.stderr_bytes` is the required wire count of that command's stderr bytes. Slice an owned binary copy of TaskFinished output by these counts before decoding or masking to preserve the order of commands and their stderr. The borrowed file still expires when its callback returns. Command diagnostics preserve argument boundaries and redacted
declared environment/cwd; `argv=None` and `commands_complete=False` indicate missing
or invalid representations. Command representations have no dedicated length or item-count limit; the existing general RPC record limit still applies. An unattempted command tail is not reported.
`timing` is measured execution clipped to this request's scheduler-submission time after its own preparation (or its successful cache operation); resource/admission waiting is excluded. Optional `preparation` measures early setup. `preparation_parent_uid` records which parent actually started a dependency, preserving causal critical paths without attaching late joins. `stages`
contains physical execution and request cache/resource observations. Cached nodes
retain measured restore intervals without process callbacks. Rendering, shell
quoting and adaptation to existing --stat reports belong to the ya caller.


`TaskFinished.stderr_chunks` retains raw per-command stderr byte boundaries for non-verbose output. `FailureDetails.display_message` separates a concise user diagnosis from internal `message`; optional `command` is an owned `CommandDiagnostic`, with actual `process_id` and optional typed `CommandSystemError`. Applications choose presentation and newline conversion after slicing; structured failure metadata is not a preformatted CLI footer.


`ProgressListener.task_output(TaskOutput)` receives a borrowed readable stderr-file range while a task is running. Copy `size` bytes at `offset` from `output_path` before returning. `command_started=True` denotes a zero-byte command boundary; other frames contain at most 65536 bytes. Callbacks arrive in command order and contiguous byte order, before TaskFinished. Boundaries include silent commands. The ya adapter uses these events for legacy live status, dynamic tags and separate messages; final TaskFinished still provides the full raw stderr journal.

## Ordered cache layers (transport generation 2)

Client and service must be upgraded together. `BuildRequest.cache_layers` replaces
`local_cache_mode`, `yt_cache_mode` and top-level `download_root`; old arguments
are rejected. SDK defaults are local then YT. Pass `cache_layers=()` to disable
all artifact caches. Each backend may occur at most once; order is preserved.

```python
from devtools.ya_make.libs.larry_client.py import (
    BazelCacheOptions, BazelCacheStore, LocalCacheOptions, YtCacheOptions,
)
layers = (
    LocalCacheOptions(), YtCacheOptions(),
    BazelCacheOptions(stores=(BazelCacheStore(base_uri='https://cache.example/prefix'),)),
)
request = BuildRequest(source_root, graph, cache_layers=layers)
```

Bazel performs static-UID HEAD/GET only, validates decoded CAS bytes and skips
content-UID lookup. `CacheLookup.LATE` omits the preliminary HEAD. Per-endpoint
Basic credentials are owned by the request and passwords are excluded from
repr. A nonempty `stores` tuple specifies independent candidates; AC and CAS
are always fetched from the same candidate. Recoverable failure continues to
later stores and cache layers, then normal execution. Forced execution bypasses
all cache reads. If local is included, successful remote results may be published
locally regardless of its position in the read list.

`YtDownloadOnlyOptions(download_root=...)` is an exclusive single layer.
Bazel writes, warming and manifest generation are unsupported.
`TaskStarted`/`TaskFinished` identify remote activity with `backend='yt'|'bazel'`.
`BuildResult.cache_statistics` contains `backend`, `restored_nodes` and
`restored_file_bytes`; bytes count uncompressed regular files before finalization,
including auxiliary metadata, excluding symlinks. Only finalized winners count.
Local publication does not count as another restore.

The ya launcher defaults to local → YT → Bazel, omitting disabled remote caches.
Use `--larry-no-local-cache` (or `larry_local_cache=false`) to omit only local,
`--no-yt-store` to omit YT and `--no-bazel-remote-store` to omit Bazel. Explicitly
select `--larry-runner sync:/absolute/path/to/larry` or `--larry-runner local:`.
Bazel uses the existing `--bazel-remote-store`, `--bazel-remote-base-uri`,
`--bazel-remote-caches`, authentication, timeout, eligibility,
`--dist-store-threads` and `--dist-cache-late-fetch` settings. A nonempty endpoint
list overrides scalar settings; a nonempty password overrides its password file.
No fallback to the old runner occurs if Larry rejects a request.
