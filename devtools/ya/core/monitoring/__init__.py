import devtools.ya.core.report as report
import enum


class MetricNames(enum.StrEnum):
    YA_STARTED = enum.auto()
    YA_FINISHED = enum.auto()


class MetricStore:
    def __init__(self, labels: dict[str, str], telemetry):
        self.labels = labels
        self.telemetry = telemetry

    def report_metric(
        self,
        name: MetricNames,
        labels: dict[str, str] | None = None,
        value: int = 1,
        urgent: bool = False,
    ):
        labels = labels or {}
        metric_name = name.value
        self.telemetry.report(
            f"{report.ReportTypes.YA_METRICS}_{metric_name}",
            {
                "metrics": [
                    {
                        "labels": self.labels | labels | {"name": metric_name},
                        "value": value,
                    }
                ]
            },
            urgent=urgent,
        )
