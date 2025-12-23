import logging

from devtools.ya.core import stage_tracer


def _print_stat() -> None:
    logging.info("--- Stage durations")
    stat = stage_tracer.get_stat("gradle")
    stages = []
    predeep = 1000
    deep_i_min = {0: -1}
    for stage in stat.keys():
        deep = stage.count('>')
        if predeep > deep:
            for d in range(0, deep + 1):
                if d in deep_i_min:
                    m = deep_i_min[d]
                else:
                    deep_i_min[d] = m
            i = len(stages) - 1
            while (i > deep_i_min[deep]) and (stages[i].count('>') > deep):
                i -= 1
            stages.insert(i + 1, stage)
            deep_i_min[deep] = len(stages) - 1
        else:
            stages.append(stage)
        predeep = deep
    for stage in stages:
        if stage == 'summary':
            continue
        shift = "    " * (stage.count('>') + 1)
        pos = stage.rfind('>')
        logging.info("%s%s: %3.3f sec", shift, stage if pos < 0 else stage[pos + 1 :], stat[stage].duration)
    logging.info("=== %3.3f sec", stat['summary'].duration)
