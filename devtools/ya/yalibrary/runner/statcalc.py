import collections


def calc_stat(task_infos):
    by_type = collections.defaultdict(lambda: collections.defaultdict(int))
    s = collections.defaultdict(int)

    for group in task_infos:
        for task_info in group:
            ts = task_info.timing[1] - task_info.timing[0]
            name = task_info.task.short_name()
            by_type[name]['sum'] += ts
            by_type[name]['qty'] += 1

            s['qty'] += 1
            s['sum'] += ts

    return {
        'by_type': by_type,
        'all': s,
    }


def calc_critical(task_infos):
    crit_time = {}
    back_edge = {}
    max_crit_task = None
    task_mapping = {}
    for group in task_infos:
        task_info = group[-1]
        task = task_info.task
        task_mapping[task] = task_info
        delta = task_info.timing[1] - task_info.timing[0]

        crit_time[task] = delta
        for x in task_info.deps:
            if x in crit_time and crit_time[x] + delta > crit_time[task]:
                crit_time[task] = crit_time[x] + delta
                back_edge[task] = x

        if max_crit_task is None or crit_time[max_crit_task] < crit_time[task]:
            max_crit_task = task

    crit_path = []
    while max_crit_task is not None:
        crit_path.append(max_crit_task)
        max_crit_task = back_edge.get(max_crit_task)

    return list(reversed([task_mapping[x] for x in crit_path]))
