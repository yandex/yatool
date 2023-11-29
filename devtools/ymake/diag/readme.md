# Diagnostic messages in ymake

We have three ways to output diagnostic messages in regular ymake executions:

## 1. `YDIAG(var)` macros defined in diag/dbg.h

These debug messages are not intended for users.
* It simply prints to stderr with "var:" prefix.
* Works only in debug build. If you need both performance and these messages
  use relwithdebinfo build.
* Even in debug build most of them are disabled by default. Use `--warn var`
  option to enable.
  TODO: I do not know how to enable them via ya-bin.
* They are not persisted anywhere.
* ya-bin usually hides them. Use `ya -v` or run ymake directly, e.g.
  ```
  ya dump conf > /tmp/ymake.conf
  ymake \
    --config /tmp/ymake.conf \
    --build-root /tmp/build \
    --plugins-root $ARCADIA/build/plugins \
    --dump-build-plan /tmp/graph.json
  ```
* Possible `var` values are defined in diag/diag.h

## 2. `Y{Err,Warn,Info}` macroses defined in diag/display.h

Use these for user intended messages that are not related to graph nodes
and should not be persisted in graph cache.

ya-bin shows this messages and colors them according to their severity.
Use them only if you want user to do something.
If everything goes fine ymake should be silent.

Note: We also have `YDebug()`, but it probably should not be used.
Prefer `YDIAG` for such purposes.

## 3. `YConf{Err,Warn,Info}{,Precise}(var)` macroses defined in diag/manager.h

Use these for user intended messages that are related to graph nodes
and should be persisted in graph cache. Otherwise these messages might be lost
when using graph from cache.

ya-bin shows (most of) them and colors according to their severity, so the same
rules as in 2. apply: use them only if you expect user to do something. Also
some of these messages are used for special purposes, like selective checkout,
depending on their `var`.

These messages require TScopedContext to be defined somewhere on callstack which
binds them to graph node. Otherwise they are produced as TOP_LEVEL messages
that are output, but not persisted and probably will not be handled correctly
by ya-bin and ci.

## Special ymake execution modes

When run in some special mode, e.g. uid debugger, graph dump or relation dump,
use `Cerr` and `Cout` directly. You might want to use same `--warn var` flags
for filtering your messages. It can be done with macro like this
```
#define UidDebuggerLog (Y_UNLIKELY(Diag()->UIDs)) && TEatStream() | Cerr << "UIDs: "
```
