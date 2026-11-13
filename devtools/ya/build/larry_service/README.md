# Managed Larry recovery policy

This library implements recovery policy, release preparation and installation
(steps 1–8 of the service delivery plan). Linux/macOS production dispatch connects
the explicit `local:` mode to delivery, durable installation and the user OS manager.

The beta runner is explicitly enabled with `larry_betta_addr` in ya.conf or the
hidden `--larry-runner` CLI override. `YA_LARRY_ADDR` and the old `larry_addr`
configuration key no longer select Larry. Protocol compatibility is maintained within beta; the transition between beta
and stable may break compatibility. `larry-betta` uses the separate LARRY/YATOOL/prestable publication stream; `larry-latest`
continues to use stable.

Only the original shortcuts `local:` (legacy) and `grpc:local:` (opt-in gRPC) are managed.
`resolve_address` expands legacy to `local:ya_make/larry-{TRANSPORT_VERSION}`
using the native constant, and gRPC to its separate versioned namespace (see below). None, sync
and explicit endpoints retain their existing mode, including an explicit address
identical to the managed expansion.

`ManagedLarryClient(exchange, ensure_service).execute(address, *args, **kwargs)`
forwards arguments unchanged to the synchronous exchange. A managed first attempt
may recover only after a typed missing/refused connect failure or entry-method
rejection before admission. All native cleanup must finish before exchange raises.
A successful recovery hook promises readiness; then exactly one retry runs.
Recovery or retry failure remains primary, with `first_attempt_error` preserving
the original failure for diagnostics. Failure classification never parses exception messages.
Cancellation and callbacks without a recoverable native observation do not retry.

The recovery hook calls `check_release_dates` on the client build date and available official Larry UTC dates
before download/restart. It must stop on OUTSIDE_WINDOW, and may continue on
UNKNOWN while reporting the missing guarantee. A ready binary must be checked
again before activation in later delivery steps. The pure filter requires an
explicit nonnegative whole-day timedelta; equality is included, leap days count.
WITHIN_WINDOW is not proof of compatibility. The ya-bin build date is trusted without checking client release provenance.
Production uses 540 inclusive calendar days. Missing dates or unknown Larry
provenance produce UNKNOWN. This is a recovery heuristic, not a release promise.

Unit tests inject delivery and lifecycle operations; opt-in native tests exercise
real isolated systemd units and concurrent recovery.


## Release preparation (delivery step 3)

`release.prepare_release()` calls the existing `tools.tool('larry-betta',
with_params=True)` interface with ordinary selection and download defaults.
TTL, environment settings, process memoization and explicit resource mappings
retain their existing meaning. Preparation neither guarantees offline lookup
nor forces latest selection refresh; these are deferred follow-up ideas.

The result contains the resource root, executable, resource URI and nullable
release metadata read from the actual binary. The adapter captures canonical
paths and the existing deployment `lnk` manifest under the existing tool alias
lock, then probes the binary before releasing it. On POSIX canonical paths
resolve the alias to the concrete resource directory. These are borrowed cache
paths, not durable installations or leases; Windows alias trees also retain
ordinary tool-cache semantics. The installer copies and revalidates its source and handles cache disappearance
without publishing a partial install.

Version inspection uses a finite timeout, bounded nonblocking pipe reads, discarded stderr and
joined kill/reap on cancellation or failure. It accepts metadata format 1 and
does not infer official provenance or compatibility from a date or release ID.
Tool resolver, metadata reader and cancellation checker can be injected.
Failures propagate without producing a prepared candidate. Preparation itself does not install or restart the service.

`ReleasePreparationError` identifies candidate preparation failures; its subclass
`ReleaseMetadataError` identifies version probe and metadata failures.


## Independent installation (delivery step 4)

`ServicePaths(absolute_root)` names private service storage outside tool cache.
The caller can choose the root explicitly or use the persistent default from
`default_service_paths()` (step 5). The root and installation directory must be owner-only and owned by the
current user. Linux/macOS installation is supported; Windows service runtime is
not. Parent aliases such as macOS `/var` are resolved; a symlink root is rejected.

