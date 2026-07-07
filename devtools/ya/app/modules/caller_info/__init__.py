import dataclasses
import logging
import os
import enum
import threading
import time
import typing

logger = logging.getLogger(__name__)


class DetectionSource(enum.StrEnum):
    PROCESS_TREE = enum.auto()
    ENV = enum.auto()


@dataclasses.dataclass
class Signature:
    # Env vars take priority over process names/paths: they are explicit and don't
    # require walking the process tree. `process` matches an ancestor's command name
    # exactly; `path` matches a (case-insensitive) substring of an ancestor's exe
    # path -- needed when the binary is launched from a versioned dir and its name
    # is a version number rather than the tool name (e.g. Claude Code:
    # .../claude/versions/<ver>/claude). Each field accepts a single value or a
    # collection; __post_init__ normalizes all of them to a set.
    env: typing.Iterable[str] = ()
    process: typing.Iterable[str] = ()
    path: typing.Iterable[str] = ()

    def __post_init__(self):
        self.env = {self.env} if isinstance(self.env, str) else set(self.env)
        self.process = {self.process} if isinstance(self.process, str) else set(self.process)
        self.path = {self.path} if isinstance(self.path, str) else set(self.path)


# Each label maps to the env vars and process names that identify it.
AGENTS: dict[str, Signature] = {
    'opencode': Signature(env='OPENCODE', process='opencode'),
    'claude': Signature(env='CLAUDECODE', process={'claude', 'claude-code'}, path='/claude/'),
    'copilot': Signature(process='copilot'),
    'codex-app': Signature(process='codex-app'),
    'codex': Signature(env='CODEX_THREAD_ID', process='codex'),
    'hermes': Signature(process='hermes'),
    'openclaw': Signature(process='openclaw'),
    'droid': Signature(process='droid'),
    'pi': Signature(process='pi'),
    'cursor': Signature(env='CURSOR_TRACE_ID'),
    'windsurf': Signature(env='CODEIUM_EDITOR_APP_ROOT'),
    'aider': Signature(env='AIDER_API_KEY'),
    'amp': Signature(env='AMP_CURRENT_THREAD_ID'),
    'antigravity': Signature(env='ANTIGRAVITY_AGENT'),
    'auggie': Signature(env='AUGMENT_AGENT'),
    'gemini': Signature(env='GEMINI_CLI'),
    'qwen': Signature(env='QWEN_CODE'),
    'replit': Signature(env='REPL_ID'),
}

# Jump ramp is the surface ya was launched through. The only one so far is
# `ya code`, which execs into the `coding-agent` tool (see handlers/code).
JUMP_RAMPS: dict[str, Signature] = {
    'ya code': Signature(env='YA_CODE', process='coding-agent'),
}

# IDE the caller is running inside. Detected mostly from env; more to come.
# VSCODE_INJECTION is set in local terminals, VSCODE_IPC_HOOK_CLI in remote
# (vscode-server) ones, so we look for either.
IDES: dict[str, Signature] = {
    'codenv': Signature(env='CODENV'),
    'vscode': Signature(env={'VSCODE_INJECTION', 'VSCODE_IPC_HOOK_CLI'}, process='code'),
}

# Why we detect the shell by walking the parent-process chain (PPID) and matching
# process names against SHELL_PROCESS_NAMES, rather than reading env vars:
#
# Shell "version" variables (ZSH_VERSION, BASH_VERSION, KSH_VERSION, FISH_VERSION,
# tcsh's `version`, ...) are SHELL-INTERNAL variables, not exported into the
# environment. They never reach a child process's environ, so a subprocess
# launched from zsh sees nothing identifying zsh -- which is exactly the gap we hit.
#
# SHELL is exported, but it's the login shell from /etc/passwd, not the shell that
# actually spawned us. If the login shell is zsh and the user is inside bash, SHELL
# still says zsh -- wrong answer.
#
# On Windows the few exported signals are unreliable too: COMSPEC is a system
# constant (points at cmd.exe even when cmd isn't running), PROMPT/CMDCMDLINE and
# PSModulePath are inherited down the process tree and/or set system-wide, so none
# of them reliably indicate the *current* shell.
#
# The only robust source is the live process tree: _walk_process_chain() resolves
# our ancestors with psutil (which under the hood reads /proc on Linux, sysctl on
# macOS and the Toolhelp32 snapshot on Windows) and matches each process name
# against SHELL_PROCESS_NAMES.
SHELL_PROCESS_NAMES = {
    'bash',
    'zsh',
    'fish',
    'sh',
    'dash',
    'ksh',
    'csh',
    'tcsh',
    'cmd',
    'cmd.exe',
    'powershell',
    'powershell.exe',
    'pwsh',
    'pwsh.exe',
}


class CallerInfo(typing.TypedDict):
    agent: str | None
    agent_source: str | None
    jump_ramp: str | None
    jump_ramp_source: str | None
    ide: str | None
    ide_source: str | None
    shell: str | None
    shell_source: str | None


UNKNOWN_CALLER_INFO: CallerInfo = {
    'agent': None,
    'agent_source': None,
    'jump_ramp': None,
    'jump_ramp_source': None,
    'ide': None,
    'ide_source': None,
    'shell': None,
    'shell_source': None,
}


