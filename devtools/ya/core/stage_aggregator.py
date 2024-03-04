import time


class Aggregator:
    name = 'aggregator'
    suffix = '-walltime_sec'

    def is_event_applicable(self, event_name):
        return event_name.startswith(self.name)

    def snowden_entry_name(self):
        return self.name + self.suffix

    def applicable_events(self, stages):
        """
        Yields one interval from stage list at a time
        """
        for stage_name, stats in stages.items():
            if self.is_event_applicable(stage_name):
                for stat in stats.intervals:
                    yield stat

    def aggregate(self, stages):
        start = time.time()
        end = 0
        for stat in self.applicable_events(stages):
            start = min(stat[0], start)
            end = max(stat[1], end)

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
        return event_name.startswith(YaScriptAggregator.name) or event_name.startswith(FakeYaAggregator.name)


class YaScriptDownloadsAggregator(Aggregator):
    name = 'ya-script-downloads'
    suffix = ''

    def is_event_applicable(self, event_name):
        return event_name == "ya-script-download"

    def aggregate(self, stages):
        cnt = len(list(self.applicable_events(stages)))

        if cnt > 0:
            return {self.snowden_entry_name(): cnt}
        else:
            return {}


def get_aggregators():
    return [
        FakeYaAggregator(),
        YaScriptAggregator(),
        YaBinPreparationAggregator(),
        InvokationAggregator(),
        BootstrapAggregator(),
    ]
