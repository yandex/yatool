"""
Логика запуска/переиспользования persistent рецептов через Recipe Manager.

Каждый чанк независимо опрашивает RM для каждого рецепта.
Все рецепты независимы и не видят env друг друга.
Единственный канал коммуникации рецепт → тест: env.json.txt в shallow root рецепта.

Защита от гонок: неблокирующий FileLock на shallow dir рецепта.
Под локом вызывается GetRecipe и принимается решение о запуске/ассоциации.
"""

import base64
import functools
import io
import json
import logging
import os
import shlex
import shutil
import tempfile
import time
import traceback

from library.python.filelock import FileLock

from exts.hashing import md5_path, md5_value
from devtools.ya.test import const
from devtools.ya.test.programs.test_tool.run_test.recipe_context import (
    RECIPE_CONTEXT_FILE,
    write_recipe_context,
)

logger = logging.getLogger(__name__)

RECIPE_BIN_NAME = 'recipe_bin'
RECIPE_CMD_FILE = 'recipe_cmd'
RECIPE_HASH_FILE = '.recipe_hash'
RECIPE_ENV_FILE = 'env.json.txt'

POLL_INTERVAL = 0.5  # секунд между итерациями polling loop


def read_head_tail(filename, length):
    """Прочитать начало и хвост файла суммарно до length символов."""
    with io.open(filename, errors='ignore', encoding='utf-8') as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(0, os.SEEK_SET)
        data = f.read(length)
        pos = size - length * 2
        if pos > 0:
            f.seek(pos + length, os.SEEK_SET)
            return data + "\n...\n" + f.read()
        return data + f.read()


def _read_snippet_for_error(filename):
    """Прочитать фрагмент файла (начало + конец) для вставки в сообщение об ошибке."""
    if not filename or not os.path.exists(filename):
        return ''
    try:
        return read_head_tail(filename, const.REPORT_SNIPPET_LIMIT)
    except Exception:
        return ''


# ---------------------------------------------------------------------------
# Иерархия исключений рецептов


class RecipeError(Exception):
    """Базовый класс для ошибок рецептов."""

    RECIPE_ERROR_MESSAGE_LIMIT = 1024

    def __init__(self, name, err_filename, out_filename):
        super(RecipeError, self).__init__()
        self.name = name
        self.err_filename = err_filename
        self.out_filename = out_filename
        self.err_snippet = _read_snippet_for_error(err_filename).strip()

    def format_full(self):
        return "{}\n[[bad]]Stderr tail:[[bad]]{}[[rst]]".format(
            self.format_info(), self.err_snippet[-self.RECIPE_ERROR_MESSAGE_LIMIT :]
        )

    def format_info(self):
        return "[[bad]]{}: {} failed".format(type(self).__name__, self.name)


class RecipeStartUpError(RecipeError):
    pass


class RecipeTearDownError(RecipeError):
    pass


class RecipeTimeoutError(RecipeError):
    pass


class RecipeConflictError(Exception):
    """Рецепт уже используется другим ya-bin."""


class RecipeWaitTimeout(Exception):
    """Превышено время ожидания старта рецептов."""


# ---------------------------------------------------------------------------
# Декодирование dart-поля TEST-PERSISTENT-RECIPES


def decode_persistent_recipes(raw: str) -> list[str]:
    """
    base64 → плоский список package_path.

    Формат (производится в _dart_fields.py::TestPersistentRecipes.value):
        каждый вызов USE_PERSISTENT_RECIPE — одна строка,
        пути внутри вызова разделены пробелами.

    Пример:
        USE_PERSISTENT_RECIPE(recipe/a recipe/b)
        USE_PERSISTENT_RECIPE(recipe/c)
        → "recipe/a recipe/b\\nrecipe/c"
        → ['recipe/a', 'recipe/b', 'recipe/c']
    """
    text = base64.b64decode(raw.encode('utf-8')).decode('utf-8')
    result = []
    for line in text.splitlines():
        result.extend(p for p in line.split() if p)
    return result


# ---------------------------------------------------------------------------
# Вспомогательные функции


def _make_recipe_uid(package_path: str) -> str:
    """md5(package_path) — уникальный ID рецепта для RM."""
    return md5_value(package_path)


