from yalibrary.status_view import plain  # noqa
from yalibrary.status_view.status import Status, DummyListener  # noqa
from yalibrary.status_view.term_view import TermView, TickThrottle  # noqa
from yalibrary.status_view.jsonl import JsonlProgressView  # noqa


def create_view(status, display, stage='build', output_replacements=None, patterns=None, **term_view_kwargs):
    # type: (Status, object, str, list | None, object, **object) -> JsonlProgressView | TermView
    """Pick the view matching the display: progress events for a structured one, a terminal ticker otherwise."""
    if getattr(display, 'structured', False):
        return JsonlProgressView(
            status, display, stage=stage, output_replacements=output_replacements, patterns=patterns
        )
    return TermView(status, display, output_replacements=output_replacements, patterns=patterns, **term_view_kwargs)
