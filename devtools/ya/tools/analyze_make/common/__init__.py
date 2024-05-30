import logging


logger = logging.getLogger(__name__)


class Node(object):
    def __init__(self, name, tag, start, end, color, thread_name, is_critical, event=None, has_detailed=False):
        self.name = name
        self.tag = tag
        self.start = start
        self.end = end
        self.color = color
        self.thread_name = thread_name
        self.is_critical = is_critical
        self.event = event
        self.has_detailed = has_detailed


def load_from_file(evlog_reader, mode, detailed=False):
    if mode == 'evlog':
        return load_from_evlog(evlog_reader, detailed)
    elif mode == 'distbuild':
        return load_from_distbuild(evlog_reader)


def load_from_distbuild(evlog_reader):
    n = []
    for i, record in enumerate(evlog_reader):
        record['index'] = i
        n.append(record)

    # assign virtual threads
    actions = []
    for node in n:
        actions.append((node['start_time'], -1, node['index']))
        actions.append((node['finish_time'], 1, node['index']))
    actions.sort()
    inUse = []
    for action in actions:
        if action[1] == 1:
            inUse[n[action[2]]['thread']] = 0
            continue
        threadNum = len(inUse)
        i = 0
        while i < threadNum:
            if inUse[i] == 0:
                inUse[i] = 1
                n[action[2]]['thread'] = i
                break
            i += 1
        if i == threadNum:
            threadNum += 1
            n[action[2]]['thread'] = i
            inUse.append(1)

    for node in n:
        yield Node(
            name=node['task_uid'],
            tag='',
            start=node['start_time'],
            end=node['finish_time'],
            thread_name=node['thread'],
            color='green',
            is_critical=False,
        )


def load_from_evlog(evlog_reader, detailed=False):
    ymake_stage_started = 'NEvent.TStageStarted'
    ymake_stage_finished = 'NEvent.TStageFinished'

    events_to_check = ['node-finished', 'stage-finished']
    if detailed:
        events_to_check.append('node-detailed')

    nodes = []
    critical_uids = {}

    opened_ymake_stages = {}
    ymake_nodes = []

    for v in evlog_reader:
        if v['event'] in events_to_check:
            tm = v['value']['time']
            if tm:
                nodes.append(v)
        if v['event'] == 'critical_path':
            critical_uids = set(x['uid'] for x in v['value']['nodes'])

        if v['namespace'] == 'ymake':
            if v['event'] == ymake_stage_started:
                opened_ymake_stages[(v['thread_name'], v['value']['StageName'])] = v
            elif v['event'] == ymake_stage_finished:
                ymake_nodes.append((opened_ymake_stages[(v['thread_name'], v['value']['StageName'])], v))

    for x, y in ymake_nodes:
        yield Node(
            name=x['value']['StageName'],
            tag='YG',
            start=x['value']['_timestamp'] / 1e6,
            end=y['value']['_timestamp'] / 1e6,
            color='black',
            thread_name=x['thread_name'],
            is_critical=True,
        )

    for x in nodes:
        timing = x['value']['time']
        short_name = x['value'].get('tag', '??')
        yield Node(
            name=x['value']['name'],
            tag=short_name,
            start=timing[0],
            end=timing[1],
            color=short_name,
            thread_name=x['thread_name'],
            is_critical=x['value'].get('uid') in critical_uids,
            event=x['event'],
            has_detailed=x['value']['name'].startswith('Run'),
        )


def set_zero_start(nodes):
    nodes = list(nodes)
    if not nodes:
        return
    min_time = min(x.start for x in nodes)
    for x in nodes:
        x.start -= min_time
        x.end -= min_time
        yield x