def _shallow_recipe_dir(shallow_root: str, package_path: str) -> str:
    """Каталог рецепта в data/ внутри shallow root.

    Recipe working dirs live in data/ — the subdirectory that gets cleaned on
    RM restart. run_test never touches meta/.
    """
    safe_path = package_path.replace('/', '__')
    return os.path.join(shallow_root, "data", safe_path, 'persistent')


def _get_build_root_tmp_recipe_dir(package_path: str) -> str:
    """Путь к 'фейковому shallow root' рецепта в TMPDIR чанка (embedded fallback)."""
    safe_path = package_path.replace('/', '__')
    return os.path.join(tempfile.gettempdir(), 'persistent_recipe', safe_path)


def _read_recipe_cmd(build_root: str, package_path: str) -> str:
    """Прочитать команду запуска из recipe_cmd файла в build root."""
    cmd_file = os.path.join(build_root, package_path, RECIPE_CMD_FILE)
    with open(cmd_file) as f:
        return f.read().strip()


def _read_recipe_cmd_from_shallow(shallow_dir: str) -> str:
    """Прочитать команду из shallow_dir/package/recipe_cmd (сохранённую при последнем старте)."""
    cmd_file = os.path.join(shallow_dir, 'package', RECIPE_CMD_FILE)
    with open(cmd_file) as f:
        return f.read().strip()


def _compute_hash(build_root: str, package_path: str, recipe_cmd: str) -> str:
    """md5 от контента артефактов PACKAGE + команды запуска."""
    pkg_dir = os.path.join(build_root, package_path)
    return md5_value(md5_path(pkg_dir) + recipe_cmd)


def _read_stored_hash(shallow_dir: str) -> str:
    """Прочитать сохранённый хэш из shallow root.

    Поднимает FileNotFoundError если файл отсутствует — это внештатная ситуация
    (например, пользователь удалил ~/.ya пока watchdog ещё не среагировал).
    """
    with open(os.path.join(shallow_dir, RECIPE_HASH_FILE)) as f:
        return f.read().strip()


def _write_stored_hash(shallow_dir: str, content_hash: str) -> None:
    with open(os.path.join(shallow_dir, RECIPE_HASH_FILE), 'w') as f:
        f.write(content_hash)


def _clean_dir(path: str, keep: list[str]) -> None:
    """Очистить каталог, кроме файлов/папок из списка keep."""
    for entry in os.listdir(path):
        if entry not in keep:
            full = os.path.join(path, entry)
            if os.path.isdir(full):
                shutil.rmtree(full)
            else:
                os.remove(full)


def _read_env_file(env_file: str) -> dict[str, str | None]:
    """
    Читает env.json.txt в формате jsonlines (каждая строка — {"KEY": "VALUE"}).
    Совпадает с форматом set_env() из library/python/testing/recipe.

    None — валидное значение: означает unset переменной окружения.
    Поднимает FileNotFoundError если файл отсутствует — внештатная ситуация.
    """
    result: dict[str, str | None] = {}
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    data = json.loads(line)
                    for k, v in data.items():
                        result[k] = v
                except (ValueError, KeyError):
                    pass
    return result


@functools.cache
def _get_rm_client(shallow_root: str) -> object:
    """Создать RM клиент по shallow_root (кэшируется на процесс run_test)."""
    from devtools.recipe_manager.client.client import RecipeManagerClient

    return RecipeManagerClient(shallow_root)


def _build_recipe_cmd(shallow_dir: str, extra_args: list[str], action: str) -> list[str]:
    """Собрать команду запуска/остановки рецепта (start или stop).

    --build-root, --source-root, --env-file не передаются явно — рецепт
    читает их из контекстного файла (YA_TEST_CONTEXT_FILE).
    """
    bin_path = os.path.join(shallow_dir, 'package', RECIPE_BIN_NAME)
    return [bin_path] + [action] + extra_args


# ---------------------------------------------------------------------------
# Хелперы для логирования ошибок


def _is_grpc_deadline_exceeded(exc) -> bool:
    """Вернуть True если exc — grpc-ошибка DEADLINE_EXCEEDED (таймаут рецепта в RM)."""
    try:
        import grpc

        return isinstance(exc, grpc.RpcError) and exc.code() == grpc.StatusCode.DEADLINE_EXCEEDED
    except Exception:
        return False


