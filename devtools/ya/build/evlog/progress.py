from enum import Enum
import threading
import collections
import six

import devtools.ya.core.event_handling as event_handling


class Mode(Enum):
    NOT_STARTED = 1
    NO_PRINTING = 2
    MODULES_ONLY = 3
    MODULES_AND_FILES = 4
    MODULES_AND_RENDERED = 5
    MODULES_FILES_AND_RENDERED = 6


class ModulesFilesStatistic:
    def __init__(self, stream, is_rewritable):
        self._stream = stream
        self._lock = threading.Lock()
        self._mode = Mode.NOT_STARTED
        self._modules_done = 0
        self._modules_total = 0
        self._files_read = 0
        self._rendered_done = 0
        self._rendered_total = 0
        self._next_timestamp_update = None
        self._last_module_timestamp = None
        self._module_stats_exist = False
        self._have_new_modules_info = False
        self._have_new_files_info = False
        self._have_new_rendered_info = False
        self._current_ymake_processing = 0

        if is_rewritable:
            self._preperiod_mcs = 1 * 10**6
            self._period_mcs = 0
            self._files_threshold_mcs = 3 * 10**6
        else:
            self._preperiod_mcs = 15 * 10**6
            self._period_mcs = 15 * 10**6
            self._files_threshold_mcs = 15 * 10**6

    def _print_message(self):
        if self._mode == Mode.NOT_STARTED or self._mode == Mode.NO_PRINTING:
            return
        if self._mode == Mode.MODULES_ONLY:
            self._stream(
                "[{ymake} ymakes processing] [{done}/{total} modules configured]".format(
                    ymake=self._current_ymake_processing,
                    done=self._modules_done,
                    total=self._modules_total,
                )
            )
        if self._mode == Mode.MODULES_AND_FILES:
            self._stream(
                "[{ymake} ymakes processing] [{done}/{total} modules configured] [{files} files read]".format(
                    ymake=self._current_ymake_processing,
                    done=self._modules_done if self._module_stats_exist else "?",
                    total=self._modules_total if self._module_stats_exist else "?",
                    files=self._files_read,
                )
            )
        if self._mode == Mode.MODULES_AND_RENDERED:
            self._stream(
                "[{ymake} ymakes processing] [{done}/{total} modules configured] [{r_done}/{r_total} modules rendered]".format(
                    ymake=self._current_ymake_processing,
                    done=self._modules_done if self._module_stats_exist else "?",
                    total=self._modules_total if self._module_stats_exist else "?",
                    r_done=self._rendered_done,
                    r_total=self._rendered_total,
                )
            )
        if self._mode == Mode.MODULES_FILES_AND_RENDERED:
            self._stream(
                "[{ymake} ymakes processing] [{done}/{total} modules configured] [{files} files read] [{r_done}/{r_total} modules rendered]".format(
                    ymake=self._current_ymake_processing,
                    done=self._modules_done if self._module_stats_exist else "?",
                    total=self._modules_total if self._module_stats_exist else "?",
                    files=self._files_read,
                    r_done=self._rendered_done,
                    r_total=self._rendered_total,
                )
            )

    def _update_stats(self, event, delta_done, delta_total, delta_files, delta_rendered, delta_rendered_total):
        if delta_done:
            self._modules_done += delta_done
            self._have_new_modules_info |= delta_done > 0
        if delta_total:
            self._modules_total += delta_total
            self._have_new_modules_info |= delta_total > 0
        if delta_files:
            self._files_read += delta_files
            self._have_new_files_info |= delta_files > 0
        if delta_rendered:
            self._rendered_done += delta_rendered
            self._have_new_rendered_info |= delta_rendered > 0
        if delta_rendered_total:
            self._rendered_total += delta_rendered_total
            self._have_new_rendered_info |= delta_rendered_total > 0
        if event["_typename"] == "NEvent.TConfModulesStat":
            self._module_stats_exist = True
            self._last_module_timestamp = event["_timestamp"]

    def _have_new_info(self):
        if self._mode == Mode.MODULES_ONLY:
            return self._have_new_modules_info
        elif self._mode == Mode.MODULES_AND_FILES:
            return self._have_new_modules_info or self._have_new_files_info
        if self._mode == Mode.MODULES_AND_RENDERED:
            return self._have_new_modules_info or self._have_new_rendered_info
        elif self._mode == Mode.MODULES_FILES_AND_RENDERED:
            return self._have_new_modules_info or self._have_new_files_info or self._have_new_rendered_info
        else:
            return False

    def _set_new_info_false(self):
        if self._mode == Mode.MODULES_ONLY:
            self._have_new_modules_info = False
        elif self._mode == Mode.MODULES_AND_FILES:
            self._have_new_modules_info = False
            self._have_new_files_info = False
        elif self._mode == Mode.MODULES_AND_RENDERED:
            self._have_new_modules_info = False
            self._have_new_rendered_info = False
        elif self._mode == Mode.MODULES_FILES_AND_RENDERED:
            self._have_new_modules_info = False
            self._have_new_files_info = False
            self._have_new_rendered_info = False

    def _try_print_message(self, timestamp):
        if not self._have_new_info():
            return
        self._next_timestamp_update = timestamp + self._period_mcs
        self._print_message()
        self._set_new_info_false()

    def handler(
        self, event, delta_done=None, delta_total=None, delta_files=None, delta_rendered=None, delta_rendered_total=None
    ):
        typename = event["_typename"]
        timestamp = event["_timestamp"]
        with self._lock:
            self._update_stats(event, delta_done, delta_total, delta_files, delta_rendered, delta_rendered_total)

            if typename == "NEvent.TStageStarted" and event["StageName"] == "ymake run":
                self._current_ymake_processing += 1

                if self._mode == Mode.NOT_STARTED:
                    self._mode = Mode.NO_PRINTING
                    self._next_timestamp_update = timestamp + self._preperiod_mcs

            if self._mode == Mode.NO_PRINTING:
                if self._next_timestamp_update <= timestamp:
                    self._mode = Mode.MODULES_ONLY

            if self._mode == Mode.MODULES_ONLY:
                if typename == "NEvent.TFilesStat":
                    if (
                        not self._module_stats_exist
                        or self._last_module_timestamp + self._files_threshold_mcs < timestamp
                    ):
                        self._mode = Mode.MODULES_AND_FILES
                elif typename == "NEvent.TRenderModulesStat":
                    self._mode = Mode.MODULES_AND_RENDERED
                elif typename == "NEvent.TConfModulesStat":
                    if self._next_timestamp_update <= timestamp or event['Total'] == event['Done']:
                        self._try_print_message(timestamp)

            if self._mode == Mode.MODULES_AND_FILES:
                if typename == "NEvent.TConfModulesStat" or typename == "NEvent.TFilesStat":
                    if self._next_timestamp_update <= timestamp:
                        self._try_print_message(timestamp)
                elif typename == "NEvent.TRenderModulesStat":
                    self._mode = Mode.MODULES_FILES_AND_RENDERED

            if self._mode == Mode.MODULES_AND_RENDERED:
                if typename == "NEvent.TFilesStat":
                    if (
                        not self._module_stats_exist
                        or self._last_module_timestamp + self._files_threshold_mcs < timestamp
                    ):
                        self._mode = Mode.MODULES_FILES_AND_RENDERED
                elif typename == "NEvent.TConfModulesStat" or typename == "NEvent.TRenderModulesStat":
                    if self._next_timestamp_update <= timestamp or event['Total'] == event['Done']:
                        self._try_print_message(timestamp)

            if self._mode == Mode.MODULES_FILES_AND_RENDERED:
                if (
                    typename == "NEvent.TConfModulesStat"
                    or typename == "NEvent.TFilesStat"
                    or typename == "NEvent.TRenderModulesStat"
                ):
                    if self._next_timestamp_update <= timestamp:
                        self._try_print_message(timestamp)

            if typename == "NEvent.TStageFinished" and event["StageName"] == "ymake run":
                self._current_ymake_processing -= 1
                self._try_print_message(timestamp)