`prepare_installation(paths, candidate, origin=ReleaseOrigin.UNKNOWN)` copies
all files and internal links through staging, verifies content/modes/resource
identity and probes the copied executable. Escaping or dangling links fail.
The immutable result has a unique `artifact_id`, separate from the resource URI;
the future service configuration can use it as `installed-artifact-id`.
Prepared copies survive removal of tool cache. Copy failure/cancellation never
selects a partially prepared artifact.

`activate_installation(paths, artifact_id)` verifies the copy and returns
`ActivationResult(selected, decision)`. It atomically switches `current` only
for first selection or a strictly later official release date. Same resource
and metadata return `ALREADY_SELECTED`; unproven forward order returns
`NOT_NEWER`. Neither opaque IDs nor same-day timestamps provide an order.
`origin` must come from trusted release-source metadata; the installer does not
infer OFFICIAL from `--version`, a resource URI or the name `larry-betta`.
Production qualification queries the exact Sandbox resource ID for LARRY,
owner YATOOL and prestable release membership. Current tool configuration alone
cannot qualify a resource retained in the alias cache. Unavailable metadata
leaves UNKNOWN; it never bypasses conservative replacement. Recovery requalifies
a previously UNKNOWN installed resource before replacement, so a transient
metadata outage at first installation does not permanently prevent updates.
The stable launch path is `paths.executable` (`current/larry`), a POSIX shell
launcher that execs the absolute executable inside the selected artifact and
forwards its arguments. This preserves companion-file paths relative to the
executable without retaining a shell parent process. Selection does
not restart any process and is not evidence of readiness.

All operations share a cancellable interprocess lock, including preparation.
`selected_installation(paths)` observes the selected manifest under that lock.
`cleanup_installations(paths, can_remove=...)` never deletes current, and only
deletes other ready copies when the callback confirms all process users have
exited and no pending launch can use them. By default it retains every ready
copy. The callback runs under the lock and must not reenter installer APIs.
Future manager integration must synchronize this proof with launches and
restarts; PID files and missing/stale readiness records do not prove it.
Abandoned staging and interrupted obsolete-copy deletion are recovered under
the same lock. No rollback or power-loss/fsync guarantee is provided.


## Persistent configuration and credentials (delivery step 5)

`default_service_paths()` on Linux/macOS chooses `<XDG_DATA_HOME or ~/.local/share>/ya/larry/service`;
Windows defaults retain `<transport-version>` as the last path component;
`data_home=` overrides that choice and must be absolute. These paths are outside
ordinary tool cache. `ServicePaths(root)` remains available for an explicit root.
`state_file` and `token_file` identify shared process-observation and credential
files under the private root. Cache placement is an explicit caller input.

`write_configuration(paths, artifact_id, ServiceOptions(endpoint, local_cache_directory, yt_proxy_role))`
writes owner-only TOML next to that installation's manifest, outside its hashed
artifact. `ServiceLaunch.argv` binds the exact installation launcher and config;
it does not start a process. The file contains endpoint, absolute local cache
and readiness paths, installed artifact ID, token-file path and optional proxy
role. It contains neither the token nor a copy of the invoking client's environment.
Different prepared installations have separate configurations. A future manager
must coordinate their use with selection/restart and process-instance checks.

`update_credentials(paths, token)` atomically replaces an owner-only persistent
file using a temporary file created with mode 0600. Cancellation or failure
before replacement leaves the old file intact and removes the temporary file.
Empty/missing credentials preserve the old file; first use without credentials
leaves it absent, retaining Larry's existing YT failure/fallback policy.
`prepare_credentials(paths, options, token_resolver=...)` skips lookup when
`options.yt_store` is disabled; otherwise it delegates selection to the supplied
resolver. Production integration in step 7 will pass existing
`ya_make.get_yt_token`, preserving its resolved-YT/OAuth/user fallback policy
without a circular dependency on the build orchestrator.