def append_traceback_to_file(path: str) -> None:
    """Дописать текущий traceback в файл (создать если нет)."""
    try:
        with open(path, 'a') as f:
            f.write('\nException:\n')
            traceback.print_exc(file=f)
    except Exception:
        logger.warning("Failed to append traceback to %s", path)


def _make_log_safe_name(package_path: str, uid8: str) -> str:
    """Build a human-readable, filesystem-safe name for recipe log files.

    Uses safe_path (slashes replaced with '__') truncated to 200 chars plus
    '_<uid8>' suffix for uniqueness when truncated.  The resulting name fits
    within PC_NAME_MAX=255 even with the 'recipe_persistent_start_' prefix and
    '.err' extension (24 + 200 + 1 + 8 + 4 = 237 chars).
    """
    safe_path = package_path.replace('/', '__')
    return '{}_{}'.format(safe_path[:200], uid8)


def _copy_recipe_logs(
    src_dir: str,
    out_dir: str,
    safe_name: str,
    action: str = 'start',
) -> tuple[str, str]:
    """Copy recipe_{action}.out/err from src_dir to out_dir.

    Also copies any flat files from src_dir/output/ into out_dir so that
    additional logs written by the recipe via output_path become available.

    Returns (out_dst, err_dst) — destination paths in out_dir (build root).
    If a src file does not exist, an empty dst is created so that RecipeError
    always receives valid paths.
    """
    result = []
    for suffix in ('out', 'err'):
        src = os.path.join(src_dir, 'recipe_{}.{}'.format(action, suffix))
        dst = os.path.join(out_dir, 'recipe_persistent_{}_{}.{}'.format(action, safe_name, suffix))
        if os.path.exists(src):
            shutil.copy2(src, dst)
        else:
            open(dst, 'a').close()
        result.append(dst)

    # Copy flat files from output/ — recipe may write extra logs there.
    output_dir = os.path.join(src_dir, 'output')
    if os.path.isdir(output_dir):
        for name in os.listdir(output_dir):
            src_file = os.path.join(output_dir, name)
            if os.path.isfile(src_file):
                shutil.copy2(src_file, os.path.join(out_dir, name))

    return result[0], result[1]  # out_dst, err_dst


# ---------------------------------------------------------------------------
# Запуск одного рецепта (вызывается под локом)


def _do_stop_and_start(
    recipe_uid: str,
    package_path: str,
    shallow_dir: str,
    rm: object,
    options: object,
    rm_timeout: float = 0,
) -> None:
    """Остановить существующий рецепт (если есть) и запустить новый."""
    # Читаем команду из shallow_dir/package/ — там артефакты от предыдущего старта.
    # Не используем build_root: он мог измениться относительно того, чем рецепт был запущен.
    old_recipe_cmd = _read_recipe_cmd_from_shallow(shallow_dir)
    extra_args = shlex.split(old_recipe_cmd)[1:]

    stop_err = os.path.join(shallow_dir, 'recipe_stop.err')
    stop_out = os.path.join(shallow_dir, 'recipe_stop.out')

    logger.debug("Stopping recipe %s before restart", package_path)
    stop_cmd = _build_recipe_cmd(shallow_dir, extra_args, 'stop')
    ctx_path = os.path.join(shallow_dir, RECIPE_CONTEXT_FILE)
    try:
        rm.stop_recipe(
            recipe_uid=recipe_uid,
            command=stop_cmd,
            err_filename=stop_err,
            out_filename=stop_out,
            env={'YA_TEST_CONTEXT_FILE': ctx_path},
            timeout=rm_timeout,
        )
    except Exception as exc:
        append_traceback_to_file(stop_err)
        if _is_grpc_deadline_exceeded(exc):
            raise RecipeTimeoutError(package_path, stop_err, stop_out) from exc
        raise RecipeTearDownError(package_path, stop_err, stop_out) from exc

    _do_start(recipe_uid, package_path, shallow_dir, rm, options, rm_timeout=rm_timeout)


