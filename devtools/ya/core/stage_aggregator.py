import time


class Aggregator:
    name = 'aggregator'

    def is_event_applicable(self, event_name):
        return event_name.startswith(self.name)

    def snowden_entry_name(self):
        return self.name + "-walltime_sec"

    def get_walltime(self, stages):
        start = time.time()
        end = 0
        for stage_name, stat in stages.items():
            if self.is_event_applicable(stage_name):
                start = min(stat.intervals[0][0], start)
                end = max(stat.intervals[-1][1], end)

        if end > 0:
            return {self.snowden_entry_name(): end - start}
        else:
            return {}


class FakeYaAggregator(Aggregator):
    name = 'fakeya'


class YaScriptAggregator(Aggregator):
    name = 'ya-script'


class YaBinPreparationAggregator(Aggregator):
    contents = set(
        [
            'binary-initialization',
            'main-processing',
            'modules-initialization-lite',
            'handler-selection',
            'modules-initialization-full',
        ]
    )
    name = 'ya-bin-preparation'

    def is_event_applicable(self, event_name):
        return event_name in self.contents


class InvokationAggregator(Aggregator):
    name = 'invocation'

    def is_event_applicable(self, event_name):
        return event_name.startswith('invoke')


class BootstrapAggregator(Aggregator):
    name = 'bootstrap'

    def is_event_applicable(self, event_name):
        # We imply that all events that do not meet this condition
        # happen before first invokation.
        return not event_name.startswith('invoke')


def get_aggregators():
    return [
        FakeYaAggregator(),
        YaScriptAggregator(),
        YaBinPreparationAggregator(),
        InvokationAggregator(),
        BootstrapAggregator(),
    ]
