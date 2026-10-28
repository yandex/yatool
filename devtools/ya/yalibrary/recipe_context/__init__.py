"""One recipe association lifetime per executable local build."""

import contextlib
import logging
from dataclasses import dataclass

from library.python import unique_id

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class RecipeBuildContext:
    invocation_id: str
    build_id: str
    manager_root: str

    def environment(self) -> dict[str, str]:
        return {
            '_YA_INVOCATION_ID': self.invocation_id,
            '_YA_BUILD_ID': self.build_id,
            '_YA_RECIPE_MANAGER_ROOT': self.manager_root,
        }


@contextlib.contextmanager
def recipe_build_context(manager, invocation_id: str, *, id_factory=unique_id.gen16):
    if manager is None:
        yield None
        return
    context = RecipeBuildContext(invocation_id, id_factory(), manager.manager_root)
    try:
        yield context
    finally:
        try:
            manager.finish_build(invocation_id=context.invocation_id, build_id=context.build_id)
        except Exception:
            logger.debug('Failed to notify RM about build finish (build_id=%s)', context.build_id, exc_info=True)