def _do_start(
    recipe_uid: str,
    package_path: str,
    shallow_dir: str,
    rm: object,
    options: object,
    rm_timeout: float = 0,
) -> None:
    """Подготовить shallow dir и запустить рецепт через RM.

    Любая ошибка (файловая, grpc, ...) конвертируется в RecipeStartUpError
    с путями к логам в shallow_dir. Обогащение путями до out_dir происходит
    в run_all_persistent_recipes.

    DEADLINE_EXCEEDED от RM конвертируется в RecipeTimeoutError.
    """
    from devtools.recipe_manager.client.client import Lifetime

    start_err = os.path.join(shallow_dir, 'recipe_start.err')
    start_out = os.path.join(shallow_dir, 'recipe_start.out')
    try:
        recipe_cmd = _read_recipe_cmd(options.build_root, package_path)
        current_hash = _compute_hash(options.build_root, package_path, recipe_cmd)
        extra_args = shlex.split(recipe_cmd)[1:]

        # Очистить shallow dir (кроме lock-файла)
        _clean_dir(shallow_dir, keep=['.lock'])

        # Скопировать артефакты из build root в shallow root
        pkg_src = os.path.join(options.build_root, package_path)
        pkg_dst = os.path.join(shallow_dir, 'package')
        shutil.copytree(pkg_src, pkg_dst)

        # Подготовить recipe context — рецепт читает его через YA_TEST_CONTEXT_FILE
        ctx_path = write_recipe_context(shallow_dir, options)

        # Записать хэш
        _write_stored_hash(shallow_dir, current_hash)

        full_cmd = _build_recipe_cmd(shallow_dir, extra_args, 'start')

        logger.debug("Starting persistent recipe %s", package_path)

        env = {'YA_TEST_CONTEXT_FILE': ctx_path}
        if options.create_root_guidance_file:
            # XXX: --ext-py mode
            env['Y_PYTHON_SOURCE_ROOT'] = options.source_root
            env['PYTHONPYCACHEPREFIX'] = options.pycache_prefix
        rm.start_recipe(
            invocation_id=options.invocation_id,
            build_id=options.build_id,
            recipe_uid=recipe_uid,
            command=full_cmd,
            lifetime=Lifetime.PERSISTENT,
            working_dir=shallow_dir,
            out_filename=start_out,
            err_filename=start_err,
            retry_count=1,
            env=env,
            timeout=rm_timeout,
        )
        import app_ctx

        app_ctx.display.emit_message(
            "Persistent recipe [[imp]]{}[[rst]] launched in [[path]]{}[[rst]]\n".format(package_path, shallow_dir)
        )
    except Exception as exc:
        append_traceback_to_file(start_err)
        if _is_grpc_deadline_exceeded(exc):
            raise RecipeTimeoutError(package_path, start_err, start_out) from exc
        raise RecipeStartUpError(package_path, start_err, start_out) from exc