Configuration and token writes use the installation lock. A real native-factory
process test verifies that new sessions see the new token after rotation while
old sessions keep their captured credentials. No new server API was needed.
Linux process tests also start installed Larry with the generated TOML and check
its readiness identity. Native macOS qualification is described in step 8; Linux OS-manager
qualification is described in step 6 below.
The APIs are not connected to production dispatch; sync and explicit endpoints
retain their existing behavior. No power-loss/fsync guarantee is added.


## Linux user manager (delivery step 6)

`SystemdServiceManager` implements `ServiceManager`: `ensure_installed`, `status`,
`start`, `restart` and `wait_ready`. Pass the durable `ServicePaths` and the exact
`ServiceLaunch` from step 5. The production unit name is `ya-larry.service`, independent of transport generation
and storage root; logs are available with `journalctl --user -u ya-larry.service`.
Tests pass an explicit unique `unit_name=` along with their isolated root.
It does not replace a manually installed `larry.service`.

The unit name is the stable service identifier. The adapter addresses only this
unit and does not discover or migrate development installations under other names.
Conflicting registrations from different data homes fail rather than overwrite
the existing unit.

Installation publishes a private unit under the service root, registers it with
`systemctl --user link` and reloads the manager. Repeated equal installation
preserves the unit file and never restarts a process. Start/restart run through
systemd; they do not send signals to a readiness-record PID. The unit uses
`Type=exec` (systemd 240+), journal logging, a 20-second stop timeout, complete
cgroup termination, delayed on-failure restarts and a three-starts-per-minute
limit. No sudo, automatic linger, boot enablement or client environment import.
An unavailable user manager produces a diagnostic failure.

`wait_ready(ReadinessExpectation(artifact_id, endpoint, previous_run_id))`
requires an active/running unit, matching record, live PID/start ticks and stable
manager invocation. A previous run ID cannot satisfy a replacement wait.
Commands and readiness have finite deadlines and observe cancellation. Stopping
the controller does not undo a manager job already submitted. The result is a
verified observation, not a guarantee that the process cannot exit afterwards.

The future step-7 orchestrator must serialize selection, configuration, restart
and cleanup across clients, and retain all potentially used installations.
These low-level operations are not that transaction and are not connected to
production dispatch. The exact oldest distribution remains unqualified.

Native systemd qualification is explicitly enabled with
`ya test -r devtools/ya/build/larry_service/it --test-param larry-systemd=1 --retest`
(from a project directory, use the appropriate relative target). It requires an
available user manager, uses uniquely named units and separate cache/state,
and stops/unlinks them afterwards. Without the parameter the native checks
are skipped; ordinary deterministic tests still run. The tests cover real
installed Larry replacement and forced cleanup of a stubborn child process.


## Production recovery (delivery step 7)

Before the first native call, observe only the local run ID; this is not readiness
proof. On an eligible failure, hold a separate cancellable recovery lock through
readiness. Recheck the live instance through systemd and reuse a changed run.
Reuse the selected installation for a missing service; for a rejected running
artifact, prepare and select a strictly newer qualified release. An unchanged
candidate is a diagnostic failure, not an update.

The service owns `<service-root>/cache`. Linux/macOS clients share one control root,
selected installation, recovery/installation locks, credentials and readiness
across transport generations. RPC endpoints remain transport-versioned; an
incompatible older client may fail, but cannot select an older release over a
newer official one. A client requesting an older managed transport fails with an
instruction to update ya, even when a more recent release is available. A selected installation keeps its configured endpoint;
a client requesting another transport must select a strictly newer release
before changing that binding, even when the service is stopped. Production
writes this binding before publishing `current`, so interruption after activation
does not let another client bind that release to an older endpoint.
Client build cache options do not move this shared storage. Existing credential
selection publishes nonempty YT credentials to the service's private token file;
empty or disabled credentials do not erase it.

