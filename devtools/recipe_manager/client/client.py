import contextlib
import enum
import grpc
import hashlib
import logging
import os
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field

from library.python.filelock import FileLock
from library.python import fs
from devtools.recipe_manager.proto import manager_pb2, manager_pb2_grpc
from yalibrary.loggers import file_log

__all__ = [
    "Lifetime",
    "RecipeManagerClient",
    "RecipeInfo",
    "UserRef",
    "get_socket_path",
    "get_recipes_shallow_root_path",
    "get_shallow_root_meta_path",
    "get_shallow_root_data_path",
]

# Filenames relative to meta dir
_VERSION_FILE = "manager.version"
_VERSION_LOCK = "manager.version.lock"

logger = logging.getLogger(__name__)


def _get_binary_version() -> str:
    """Return a string identifying the current binary version.

    Uses svn_version.hash() (Arc commit hash baked into the binary at build time).
    Falls back to commit_id, then svn_revision, then 'unknown'.
    'unknown' is a safe fallback: two 'unknown' values compare equal → no restart.
    """
    default = "unknown"
    with contextlib.suppress(ImportError):
        from library.python import svn_version

        return svn_version.hash() or svn_version.commit_id() or default
    return default


class Lifetime(enum.StrEnum):
    TRANSIENT = enum.auto()
    PERSISTENT = enum.auto()


@dataclass
class UserRef:
    invocation_id: str
    build_id: str


@dataclass
class RecipeInfo:
    recipe_uid: str
    lifetime: Lifetime
    working_dir: str
    users: list = field(default_factory=list)
    started_at: float = 0.0

    def _format_users(self) -> str:
        if not self.users:
            return '(none)'
        return ','.join('{}:{}'.format(u.invocation_id, u.build_id) for u in self.users)

    def __str__(self) -> str:
        return f"""\
recipe_uid: {self.recipe_uid}
lifetime: {self.lifetime}
working_dir: {self.working_dir}
users: {self._format_users()}
started_at: {self.started_at:.3f}
"""