def _start_one_recipe(package_path: str, options: object, rm_timeout: float = 0) -> None:
    """
    Запустить или переиспользовать один persistent рецепт.

    Вызывается уже под FileLock на shallow dir рецепта.
    Опрашивает RM через GetRecipe и принимает решение о запуске/ассоциации.

    Пользователи рецепта в RM хранятся как кортежи (invocation_id, build_id).
    Один ya-bin (invocation_id) может запускать несколько сборок (build_id).

    Алгоритм:
      GetRecipe → NOT_FOUND:
        → StartRecipe

      GetRecipe → FOUND:
        нет пользователей (все умерли по heartbeat timeout или FinishBuild):
          хэш совпадает → AssociateRecipe
          хэш не совпадает → StopRecipe + StartRecipe

        same_build (пользователи с тем же invocation_id:build_id):
          хэш совпадает → AssociateRecipe (другой чанк той же сборки уже запустил)
          хэш не совпадает → RecipeConflictError (разные платформы, одна сборка)

        same_invocation, другой build_id (другая сборка того же ya-bin):
          хэш совпадает → AssociateRecipe
          хэш не совпадает → RecipeConflictError (другая сборка — нельзя перезапустить)

        other_invocation (пользователи с другим invocation_id):
          force → RecipeConflictError
          хэш совпадает → AssociateRecipe
          хэш не совпадает → RecipeConflictError
    """
    import grpc

    recipe_uid = _make_recipe_uid(package_path)
    shallow_dir = _shallow_recipe_dir(options.shallow_root, package_path)
    os.makedirs(shallow_dir, exist_ok=True)

    rm = _get_rm_client(options.shallow_root)
    force = options.force_restart_recipes
    current_invocation_id = options.invocation_id
    current_build_id = options.build_id

    # Запросить актуальное состояние рецепта
    try:
        recipe_info = rm.get_recipe(recipe_uid)
        recipe_found = True
        users = {(u.invocation_id, u.build_id) for u in recipe_info.users}
    except grpc.RpcError as e:
        if e.code() == grpc.StatusCode.NOT_FOUND:
            recipe_found = False
            users = set()
        else:
            raise

    if not recipe_found:
        # Рецепт не запущен — запускаем
        logger.debug("Recipe %s not found in RM, starting fresh", package_path)
        _do_start(recipe_uid, package_path, shallow_dir, rm, options, rm_timeout=rm_timeout)
        return

    # Рецепт запущен — классифицируем пользователей
    current_user = (current_invocation_id, current_build_id)
    same_build = {u for u in users if u == current_user}
    same_invocation = {u for u in users if u[0] == current_invocation_id and u != current_user}
    other_invocation = {u for u in users if u[0] != current_invocation_id}
    no_users = not users

    # Хэш нужен во всех ветках кроме NOT_FOUND
    recipe_cmd = _read_recipe_cmd(options.build_root, package_path)
    current_hash = _compute_hash(options.build_root, package_path, recipe_cmd)
    stored_hash = _read_stored_hash(shallow_dir)

    if no_users:
        # Нет пользователей — все умерли (heartbeat timeout или FinishBuild)
        if current_hash == stored_hash:
            if force:
                logger.debug("Recipe %s: no users, hash matches, force=True → restart", package_path)
                _do_stop_and_start(recipe_uid, package_path, shallow_dir, rm, options, rm_timeout=rm_timeout)
            else:
                logger.debug("Recipe %s: no users, hash matches → associate", package_path)
                rm.associate_recipe(current_invocation_id, current_build_id, recipe_uid)
        else:
            logger.debug("Recipe %s: no users, hash changed → restart", package_path)
            _do_stop_and_start(recipe_uid, package_path, shallow_dir, rm, options, rm_timeout=rm_timeout)

    elif same_build and not same_invocation and not other_invocation:
        # Только чанки той же сборки — другой чанк уже запустил рецепт
        if current_hash == stored_hash:
            logger.debug("Recipe %s: same build users only, hash matches → associate", package_path)
            rm.associate_recipe(current_invocation_id, current_build_id, recipe_uid)
        else:
            # Хэш отличается внутри одной сборки — конфликт платформ.
            # Возможно при нескольких --target-platform с platform-зависимым
            # контентом PACKAGE (например, IF (MUSL) в ya.make).
            raise RecipeConflictError(
                "Recipe {!r}: hash mismatch within the same build (stored={}, current={}). "
                "This can happen when multiple --target-platform flags produce different PACKAGE "
                "artifacts for the same recipe path. Persistent recipes do not support "
                "multi-platform builds with platform-dependent recipe content.".format(
                    package_path, stored_hash, current_hash
                )
            )

    elif same_invocation and not other_invocation:
        # Другая сборка того же ya-bin использует рецепт (ya package, параллельные сборки)
        if current_hash == stored_hash:
            logger.debug("Recipe %s: same invocation other build users, hash matches → associate", package_path)
            rm.associate_recipe(current_invocation_id, current_build_id, recipe_uid)
        else:
            raise RecipeConflictError(
                "Recipe {!r}: hash mismatch — another build of the same ya-bin is using "
                "a different version of this recipe (stored={}, current={}, users={}). "
                "Cannot restart while another build is running.".format(
                    package_path, stored_hash, current_hash, same_invocation
                )
            )

    else:
        # Есть пользователи от другого ya-bin
        all_other = same_invocation | other_invocation
        if force:
            raise RecipeConflictError(
                "Cannot force-restart recipe {!r}: it is still in use by other ya test runs "
                "(users={}). Stop all other running ya test sessions first.".format(package_path, all_other)
            )
        if current_hash == stored_hash:
            logger.debug("Recipe %s: other users exist, hash matches → associate", package_path)
            rm.associate_recipe(current_invocation_id, current_build_id, recipe_uid)
        else:
            raise RecipeConflictError(
                "Cannot restart recipe {!r} (recipe code changed): it is still in use by other "
                "ya test runs (users={}). Stop all other running ya test sessions "
                "and try again.".format(package_path, all_other)
            )


