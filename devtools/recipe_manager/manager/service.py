import dataclasses
import grpc
import inspect
import logging
import os
import threading
import time
import typing as tp
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from google.protobuf.json_format import MessageToJson
from grpc_reflection.v1alpha import reflection
from library.python.fs import remove_tree_safe

from devtools.recipe_manager.client import get_socket_path, get_shallow_root_data_path, Lifetime
from devtools.recipe_manager.proto import manager_pb2, manager_pb2_grpc

from . import sentinel
from .error import (
    AlreadyExistsError,
    InvalidArgumentError,
    NotFoundError,
    PreconditionError,
    ProcessTimeoutError,
    RecipeError,
)

logger = logging.getLogger(__name__)


@dataclasses.dataclass()
class Recipe:
    sentinel: sentinel.Sentinel
    users: set[tuple[str, str]]  # set of (invocation_id, build_id)


class RecipeManagerServicer(manager_pb2_grpc.RecipeManager):
    def __init__(self, shallow_root: str, stop_event: threading.Event):
        super().__init__()
        self._shallow_root = shallow_root
        self._stop_event = stop_event
        self._keep_shallow_root = False
        self._lost_ownership = False

        self._recipes: dict[str, Recipe] = {}
        self._recipes_lock = threading.Lock()

        self._heartbeat_times: dict[str, float] = {}  # invocation_id → last heartbeat time
        self._heartbeat_lock = threading.Lock()

        # Inode of our socket file — set by start_watchdog() after server.start()
        # creates the socket.  None means watchdog has not been started yet.
        self._socket_inode: int | None = None

        self._heartbeat_monitor_thread = threading.Thread(
            target=self._heartbeat_monitor_loop, daemon=True, name='rm-heartbeat-monitor'
        )
        self._heartbeat_monitor_thread.start()

    def start_watchdog(self) -> None:
        """Record the socket inode and start the filesystem watchdog thread.

        Must be called after grpc server.start() so the socket file already exists.
        """
        socket_path = get_socket_path(self._shallow_root)
        self._socket_inode = self._read_socket_inode(socket_path)
        logger.debug("Watchdog: recorded socket inode=%s path=%s", self._socket_inode, socket_path)
        self._watchdog_thread = threading.Thread(
            target=self._filesystem_watchdog_loop, daemon=True, name='rm-fs-watchdog'
        )
        self._watchdog_thread.start()

    def Ping(self, request: manager_pb2.Empty, context: grpc.ServicerContext) -> manager_pb2.Empty:
        return manager_pb2.Empty()

    def Shutdown(self, request: manager_pb2.ShutdownRequest, context: grpc.ServicerContext) -> manager_pb2.Empty:
        return self._process_request(self._do_shutdown, request, context)

    def FinishBuild(self, request: manager_pb2.FinishBuildRequest, context: grpc.ServicerContext) -> manager_pb2.Empty:
        return self._process_request(self._do_finish_build, request, context)

    def HeartBeat(self, request: manager_pb2.HeartBeatRequest, context: grpc.ServicerContext) -> manager_pb2.Empty:
        with self._heartbeat_lock:
            self._heartbeat_times[request.invocation_id] = time.time()
        return manager_pb2.Empty()

    def StartRecipe(
        self, request: manager_pb2.StartRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.StartRecipeResult:
        return self._process_request(self._do_start_recipe, request, context)

    def StopRecipe(
        self, request: manager_pb2.StopRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.StopRecipeResult:
        return self._process_request(self._do_stop_recipe, request, context)

    def ListRecipes(self, request: manager_pb2.Empty, context: grpc.ServicerContext) -> manager_pb2.ListRecipesResult:
        return self._process_request(self._do_list_recipe, request, context)

    def GetRecipe(
        self, request: manager_pb2.GetRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.GetRecipeResult:
        return self._process_request(self._do_get_recipe, request, context)

    def AssociateRecipe(
        self, request: manager_pb2.AssociateRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.Empty:
        return self._process_request(self._do_associate_recipe, request, context)

    def _heartbeat_monitor_loop(self) -> None:
        """Monitor heartbeats. If an invocation_id stops sending heartbeats for
        HEARTBEAT_TIMEOUT seconds, disassociate it from all recipes it was using.
        Recipes keep running — persistent recipes survive ya-bin termination."""
        HEARTBEAT_TIMEOUT = 1.0
        CHECK_INTERVAL = 1.0

        while not self._stop_event.wait(timeout=CHECK_INTERVAL):
            now = time.time()

            # Step 1: Find and pop stale invocation_ids under heartbeat_lock
            with self._heartbeat_lock:
                stale = [
                    invocation_id for invocation_id, ts in self._heartbeat_times.items() if now - ts > HEARTBEAT_TIMEOUT
                ]
                # Pop stale invocation_ids under heartbeat_lock
                for invocation_id in stale:
                    self._heartbeat_times.pop(invocation_id, None)

            # Step 2: Remove stale users under _recipes_lock (AFTER releasing heartbeat_lock)
            # This prevents deadlock if RPC handler holds _heartbeat_lock and tries _recipes_lock
            with self._recipes_lock:
                for invocation_id in stale:
                    logger.debug(
                        "Heartbeat timeout for invocation_id=%s, disassociating all its build users",
                        invocation_id,
                    )
                    for r in self._recipes.values():
                        # Modify users set in-place
                        stale_users = {u for u in r.users if u[0] == invocation_id}
                        if stale_users:
                            r.users -= stale_users
                            logger.debug(
                                "Disassociated users=%s from recipe %s (heartbeat timeout), remaining users: %s",
                                stale_users,
                                r.sentinel.recipe_uid,
                                r.users,
                            )

    def _filesystem_watchdog_loop(self) -> None:
        """Shut down if our socket file disappears or is replaced by another RM.

        The socket lives in meta/ which is never deleted by RM itself.
        It can only disappear if the user removes ~/.ya (the whole shallow_root).

        Detects two cases:
        - Socket deleted (e.g. user removed ~/.ya): FileNotFoundError on stat().
        - Socket replaced (new RM started after ~/.ya was removed and recreated):
          inode of the socket file differs from the one we recorded at startup.

        In both cases we set _lost_ownership=True so cleanup() skips _remove_data_dir().
        """
        CHECK_INTERVAL = 1.0
        socket_path = get_socket_path(self._shallow_root)
        while not self._stop_event.wait(timeout=CHECK_INTERVAL):
            current_inode = self._read_socket_inode(socket_path)
            if current_inode is None:
                logger.warning("Socket file removed (path=%s) — shutting down", socket_path)
                self._lost_ownership = True
                self._stop_event.set()
            elif current_inode != self._socket_inode:
                logger.warning(
                    "Socket inode changed (expected=%s, got=%s) — another RM is running, shutting down",
                    self._socket_inode,
                    current_inode,
                )
                self._lost_ownership = True
                self._stop_event.set()

    @staticmethod
    def _read_socket_inode(socket_path: str) -> int | None:
        """Return the inode of the socket file, or None if it does not exist."""
        try:
            return os.stat(socket_path).st_ino
        except FileNotFoundError:
            return None

    def cleanup(self) -> None:
        logger.debug("Start cleanup")

        # Abort all sentinels
        with self._recipes_lock:
            for r in self._recipes.values():
                logger.debug("Abort recipe %s", r.sentinel.recipe_uid)
                r.sentinel.abort()

        if not self._keep_shallow_root and not self._lost_ownership:
            self._remove_data_dir()

    # GRPC interceptors have no access to the request, so we need to implement this wrapper to log the request
    def _process_request(self, func: Callable, request: tp.Any, context: grpc.ServicerContext) -> tp.Any:
        method_name = inspect.stack()[1].function
        logger.debug("GRPC %s(%s)", method_name, MessageToJson(request, indent=None))
        try:
            result = func(request, context)
            logger.debug(
                "GRPC %s(%s) RESULT (%s)",
                method_name,
                MessageToJson(request, indent=None),
                MessageToJson(result, indent=None),
            )
            return result
        except BaseException as e:
            if isinstance(e, RecipeError):
                status = self._status_code_by_error(e)
            else:
                status = grpc.StatusCode.INTERNAL
                logger.exception("Internal error occurred")
            logger.debug(
                "GRPC %s(%s) ERROR (status='%s', '%s')",
                method_name,
                MessageToJson(request, indent=None),
                status.value[1],
                str(e),
            )
            context.abort(status, str(e))

    def _do_finish_build(
        self, request: manager_pb2.FinishBuildRequest, context: grpc.ServicerContext
    ) -> manager_pb2.Empty:
        """Disassociate all users with (invocation_id, build_id) from all recipes.

        Called by ya-bin after a build finishes (successfully or not).
        Allows the next build of the same ya-bin to restart or reuse recipes.
        """
        user = (request.invocation_id, request.build_id)

        with self._recipes_lock:
            for r in self._recipes.values():
                # Modify users set in-place
                if user in r.users:
                    r.users.discard(user)
                    logger.debug(
                        "FinishBuild: disassociated user=%s from recipe %s, remaining users: %s",
                        user,
                        r.sentinel.recipe_uid,
                        r.users,
                    )
        return manager_pb2.Empty()

    def _do_shutdown(self, request: manager_pb2.ShutdownRequest, context: grpc.ServicerContext) -> manager_pb2.Empty:
        with self._recipes_lock:
            users = set()
            for r in self._recipes.values():
                users.update(r.users)
            if not request.force and users:
                raise PreconditionError(
                    "Cannot shutdown: someone uses running recipe(s). "
                    "Use force=True to abort them anyway. Users {}".format(users)
                )
        self._keep_shallow_root = request.keep_shallow_root
        self._stop_event.set()
        return manager_pb2.Empty()

    def _do_start_recipe(
        self, request: manager_pb2.StartRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.StartRecipeResult:
        with self._recipes_lock:
            if request.recipe_uid in self._recipes:
                raise AlreadyExistsError(f"Recipe {request.recipe_uid} already exists")

        working_dir = self._check_and_fix_path(context, request, "working_dir")
        err_filename = self._check_and_fix_path(context, request, "err_filename")
        out_filename = self._check_and_fix_path(context, request, "out_filename")

        try:
            lifetime = Lifetime(request.lifetime)
        except ValueError as e:
            raise InvalidArgumentError(str(e))
        if lifetime != Lifetime.PERSISTENT:
            raise InvalidArgumentError(f"Unsupported lifetime {lifetime}")

        s = sentinel.Sentinel(
            recipe_uid=request.recipe_uid,
            lifetime=lifetime,
            working_dir=working_dir,
        )
        s.start(
            list(request.command), dict(request.env), err_filename, out_filename, request.timeout, request.retry_count
        )

        # XXX: Check if invocation_id exists in heartbeat_times. We should never
        # add non-existent users. Lock prevents race when ya-bin times out
        # immediately after sending StartRecipeRequest request.
        with self._heartbeat_lock:
            has_invocation = request.invocation_id in self._heartbeat_times
            # Check invocation and add Recipe
            with self._recipes_lock:
                if has_invocation:
                    users = {(request.invocation_id, request.build_id)}
                else:
                    users = set()

                self._recipes[request.recipe_uid] = Recipe(sentinel=s, users=users)

        logger.debug(
            "Recipe %s started, invocation_id=%s, build_id=%s, users=%s",
            request.recipe_uid,
            request.invocation_id,
            request.build_id,
            users,
        )
        return manager_pb2.StartRecipeResult()

    def _do_stop_recipe(
        self, request: manager_pb2.StopRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.StopRecipeResult:
        with self._recipes_lock:
            r = self._recipes.get(request.recipe_uid)
            if r is None:
                raise NotFoundError(f"No recipe found for recipe_uid={request.recipe_uid}")
            if r.users:
                raise PreconditionError(
                    f"Recipe {request.recipe_uid} still has users: {r.users}. "
                    "Stop is only allowed when no users are associated."
                )

        err_filename = self._check_and_fix_path(context, request, "err_filename")
        out_filename = self._check_and_fix_path(context, request, "out_filename")

        s = r.sentinel
        try:
            s.stop(list(request.command), dict(request.env), err_filename, out_filename, request.timeout)
        finally:
            # XXX: sentinel is aborted even if stop fails so we remove regardless
            with self._recipes_lock:
                self._recipes.pop(request.recipe_uid)

        return manager_pb2.StopRecipeResult()

    def _do_list_recipe(
        self, request: manager_pb2.Empty, context: grpc.ServicerContext
    ) -> manager_pb2.ListRecipesResult:
        items = []

        with self._recipes_lock:
            for r in self._recipes.values():
                s = r.sentinel
                items.append(
                    manager_pb2.RecipeInfo(
                        recipe_uid=s.recipe_uid,
                        lifetime=s.lifetime.value,
                        working_dir=s.working_dir,
                        users=[manager_pb2.UserRef(invocation_id=u[0], build_id=u[1]) for u in r.users],
                        started_at=s.started_at,
                    )
                )
        return manager_pb2.ListRecipesResult(items=items)

    def _do_get_recipe(
        self, request: manager_pb2.GetRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.GetRecipeResult:
        with self._recipes_lock:
            if request.recipe_uid not in self._recipes:
                raise NotFoundError(f"No recipe found for recipe_uid={request.recipe_uid}")

            r = self._recipes[request.recipe_uid]
            s = r.sentinel

            recipe_info = manager_pb2.RecipeInfo(
                recipe_uid=s.recipe_uid,
                lifetime=s.lifetime.value,
                working_dir=s.working_dir,
                users=[manager_pb2.UserRef(invocation_id=u[0], build_id=u[1]) for u in r.users],
                started_at=s.started_at,
            )

        return manager_pb2.GetRecipeResult(recipe=recipe_info)

    def _do_associate_recipe(
        self, request: manager_pb2.AssociateRecipeRequest, context: grpc.ServicerContext
    ) -> manager_pb2.Empty:
        # XXX: Check if invocation_id exists in heartbeat_times. We should never
        # add non-existent users. Lock prevents race when ya-bin times out
        # immediately after sending AssociateRecipeRequest request.
        with self._heartbeat_lock:
            has_invocation = request.invocation_id in self._heartbeat_times
            with self._recipes_lock:
                if request.recipe_uid not in self._recipes:
                    raise NotFoundError(
                        f"Recipe {request.recipe_uid} not found in sentinels — "
                        "AssociateRecipe must only be called for running recipes"
                    )

                r = self._recipes[request.recipe_uid]

                # Modify users set in-place
                if has_invocation:
                    r.users.add((request.invocation_id, request.build_id))

        logger.debug(
            "Associated (invocation_id=%s, build_id=%s) with recipe %s",
            request.invocation_id,
            request.build_id,
            request.recipe_uid,
        )

        return manager_pb2.Empty()

    def _remove_data_dir(self) -> None:
        """Remove data/ — holds recipe working dirs. meta/ is never touched."""
        data_dir = get_shallow_root_data_path(self._shallow_root)
        if os.path.exists(data_dir):
            logger.debug("Remove data dir: %s", data_dir)
            remove_tree_safe(data_dir)

    def _check_and_fix_path(self, context, request, field_name):
        path = getattr(request, field_name)
        if not path:
            context.abort(grpc.StatusCode.INVALID_ARGUMENT, f"Empty path {field_name}")
        data_dir = get_shallow_root_data_path(self._shallow_root)
        if not os.path.isabs(path):
            # Relative path is convenient for manual testing
            return os.path.join(data_dir, path)
        elif os.path.commonpath([path, data_dir]) != data_dir:
            context.abort(
                grpc.StatusCode.INVALID_ARGUMENT,
                f"{field_name} is out of data dir: {field_name}={path}, data_dir={data_dir}",
            )
        return path

    @staticmethod
    def _status_code_by_error(e):
        if isinstance(e, InvalidArgumentError):
            return grpc.StatusCode.INVALID_ARGUMENT
        if isinstance(e, AlreadyExistsError):
            return grpc.StatusCode.ALREADY_EXISTS
        if isinstance(e, NotFoundError):
            return grpc.StatusCode.NOT_FOUND
        if isinstance(e, PreconditionError):
            return grpc.StatusCode.FAILED_PRECONDITION
        if isinstance(e, ProcessTimeoutError):
            return grpc.StatusCode.DEADLINE_EXCEEDED
        return grpc.StatusCode.UNKNOWN


def serve(shallow_root, stop_event):
    rm_servicer = RecipeManagerServicer(shallow_root, stop_event)
    socket_path = get_socket_path(shallow_root)
    server = grpc.server(ThreadPoolExecutor(max_workers=10))
    manager_pb2_grpc.add_RecipeManagerServicer_to_server(rm_servicer, server)
    # Enable reflection (required for grpcurl)
    service_names = (
        manager_pb2.DESCRIPTOR.services_by_name['RecipeManager'].full_name,
        reflection.SERVICE_NAME,
    )
    reflection.enable_server_reflection(service_names, server)

    server.add_insecure_port(f"unix://{socket_path}")
    server.start()
    logger.debug("Service has started on socket=%s", socket_path)

    # Start watchdog after server.start() — socket file now exists on disk.
    rm_servicer.start_watchdog()

    stop_event.wait()

    logger.debug("Service shutdown is requested")
    server.stop(grace=0.5)
    rm_servicer.cleanup()