class MixedProgressMeta(type(event_handling.SubscriberExcludedTopics), type(event_handling.SingletonSubscriber)):
    pass


class PrintProgressSubscriber(
    six.with_metaclass(MixedProgressMeta, event_handling.SubscriberExcludedTopics, event_handling.SingletonSubscriber)
):
    topics = {"NEvent.TNeedDirHint"}

    class YmakeLastState:
        def __init__(self):
            self.files_read = 0
            self.rendered_done = 0
            self.rendered_total = 0
            self.modules_done = 0
            self.modules_total = 0

    def __init__(self, params, display, logger):
        print_status = get_print_status_func(params, display, logger)
        self.modules_files_stats = ModulesFilesStatistic(
            stream=print_status, is_rewritable=getattr(params, "output_style", "") == "ninja"
        )
        self.ymake_states = collections.defaultdict(PrintProgressSubscriber.YmakeLastState)
        self._subscribers_count = 0
        self._lock = threading.Lock()

    def _action(self, event):
        # type: (dict) -> None
        ymake_run_uid = event.get('ymake_run_uid')
        if ymake_run_uid is None:
            return

        typename = event["_typename"]

        prev_ymake_state = self.ymake_states[ymake_run_uid]

        if typename == "NEvent.TConfModulesStat":
            self.modules_files_stats.handler(
                event,
                delta_done=event["Done"] - prev_ymake_state.modules_done,
                delta_total=event["Total"] - prev_ymake_state.modules_total,
            )
            prev_ymake_state.modules_done = event["Done"]
            prev_ymake_state.modules_total = event["Total"]
        elif typename == "NEvent.TFilesStat":
            self.modules_files_stats.handler(
                event,
                delta_files=event["Count"] - prev_ymake_state.files_read,
            )
            prev_ymake_state.files_read = event["Count"]
        elif typename == "NEvent.TRenderModulesStat":
            self.modules_files_stats.handler(
                event,
                delta_rendered=event["Done"] - prev_ymake_state.rendered_done,
                delta_rendered_total=event["Total"] - prev_ymake_state.rendered_total,
            )
            prev_ymake_state.rendered_done = event["Done"]
            prev_ymake_state.rendered_total = event["Total"]
        else:
            self.modules_files_stats.handler(event)