# ---------------------------------------------------------------------------
# Polling loop — главная точка входа


def run_all_persistent_recipes(
    options: object,
    out_dir: str,
    enable_recipe_timeout: bool,
    chunk_time_left: object,
) -> dict[str, str | None]:
    """
    Запустить все persistent рецепты сьюты через Recipe Manager.

    Для каждого рецепта пытается взять неблокирующий FileLock на его shallow dir
    и под локом вызывает _start_one_recipe, которая через GetRecipe решает:
    запустить новый рецепт, перезапустить изменившийся или ассоциироваться
    с уже работающим. Рецепты, лок на которые не удалось взять (другой чанк
    уже работает), пропускаются и опрашиваются на следующей итерации.

    Цикл завершается когда все рецепты успешно обработаны (StartRecipe или
    AssociateRecipe). После этого читает env.json.txt каждого рецепта и
    возвращает накопленные env-переменные.

    Логи каждого рецепта копируются в out_dir сразу после его обработки
    (как при успехе, так и при ошибке). При ошибке:
    - grpc-ошибка или другая при старте → конвертируется в RecipeStartUpError
      с путями к логам в out_dir (build root)
    - ошибка при stop в _do_stop_and_start → конвертируется в RecipeTearDownError
      с путями к stop-логам в out_dir
    - RecipeConflictError → пробрасывается как есть (нет err/out файлов)

    Уже запущенные persistent рецепты при ошибке НЕ останавливаются — они
    живут в RM и будут переиспользованы следующим запуском.

    Таймаут:
    - enable_recipe_timeout=False (нет node_timeout): ждёт бесконечно
    - enable_recipe_timeout=True: ограничивает ожидание chunk_time_left секундами
    """
    packages = decode_persistent_recipes(options.persistent_recipes)
    if not packages:
        return {}

    all_uids = {_make_recipe_uid(p): p for p in packages}
    done_uids = set()  # рецепты, для которых StartRecipe/AssociateRecipe прошёл успешно

    deadline = None
    if enable_recipe_timeout and chunk_time_left is not None:
        deadline = time.time() + chunk_time_left

    while done_uids != all_uids.keys():
        if deadline is not None and time.time() > deadline:
            pending = [all_uids[uid] for uid in all_uids if uid not in done_uids]
            raise RecipeWaitTimeout("Timed out waiting for persistent recipes to start: {}".format(pending))

        made_progress = False
        for uid, pkg in all_uids.items():
            if uid in done_uids:
                continue

            shallow_dir = _shallow_recipe_dir(options.shallow_root, pkg)
            os.makedirs(shallow_dir, exist_ok=True)
            lock = FileLock(os.path.join(shallow_dir, '.lock'))
            safe_name = _make_log_safe_name(pkg, uid[:8])

            if lock.acquire(blocking=False):
                try:
                    rm_timeout = max(0.0, deadline - time.time()) if deadline is not None else 0
                    _start_one_recipe(pkg, options, rm_timeout=rm_timeout)
                    done_uids.add(uid)
                    made_progress = True
                    # Успех — копируем start-логи и output/ в out_dir
                    _copy_recipe_logs(shallow_dir, out_dir, safe_name, action='start')
                except RecipeConflictError:
                    # Нет err/out файлов рецепта — пробрасываем как есть
                    raise
                except RecipeError as e:
                    # RecipeStartUpError или RecipeTearDownError из _do_start/_do_stop_and_start.
                    # e.err_filename/out_filename указывают на shallow_dir — копируем логи
                    # в out_dir и пересоздаём исключение того же типа с правильными путями.
                    action = 'stop' if isinstance(e, RecipeTearDownError) else 'start'
                    out_dst, err_dst = _copy_recipe_logs(shallow_dir, out_dir, safe_name, action=action)
                    raise type(e)(pkg, err_dst, out_dst) from None
                finally:
                    lock.release()

        if not made_progress:
            time.sleep(POLL_INTERVAL)

    # Все рецепты "осознаны" чанком как работающие — собираем их env
    result_env: dict[str, str | None] = {}
    for pkg in packages:
        shallow_dir = _shallow_recipe_dir(options.shallow_root, pkg)
        recipe_env = _read_env_file(os.path.join(shallow_dir, RECIPE_ENV_FILE))
        result_env.update(recipe_env)
        logger.debug("Recipe %s is done, collected env keys: %s", pkg, list(recipe_env.keys()))

    return result_env