Lifecycle errors identify the stage; the adapter redacts both request failures.
Cancellation before retry prevents another RPC. A failed restart has no automatic
rollback. Windows service management remains unsupported; explicit
endpoints and sync keep their existing behavior. Latest selection keeps ordinary
tool-cache freshness rules; forced refresh and offline-only checks remain deferred.

## macOS user manager (delivery step 8)

`LaunchdServiceManager` implements the same interface and is selected for Darwin.
Only the invoking user's Aqua GUI session is currently supported; SSH/background
contexts fail explicitly. The production label is `ru.yandex.ya.larry`, with shared
`<data-home>/ya/larry/service` control state across transport generations. Native
tests explicitly inject unique `label=` values and private roots. The production
label is the stable service identifier; jobs under other labels are ignored.
Lifecycle commands target `gui/<uid>/ru.yandex.ya.larry`, and
status uses the documented `launchctl list` table after checking managername and
manageruid. Native qualification covers macOS 15.7.9 (24G830), arm64, in Aqua
for the invoking UID. Other macOS versions and SSH/Background sessions have
not been qualified; Linux tests use fakes.

The private plist uses literal ProgramArguments, RunAtLoad, unsuccessful-exit
KeepAlive, ten-second throttling and bounded ExitTimeOut. No client environment
or token values enter it. stdout/stderr log files live under the private root;
log rotation is not implemented. The plist is not installed in an auto-login
LaunchAgents search directory; registration is performed by managed recovery.

Publishing desired configuration does not change a running job. Start/restart
use bootout and bootstrap to load the selected artifact's exact arguments.
Before bootout, an observed old process identity is saved in a private pending
stop record. Cancellation leaves that record intact; subsequent recovery waits
for the old PID/start identity to disappear before bootstrapping a replacement.
Readiness uses TProcessUID's native units via the existing psingleton binding.

Orderly SIGTERM shutdown lets Larry cancel its command groups. Launchd's job
process-group cleanup does not cover Larry's separate command groups after a
hard kill; no such guarantee is made. Do not substitute kickstart -k and claim
it preserves command cleanup.

Native qualification (run from devtools/ya on a logged-in macOS desktop):
`ya test -r build/larry_service/it --test-param larry-launchd=1 --retest`.
It uses unique service roots/labels and covers first start, persistence after
the starting client exits, installed-artifact replacement under an active build,
normal and SIGTERM-ignoring command cleanup, stale readiness, controller timeout
and cancellation, interrupted-stop recovery, the current-UID domain guard,
crash throttling, stop deadlines and private log permissions. The suite is
MEDIUM because native lifecycle deadlines exceed the SMALL suite budget.
Without the parameter it skips; enabling it on Linux fails rather than claiming
qualification. A separate hard-kill observation records whether an independent
command survived, then explicitly cleans it. On the qualified host that command
survived and the request remained open until it was killed. This does not change
the orderly-shutdown guarantee or provide hard-kill containment.

## Diagnostic bundle

After a build using `--larry-runner=local:`, run `ya dump debug last`.
The existing `debug.json` contains `larry_diagnostics`: transport version,
client release date, runner address, safe configuration fields, build time window, outcome and
service run/artifact/release identity before and after execution. These records live
in ya's evlog and survive build-root cleanup. Capture starts before managed-service
path/credential preparation; setup errors retain the same structured report as
transport errors. Available native failure stage, reason and code are retained.

The adapter can recover the server request UUID from callback paths in the
current `sessions/<request-id>/stderr/stderr-N.log` or
`sessions/<request-id>/results/<view-id>/<slot>` layout. The ID is persisted
before callback processing and ACK, so cleanup or a later callback failure does
not erase it. This is optional evidence from the current layout, not a wire
contract: unknown paths are ignored, and conflicting IDs disable correlation.
No ID is guessed from the service run ID, source root, timestamps or error text.
Early rejection, preparation failure and builds without such callbacks can leave
the ID unavailable; a future shared execution path may also prevent correlation.

