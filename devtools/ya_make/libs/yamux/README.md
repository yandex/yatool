# Yamux

C++20 coroutine implementation of Yamux v0 over a caller-provided duplex
transport. No Larry, JSON, socket discovery, process launch or EDL dependency.
The caller supplies a Boost.Asio executor, owns its event loop and keeps it
running until session Join completes. See [compatibility](compatibility.md).

## API contract

`TSession(executor, transport, role, options)` owns the transport and starts its
reader/writer. `OpenStream` returns before peer acknowledgement; rejection can
therefore surface on subsequent IO. `AcceptStream` returns incoming streams.
Both return `unique_ptr<TStream>` with a stable `Id()`. Transfer the owning
pointer; the `TStream` object itself is neither copyable nor movable.

Each stream allows one `ReadSome` and one `WriteSome` concurrently. A second
operation in the same direction is an error. Read returns `{Bytes, State}`;
a zero-length buffer returns Data with zero bytes, never EOF. Write returns the
accepted prefix length; completion releases the caller buffer, without promising
peer application consumption. `FinishWrite` sends FIN after preceding writes
and preserves the read direction. `Flush` waits for queued stream frames and the
current physical write to complete; it shares the single-writer restriction.
Use FinishWrite followed by Flush before closing a connection whose final output
must be sent. Neither WriteSome nor Ping is a flush barrier.
A subsequent physical transport failure does not invalidate frames whose complete
physical writes succeeded, even when EOF is processed before the write completion
resumes. Flush waits for that completion and succeeds only if every accepted
stream frame was written; partial, failed or discarded writes still fail.
Explicit local Close and stream reset remain cancellation.

After physical transport loss, already received data followed by FIN remains
readable through EOF. Incomplete streams fail. Explicit local Close still cancels
reads, including reads of buffered data.

`Close()` is an ordinary, idempotent function: it immediately posts termination
to the session strand without awaiting IO. Stream close resets both directions;
session close aborts the physical transport and all its streams. Destructors use
the same path. `Ping` is correlated and bounded. `GoAway` stops accepting new
incoming streams while preserving existing streams; it is not session Close.

Cancellation is expressed by closing the owning channel. There are no per-call
cancellation tokens. Close or destruction of a stream owner wakes its pending IO
with `EError::Closed`, without terminating unrelated streams. To stop a pending
Accept/Ping, close its session. A ping timeout only ends that ping; a physical
write timeout aborts the connection. Timers wait for actual deadlines, without
periodic polling when the connection is idle.

Pending operations retain internal state, not the public handle. Creating the
awaitable captures that state before initial suspension, so destroying the owner
also safely terminates an operation created but not yet awaited. A high-level
task must not retain the owning handle inside the coroutine it wants to cancel:
keep ownership in its controlling scope and pass a borrowed reference for IO.
The owner must outlive calls through that reference, but may be destroyed after
an operation has captured its state.

Close initiates termination; it does not synchronously release caller buffers.
Keep buffers and pending coroutine frames alive until their operations complete.
Session `Join()` drains internal workers and entered operations; await your own
coroutines/completion handlers separately before freeing their state. Keep the
executor running throughout cleanup. Physical writes own header/payload storage:
stream close cannot insert RST into a partially transmitted frame.

Native Asio cancellation is deliberately disconnected at the session boundary.
An Asio signal alone does not complete an already-started operation. Timeout
compositions must close the owning stream/session and await operation completion.

`TError::Code()` distinguishes local close, peer reset/rejection, local resource
limits, concurrent use, ID exhaustion, protocol error, transport error, closed
state, GoAway and timeout. A transport exception's message is retained; no
application payload is logged. Transport failure is not normal stream EOF.

## Budgets

The initial receive window is reserved before admission. ReceiveBudget can lower
the number of streams below MaxStreams. SendBudget bounds copied queued payload;
OutgoingChunk bounds our outgoing frames, not peer frame sizes. Control frames,
pings and incoming accepts have separate limits. Peer-ID history merges adjacent
same-parity IDs into intervals, capped by MaxPeerIdIntervals; exceeding that cap
is a session resource error. History is never discarded to admit new IDs.

Existing project helpers were inspected: `libs/async_result` is a one-shot result,
`libs/coro_runner` owns test execution and `libs/local_service` manages endpoint
lifecycle. None is a duplex transport abstraction. The small injectable
`ITransport` contract stays here; its implementations use existing Asio IO.

Unit sources live beside the implementation and the target lives in `ut`.
The independent Go peer is confined to the `it` target.

## Example and diagnostics

A complete compiled duplex example is `YamuxSession.DuplexWithShortIOAndHalfClose`
in [session_ut.cpp](session_ut.cpp): open, write a prefix, FinishWrite, accept,
read until End, then reply in the remaining direction. The test exercises the
same public API with short physical reads and writes.

`co_await session.Statistics()` returns active-stream and history-interval counts,
current buffered payload, peak receive/send payload, and cumulative monitor
wakeups and shared-wait resumptions. These exclude allocator
and metadata overhead; receive reservations are separately bounded by
`ValidateOptions(options).ReservedReceiveBytes`. Snapshots are taken on the strand.

Test commands from the project directory:

```
ya test libs/yamux/ut
ya test libs/yamux/it
```

The in-memory transport gives deterministic short IO and partial-write gates.
Integration tests start the pinned Go peer over a local socket pair in both roles.
They transfer 1 MiB, delay its consumption until a separate control-stream command,
and exercise half-close, reset, ping and admission shutdown.