# ---------------------------------------------------------------------------
# Embedded fallback для persistent рецептов
#
# Используется когда --use-persistent-recipes не передан (distbuild / YT / Sandbox),
# но в сьюте есть USE_PERSISTENT_RECIPE.  Рецепт запускается "как embedded" в
# выделенном подкаталоге внутри TMPDIR чанка.
# stop вызывается в finally вызывающего кода (run_test.py).


def _run_recipe_cmd(
    full_cmd: list[str],
    recipe_env: dict[str, str],
    cwd: str,
    out_filename: str,
    err_filename: str,
    enable_recipe_timeout: bool,
    chunk_time_left: object,
) -> None:
    """Запустить один рецептный процесс и дождаться завершения.

    Использует process.execute из devtools.ya.test.system.process —
    тот же модуль, что и основной run_test.

    Raises:
        process.ExecutionTimeoutError: если enable_recipe_timeout и time exhausted.
        Exception: при ненулевом коде возврата рецепта.
    """
    from devtools.ya.test.system import process

    res = process.execute(
        command=full_cmd,
        env=recipe_env,
        wait=False,
        cwd=cwd,
        stderr=err_filename,
        stdout=out_filename,
    )
    if enable_recipe_timeout and chunk_time_left is not None:
        res.wait(timeout=chunk_time_left)
    else:
        res.wait()


def run_all_persistent_recipes_embedded(
    options: object,
    out_dir: str,
    enable_recipe_timeout: bool,
    chunk_time_left: object,
) -> tuple[dict[str, str | None], list[str]]:
    """
    Embedded fallback для persistent рецептов (без Recipe Manager).

    Используется когда --use-persistent-recipes не передан (distbuild / YT / Sandbox).
    Запускает рецепты из options.persistent_recipes независимо — каждый в своём
    подкаталоге внутри TMPDIR чанка. Рецепты не видят env друг друга;
    единственный канал коммуникации рецепт → тест: env.json.txt каждого рецепта.

    Логи каждого рецепта пишутся в tmp_dir (recipe_start.out/err), а затем
    копируются в out_dir (build root). При ошибке грамматика ошибки конвертируется
    в RecipeStartUpError с путями к логам в out_dir — снаружи tmp_dir не виден.

    Возвращает:
    - dict: накопленные env-переменные от всех успешно запущенных рецептов
    - list[str]: package_path рецептов, которые фактически поднялись
      (для последующего вызова stop_persistent_recipes_embedded)
    """
    packages = decode_persistent_recipes(options.persistent_recipes)
    if not packages:
        return {}, []

    result_env: dict[str, str | None] = {}
    started_persistent_embedded_recipes: list[str] = []

    for package_path in packages:
        recipe_uid = _make_recipe_uid(package_path)
        safe_name = _make_log_safe_name(package_path, recipe_uid[:8])
        tmp_dir = _get_build_root_tmp_recipe_dir(package_path)
        os.makedirs(tmp_dir, exist_ok=True)

        # Скопировать артефакты PACKAGE из build root в tmp
        pkg_src = os.path.join(options.build_root, package_path)
        pkg_dst = os.path.join(tmp_dir, 'package')
        shutil.copytree(pkg_src, pkg_dst)

        # Создать recipe.context (идентично persistent режиму)
        ctx_path = write_recipe_context(tmp_dir, options)

        # Собрать команду start
        recipe_cmd_str = _read_recipe_cmd(options.build_root, package_path)
        extra_args = shlex.split(recipe_cmd_str)[1:]
        start_cmd = _build_recipe_cmd(tmp_dir, extra_args, 'start')

        # YA_TEST_CONTEXT_FILE → recipe.context (не test.context!)
        recipe_env = dict(os.environ)
        recipe_env['YA_TEST_CONTEXT_FILE'] = ctx_path

        # Логи пишем в tmp_dir, потом копируем в out_dir
        out_filename = os.path.join(tmp_dir, 'recipe_start.out')
        err_filename = os.path.join(tmp_dir, 'recipe_start.err')

        logger.debug(
            "Starting persistent recipe %s in embedded fallback mode at %s",
            package_path,
            tmp_dir,
        )
        try:
            _run_recipe_cmd(
                start_cmd,
                recipe_env,
                tmp_dir,
                out_filename,
                err_filename,
                enable_recipe_timeout,
                chunk_time_left,
            )
            # Рецепт поднялся — фиксируем в списке поднятых
            started_persistent_embedded_recipes.append(package_path)
            out_dst, err_dst = _copy_recipe_logs(tmp_dir, out_dir, safe_name, action='start')
        except Exception as e:
            # Записываем трейсбек, копируем логи в out_dir, конвертируем ошибку
            append_traceback_to_file(err_filename)
            out_dst, err_dst = _copy_recipe_logs(tmp_dir, out_dir, safe_name, action='start')
            from devtools.ya.test.system import process as _process

            if isinstance(e, _process.ExecutionTimeoutError):
                raise RecipeTimeoutError(package_path, err_dst, out_dst) from e
            raise RecipeStartUpError(package_path, err_dst, out_dst) from e

        # Прочитать env, записанный рецептом через set_env()
        recipe_env_vars = _read_env_file(os.path.join(tmp_dir, RECIPE_ENV_FILE))
        result_env.update(recipe_env_vars)

        logger.debug(
            "Persistent recipe %s started in embedded fallback, env keys: %s",
            package_path,
            list(recipe_env_vars.keys()),
        )

    return result_env, started_persistent_embedded_recipes


