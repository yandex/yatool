import devtools.ya.core.report as report
import enum
import logging
from typing import Any, Callable

logger = logging.getLogger(__name__)
LabelValue = Any | Callable[[], Any]


class MetricNames(enum.StrEnum):
    YA_STARTED = enum.auto()
    YA_FINISHED = enum.auto()
    YT_CACHE_ERROR = enum.auto()


class MetricStore:
    def __init__(self, labels: dict[str, LabelValue], telemetry):
        self.labels = labels
        self.telemetry = telemetry

    @staticmethod
    def _resolve_labels(labels: dict[str, LabelValue]) -> dict[str, Any]:
        resolved = {}
        for name, value in labels.items():
            if callable(value):
                try:
                    value = value()
                except Exception:
                    logger.debug("Failed to resolve metric label %s", name, exc_info=True)
                    value = "unknown"
            resolved[name] = value
        return resolved

    def report_metric(
        self,
        name: MetricNames,
        labels: dict[str, LabelValue] | None = None,
        value: int = 1,
        urgent: bool = False,
        report_type: report.ReportTypes = report.ReportTypes.YA_METRICS,
    ):
        labels = labels or {}
        metric_name = name.value
        metric_labels = self._resolve_labels(self.labels | labels | {"name": metric_name})
        self.telemetry.report(
            f"{report_type}_{metric_name}",
            {
                "metrics": [
                    {
                        "labels": metric_labels,
                        "value": value,
                    }
                ]
            },
            urgent=urgent,
        )