The dump always retains bounded common service logs with `service_context`
scope. Untagged events, multiline failure details, and concurrent requests can
explain shared resource/cache or supervisor failures and are not discarded based
on request identity. Linux collects up to 200 journal entries for the saved build
window, with a five-second timeout and 64 KiB output limit. macOS reads the most
recent 64 KiB from each launchd stdout/stderr file. Missing, rotated, inaccessible
or oversized sources are reported as unavailable; later/concurrent traffic can
exhaust those budgets or displace relevant records. Collection never starts,
stops or contacts the service.

When an ID was recovered, each available log also includes `request_matches`, a
supplementary `request_tagged_lines` view derived from that same retained,
redacted context. Its `within_retained_context` coverage means that an empty
view does not prove an absence of server activity. Untagged multiline details
remain in the main text; they are not assigned to a request in this view. The
view has no separate query or budget and cannot restore records missing from
the common context. If the ID is unknown or conflicting, the view is omitted
and the same common context is retained.

The scope describes the Larry section only: the standard dump also includes
shared ya logs and evlogs from the current and previous day, so the whole archive
is not isolated by request. Complete protocol-level correlation remains tracked
separately in YMAKE-2416.
Known ya credentials and the current service YT token are suppressed. Credential
files, full graphs, caches and user files are not added by this collector.

Explicit endpoints and synchronous runners retain their client-side outcome;
the report states that automatic server-log collection is unavailable because
these processes are not owned by the managed local service installation.

`last` selects the newest readable saved invocation, not the last failed build.
Other builds starting later can change the selected invocation; compressed logs
of an active build may not become readable until their buffers flush. Verify the
printed command/time before using a bundle for a failure, or list saved runs with
`ya dump debug` and select the matching item number. The list is not a stable
snapshot while new invocations appear.

### Optional gRPC endpoint

`ServiceOptions.grpc_endpoint` adds the TOML `grpc-endpoint` next to
`listen-socket`. Readiness matches the requested protocol's address, while
both addresses share the same process/run identity. Legacy `local:` remains
the beta default. `grpc:local:` explicitly selects the managed gRPC namespace
`grpc:local:ya_make/larry-grpc-1`; an explicit full gRPC address remains unmanaged.

Recovery of a missing/refused managed endpoint retries the same protocol once,
only before acceptance. Enabling gRPC preserves the installed legacy address;
recovering legacy retains the optional gRPC address. A selected binary lacking
the new configuration option fails startup explicitly. Generic gRPC bootstrap
errors (including UNIMPLEMENTED) do not authorize recovery or legacy fallback.
The gRPC namespace generation is independent of the frozen legacy version.


## Embedded configuration template

`service.toml.template` is embedded through `RESOURCE`. `configuration.render_configuration`
uses `string.Template.substitute`: unused inputs are allowed, while unknown slots
and malformed templates fail explicitly. The beta slot contract may evolve;
stable compatibility is not promised by this change.

The `endpoint`, `local_cache_directory`, `service_state_file`, `installed_artifact_id`
and `yt_token_path` slots contain complete TOML string literals. Do not surround
slots with additional quotes. Literal dollar signs in the template use `$$`;
substituted values are not expanded recursively. `yt_proxy_role_setting` is an
optional whole assignment: empty for `None`, otherwise `yt-proxy-role = <quoted value>`.
This preserves the existing distinction between absent and empty roles.
`grpc_endpoint_setting` likewise contains an optional `grpc-endpoint` assignment.
When gRPC is requested, its endpoint must also match after rendering.

Before publishing `service.toml`, the renderer parses TOML and verifies the exact
endpoint, state-file path and installation identity. Recovery validates a new
candidate before activating it. Invalid templates leave an existing configuration
unchanged and do not start the service. Error messages omit configuration contents.
Other settings may be omitted by a template; service-specific validation belongs
to Larry. The embedded template preserves the existing startup settings.

Only the embedded template is used at this stage. Delivery alongside Larry and
selection from the installed artifact are subsequent steps in
[the migration plan](../../../ya_make/plans/service-configuration-template.md).