def _normalize_process_name(name: str | None) -> str:
    return os.path.basename(name or '').lower()


def _process_exe(proc) -> str:
    import psutil

    try:
        # Full path of the executable; may be empty/denied for some processes.
        return (proc.exe() or '').lower()
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return ''


def _walk_process_chain() -> list[tuple[str, str]]:
    # Returns (normalized_name, exe_path_lower) per ancestor process. name() is the
    # bare command name (no args/paths); exe() is the full binary path for path-based
    # matching. exe_path is '' when psutil can't resolve it.
    try:
        import psutil

        proc = psutil.Process()
        chain = []
        while proc is not None:
            try:
                chain.append((_normalize_process_name(proc.name()), _process_exe(proc)))
                proc = proc.parent()
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                break
        return chain
    except Exception:
        logger.debug("Failed to get process chain", exc_info=True)
        return []


def _source_value(source: DetectionSource | None) -> str | None:
    return source.value if source is not None else None


class CallerInfoProvider:
    """Figures out what launched ya: agent, jump ramp, ide and terminal.

    run() is meant to be driven from a background thread (started by the caller)
    so detection never blocks startup; until it finishes, get_nowait() returns
    UNKNOWN_CALLER_INFO. The process tree is walked lazily and at most once --
    only if env-based detection came up short.
    """

    def __init__(self):
        self._ready = threading.Event()
        self._data: CallerInfo = UNKNOWN_CALLER_INFO.copy()
        self._process_chain: list[str] | None = None

    def run(self):
        started = time.monotonic()
        try:
            self._data = self._collect()
        except Exception:
            logger.debug("Failed to get caller info", exc_info=True)
        finally:
            self._ready.set()
            logger.debug("caller_info detection took %.1f ms", (time.monotonic() - started) * 1000.0)

    def get_nowait(self) -> CallerInfo:
        return self._data

    def get(self, timeout: float | None = None) -> CallerInfo:
        # Block until detection finishes (or timeout elapses); on timeout the
        # partially-filled/unknown data is returned rather than raising.
        self._ready.wait(timeout)
        return self._data

    def ready(self) -> bool:
        return self._ready.is_set()

    def _get_process_chain(self) -> list[tuple[str, str]]:
        if self._process_chain is None:
            self._process_chain = _walk_process_chain()
        return self._process_chain

    def _detect(self, category: str, signatures: dict[str, Signature]) -> tuple[str | None, DetectionSource | None]:
        # Env vars first: explicit and don't require walking the process tree.
        for label, sig in signatures.items():
            for var in sig.env:
                if var in os.environ:
                    logger.debug("caller_info: %s=%s matched via env $%s", category, label, var)
                    return label, DetectionSource.ENV

        # Fall back to the process tree (walked lazily, only if env missed). Match an
        # ancestor's command name exactly, or its exe path by substring.
        for name, exe in self._get_process_chain():
            for label, sig in signatures.items():
                if name in sig.process:
                    logger.debug("caller_info: %s=%s matched via process name %r", category, label, name)
                    return label, DetectionSource.PROCESS_TREE
                path_marker = next((marker for marker in sig.path if marker in exe), None)
                if path_marker is not None:
                    logger.debug(
                        "caller_info: %s=%s matched via exe path %r (marker %r)", category, label, exe, path_marker
                    )
                    return label, DetectionSource.PROCESS_TREE

        logger.debug("caller_info: %s not detected", category)
        return None, None

    def _detect_shell(self) -> tuple[str | None, DetectionSource | None]:
        for name, _ in self._get_process_chain():
            if name in SHELL_PROCESS_NAMES:
                logger.debug("caller_info: shell=%s matched via process name", name)
                return name, DetectionSource.PROCESS_TREE
        logger.debug("caller_info: shell not detected")
        return None, None

    def _collect(self) -> CallerInfo:
        agent, agent_source = self._detect('agent', AGENTS)
        jump_ramp, jump_ramp_source = self._detect('jump_ramp', JUMP_RAMPS)
        ide, ide_source = self._detect('ide', IDES)
        shell, shell_source = self._detect_shell()

        return {
            'agent': agent,
            'agent_source': _source_value(agent_source),
            'jump_ramp': jump_ramp,
            'jump_ramp_source': _source_value(jump_ramp_source),
            'ide': ide,
            'ide_source': _source_value(ide_source),
            'shell': shell,
            'shell_source': _source_value(shell_source),
        }


def has_data(data: CallerInfo | None) -> bool:
    # True when detection actually found something (not the all-None UNKNOWN dict).
    return bool(data) and any(value is not None for value in data.values())


def get_caller_info_from_context(ctx, timeout: float | None = None) -> CallerInfo | None:
    # timeout=None -> non-blocking (returns whatever the background detection has
    # so far); a positive timeout blocks until detection finishes or elapses.
    # Returns None when nothing was detected, so callers can gate on truthiness.
    provider = getattr(ctx, 'caller_info', None)
    if provider is None:
        return None
    if isinstance(provider, CallerInfoProvider):
        data = provider.get(timeout) if timeout is not None else provider.get_nowait()
    else:
        data = provider
    return data if has_data(data) else None