def get_print_status_func(opts, display, logger):
    if display:
        _fprint = display.emit_status if getattr(opts, "output_style", "") == "ninja" else display.emit_message
    else:
        _fprint = logger.debug

    def _print_status(msg):
        _fprint("[[imp]]{}[[rst]]".format(msg))

    return _print_status


# Todo: use @property
class ConfigureTask:
    def __init__(self, start, end, thread_name=None, debug_id=None):
        self.duration_ms = (end - start) / 1000
        self.start_time = start
        self.end_time = end
        self.thread_name = thread_name
        self.debug_id = debug_id

    def get_time_elapsed(self):
        return self.duration_ms

    def get_colored_name(self):
        return "[ymake {} {}]".format(self.thread_name, self.debug_id)

    def as_json(self):
        return {
            'type': "ymake thread",
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
        }

    def name(self):
        return " ymake {} ".format(self.thread_name)

    def _task_details(self):
        return ""


class YmakeTimeStatistic(event_handling.SubscriberSpecifiedTopics):
    topics = {"NEvent.TStageStarted", "NEvent.TStageFinished"}

    def __init__(self):
        self.current_working_ymakes = {}
        self.threads_time = []
        self.min_timestamp_ms = None
        self.max_timestamp_ms = None

    def _action(self, event):
        if event["_typename"] == "NEvent.TStageStarted" and event["StageName"] == "ymake run":
            ymake_run_uid = event['ymake_run_uid']

            self.current_working_ymakes[ymake_run_uid] = event['_timestamp']
            if self.min_timestamp_ms is None:
                self.min_timestamp_ms = event["_timestamp"] / 1000

        elif event["_typename"] == "NEvent.TStageFinished" and event["StageName"] == "ymake run":
            ymake_run_uid = event['ymake_run_uid']

            self.max_timestamp_ms = event["_timestamp"] / 1000
            self.threads_time.append(
                ConfigureTask(
                    start=self.current_working_ymakes[ymake_run_uid],
                    end=event["_timestamp"],
                    thread_name=ymake_run_uid,
                    debug_id=event.get("debug_id", 0),
                )
            )
            del self.current_working_ymakes[ymake_run_uid]