class RecipeManagerClient:
    def __init__(self, shallow_root: str):
        self._shallow_root = shallow_root
        self._log_file = None
        self._socket_path = get_socket_path(self._shallow_root)
        self._channel = None
        self._stub = None
        self._heartbeat_stop = None

    def start_manager(
        self,
        invocation_id: str,
        logs_root: str | None = None,
        log_file: str | None = None,
        timeout: float = 5,
        force_restart: bool = False,
        # TODO: Delete after August 31st
        source_root: str = '',
    ) -> None:
        assert bool(logs_root) ^ bool(log_file), "One of 'logs_root' or 'log_file' should be specified"
        self._log_file = log_file or self._get_log_file_path(logs_root)
        # meta/ must exist before we take the lock — it must never be deleted.
        os.makedirs(get_shallow_root_meta_path(self._shallow_root), exist_ok=True)
        os.makedirs(get_shallow_root_data_path(self._shallow_root), exist_ok=True)
        lock_path = os.path.join(get_shallow_root_meta_path(self._shallow_root), _VERSION_LOCK)
        with FileLock(lock_path):
            self._start_manager_locked(invocation_id, timeout, force_restart, source_root)
        # Heartbeat runs outside the lock — it's a long-lived thread
        self._start_heartbeat(invocation_id)

    def _start_manager_locked(self, invocation_id: str, timeout: float, force_restart: bool, source_root: str) -> None:
        # FIXME: We changed the location of shallow_root, this is temporary code
        # to shut down the manager that lives in the old location.
        # TODO: Delete after August 31st
        self._shutdown_old_manager(source_root)
        current_version = _get_binary_version()
        if self.is_alive():
            saved_version = self._read_version_file()
            if force_restart or saved_version != current_version:
                logger.debug(
                    "Restarting RecipeManager: force_restart=%s, saved_version=%r, current_version=%r",
                    force_restart,
                    saved_version,
                    current_version,
                )
                # XXX: When version changes or force_restart is set kill all
                # recipes forcefully even when someone uses them.
                # Normally no one should use them at this point because
                # switching branches during build is not recommended anyway.
                self.shutdown(keep_shallow_root=False, force=True)
                # Wait for the old RM process to release manager.lock before
                # launching a new one.  shutdown() is asynchronous (it sets a
                # stop_event inside the RM process), so the old process may
                # still be running cleanup() when we reach _launch_manager().
                # Without this wait the new RM would fail to acquire
                # manager.lock and exit with an error.
                #
                # There is no race here: _start_manager_locked is always called
                # under version.lock, so only one client can execute this
                # sequence at a time.
                meta_dir = get_shallow_root_meta_path(self._shallow_root)
                self._wait_for_manager_lock_release(meta_dir, timeout)
                # meta/ survives shutdown — only data/ was removed.
                # Recreate data/ so the new RM process can use it.
                os.makedirs(get_shallow_root_data_path(self._shallow_root), exist_ok=True)
                self._launch_manager(timeout)
            else:
                logger.debug("RecipeManager is alive and version matches (%s), reusing", current_version)
                return
        else:
            self._launch_manager(timeout)
        self._write_version_file(current_version)

    def _shutdown_old_manager(self, source_root: str):
        # If we got to this function then there is a new version of the manager
        # and the old manager must be shutdown.
        import hashlib

        # new: ~/.ya/build/shallow_root/recipes/<md5>
        # old: ~/.ya/build/shallow_root/<sha256>
        base = os.path.dirname(os.path.dirname(self._shallow_root))
        digest = hashlib.sha256(source_root.encode()).hexdigest()[:32]
        shallow_root = os.path.join(base, digest)
        # Manager is capable of gracefully handling shallow root removal
        fs.remove_tree_safe(shallow_root)

    def _wait_for_manager_lock_release(self, meta_dir: str, timeout: float) -> None:
        """Poll manager.lock until the old RM process releases it.

        The old RM holds manager.lock for the entire duration of main() —
        including cleanup() after server.stop().  We must not launch a new RM
        until the lock is free, otherwise the new RM will fail to acquire it
        and exit with an error.

        Raises RuntimeError if the lock is not released within `timeout` seconds.
        """
        lock_path = os.path.join(meta_dir, "manager.lock")
        deadline = time.time() + timeout
        while time.time() < deadline:
            lk = FileLock(lock_path)
            if lk.acquire(blocking=False):
                lk.release()
                logger.debug("manager.lock released, proceeding to launch new RecipeManager")
                return
            time.sleep(0.05)
        raise RuntimeError(
            "Timed out waiting for old RecipeManager to stop "
            "(manager.lock not released within {:.1f}s)".format(timeout)
        )

    def _launch_manager(self, timeout: float) -> None:
        """Start the RM daemon process and wait until it is alive."""
        deadline = time.time() + timeout
        env = dict(os.environ)
        env["Y_PYTHON_ENTRY_POINT"] = "devtools.recipe_manager.manager.main:main"
        cmd = [sys.executable, "--log-file", self._log_file, "--shallow-root", self._shallow_root, "--daemonize"]
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
        if proc.returncode:
            raise RuntimeError(
                "RecipeManager returns exit_code={}. out:{}\nerr:{}\n".format(
                    proc.returncode,
                    proc.stdout.rstrip(),
                    proc.stderr.rstrip(),
                )
            )
        while not self.is_alive():
            if time.time() > deadline:
                raise RuntimeError("Cannot connect to RecipeManager")
            time.sleep(0.05)
        logger.debug("RecipeManager started on %s", self._socket_path)

    def _read_version_file(self) -> str:
        """Read saved binary version. Returns '' if file is absent or unreadable."""
        try:
            with open(os.path.join(get_shallow_root_meta_path(self._shallow_root), _VERSION_FILE)) as f:
                return f.read().strip()
        except OSError:
            return ""

    def _write_version_file(self, version: str) -> None:
        try:
            with open(os.path.join(get_shallow_root_meta_path(self._shallow_root), _VERSION_FILE), 'w') as f:
                f.write(version)
        except OSError:
            # meta/ may have been removed (e.g. user deleted ~/.ya) between shutdown
            # and write — log and continue, this is a non-fatal edge case.
            logger.warning("Could not write version file — meta/ may have been removed", exc_info=True)

    def is_alive(self):
        try:
            self._client.Ping(manager_pb2.Empty())
            return True
        except Exception:
            return False

    def close(self) -> None:
        if self._heartbeat_stop is not None:
            self._heartbeat_stop.set()
        if self._stub:
            self._stub = None
            self._channel.close()
            self._channel = None

    def _start_heartbeat(self, invocation_id: str) -> None:
        self._heartbeat_stop = threading.Event()

        def _send():
            try:
                self._client.HeartBeat(manager_pb2.HeartBeatRequest(invocation_id=invocation_id))
            except Exception:
                pass

        def _loop():
            _send()  # первый heartbeat сразу
            while not self._heartbeat_stop.wait(timeout=0.5):
                _send()

        t = threading.Thread(target=_loop, daemon=True, name='rm-heartbeat')
        t.start()

    def shutdown(self, keep_shallow_root: bool = False, force: bool = False) -> None:
        """`force` means shutdown even if there are running recipes and someone uses them."""
        request = manager_pb2.ShutdownRequest(
            keep_shallow_root=keep_shallow_root,
            force=force,
        )
        self._client.Shutdown(request)
        self.close()

    def finish_build(self, invocation_id: str, build_id: str) -> None:
        """Notify RM that a build has finished.

        Disassociates all users with the given (invocation_id, build_id) from
        all recipes, allowing the next build of the same ya-bin to restart
        or reuse recipes as needed.
        """
        request = manager_pb2.FinishBuildRequest(
            invocation_id=invocation_id,
            build_id=build_id,
        )
        self._client.FinishBuild(request)

    def start_recipe(
        self,
        invocation_id: str,
        build_id: str,
        recipe_uid: str,
        lifetime: Lifetime,
        command: list[str],
        err_filename: str,
        out_filename: str,
        working_dir: str,
        env: dict[str, str] | None = None,
        retry_count: int = 0,
        timeout: float = 0,
    ) -> None:
        request = manager_pb2.StartRecipeRequest(
            invocation_id=invocation_id,
            build_id=build_id,
            recipe_uid=recipe_uid,
            lifetime=lifetime.value,
            command=command,
            env=env,
            err_filename=err_filename,
            out_filename=out_filename,
            working_dir=working_dir,
            retry_count=retry_count,
            timeout=timeout,
        )
        self._client.StartRecipe(request)

    def stop_recipe(
        self,
        recipe_uid: str,
        command: list[str],
        err_filename: str,
        out_filename: str,
        env: dict[str, str] | None = None,
        timeout: float = 0,
    ) -> None:
        request = manager_pb2.StopRecipeRequest(
            recipe_uid=recipe_uid,
            command=command,
            env=env,
            err_filename=err_filename,
            out_filename=out_filename,
            timeout=timeout,
        )
        self._client.StopRecipe(request)

    def get_recipe(self, recipe_uid: str) -> RecipeInfo:
        """Return RecipeInfo for the given recipe_uid.

        Raises grpc.RpcError with NOT_FOUND status if recipe doesn't exist.
        """
        request = manager_pb2.GetRecipeRequest(recipe_uid=recipe_uid)
        result = self._client.GetRecipe(request)
        return self._recipe_info_from_proto(result.recipe)

    def associate_recipe(self, invocation_id: str, build_id: str, recipe_uid: str) -> None:
        """Associate (invocation_id, build_id) with an already-running recipe."""
        request = manager_pb2.AssociateRecipeRequest(
            invocation_id=invocation_id,
            build_id=build_id,
            recipe_uid=recipe_uid,
        )
        self._client.AssociateRecipe(request)

    def list_recipes(self) -> list[RecipeInfo]:
        recipes = []
        for r in self._client.ListRecipes(manager_pb2.Empty()).items:
            recipes.append(self._recipe_info_from_proto(r))
        return recipes

    def log_file(self) -> str:
        return self._log_file

    @property
    def _client(self) -> manager_pb2_grpc.RecipeManagerStub:
        if not self._stub:
            channel = grpc.insecure_channel(f"unix://{self._socket_path}")
            try:
                stub = manager_pb2_grpc.RecipeManagerStub(channel)
                stub.Ping(manager_pb2.Empty())
                self._channel = channel
                self._stub = stub
            except Exception:
                channel.close()
                raise
        return self._stub

    @staticmethod
    def _recipe_info_from_proto(r) -> 'RecipeInfo':
        return RecipeInfo(
            recipe_uid=r.recipe_uid,
            lifetime=Lifetime(r.lifetime),
            working_dir=r.working_dir,
            users=[UserRef(invocation_id=u.invocation_id, build_id=u.build_id) for u in r.users],
            started_at=r.started_at,
        )

    @staticmethod
    def _get_log_file_path(logs_root: str) -> str:
        assert os.path.exists(logs_root)
        now = file_log.log_creation_time("")
        chunks = file_log.LogChunks(logs_root)
        log_chunk = chunks.get_or_create(file_log.format_date(now))
        file_name = "{}.recipe_manager.{}.log".format(file_log.format_time(now), os.getpid())
        return os.path.join(log_chunk, file_name)


def get_shallow_root_meta_path(shallow_root: str) -> str:
    """Return path to the meta subdirectory — never deleted."""
    return os.path.join(shallow_root, "meta")


def get_shallow_root_data_path(shallow_root: str) -> str:
    """Return path to the data subdirectory — deleted on Shutdown, cleaned on RM start."""
    return os.path.join(shallow_root, "data")


def get_socket_path(shallow_root: str) -> str:
    """Socket lives in meta/ so it is not affected by data/ cleanup."""
    return os.path.join(get_shallow_root_meta_path(shallow_root), "manager.sock")


def get_recipes_shallow_root_path(shallow_root: str, arcadia_path: str) -> str:
    # Use first 8 hex chars (32 bits) to keep the socket path under the
    # 103-char UNIX_PATH_MAX limit on macOS / Linux.
    digest = hashlib.md5(arcadia_path.encode()).hexdigest()[:8]
    return os.path.join(shallow_root, "recipes", digest)