def stop_persistent_recipes_embedded(
    started_persistent_embedded_recipes: list[str],
    options: object,
    out_dir: str,
    enable_recipe_timeout: bool,
    chunk_time_left: object,
) -> list[tuple]:
    """
    Вызвать stop для persistent рецептов, запущенных в embedded fallback.

    Принимает явный список фактически поднятых пакетов (из
    run_all_persistent_recipes_embedded) — останавливает только их.

    Логи stop пишутся в tmp_dir, затем копируются в out_dir (build root).
    Возвращаемые пути в tuple указывают на out_dir — снаружи tmp_dir не виден.

    Выполняется в обратном порядке (зеркально start).
    Ошибки накапливаются и возвращаются — не прерывают цикл остановки.

    Возвращает список (package_path, err_dst, out_dst, exception).
    """
    errors = []
    for package_path in reversed(started_persistent_embedded_recipes):
        recipe_uid = _make_recipe_uid(package_path)
        safe_name = _make_log_safe_name(package_path, recipe_uid[:8])
        tmp_dir = _get_build_root_tmp_recipe_dir(package_path)
        ctx_path = os.path.join(tmp_dir, RECIPE_CONTEXT_FILE)

        recipe_cmd_str = _read_recipe_cmd_from_shallow(tmp_dir)
        extra_args = shlex.split(recipe_cmd_str)[1:]
        stop_cmd = _build_recipe_cmd(tmp_dir, extra_args, 'stop')

        out_filename = os.path.join(tmp_dir, 'recipe_stop.out')
        err_filename = os.path.join(tmp_dir, 'recipe_stop.err')
        recipe_env = dict(os.environ)
        recipe_env['YA_TEST_CONTEXT_FILE'] = ctx_path
        try:
            _run_recipe_cmd(
                stop_cmd,
                recipe_env,
                tmp_dir,
                out_filename,
                err_filename,
                enable_recipe_timeout,
                chunk_time_left,
            )
            logger.debug("Persistent recipe %s stopped (embedded fallback)", package_path)
            out_dst, err_dst = _copy_recipe_logs(tmp_dir, out_dir, safe_name, action='stop')
        except Exception as e:
            logger.warning(
                "Failed to stop persistent recipe %s in embedded fallback: %s",
                package_path,
                e,
            )
            append_traceback_to_file(err_filename)
            out_dst, err_dst = _copy_recipe_logs(tmp_dir, out_dir, safe_name, action='stop')
            from devtools.ya.test.system import process as _process

            if isinstance(e, _process.ExecutionTimeoutError):
                converted = RecipeTimeoutError(package_path, err_dst, out_dst)
            else:
                converted = RecipeTearDownError(package_path, err_dst, out_dst)
            errors.append((package_path, err_dst, out_dst, converted))
    return errors
