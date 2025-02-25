import typing as tp  # noqa: F401
import logging
from pathlib import Path

import six

from jinja2 import Environment, select_autoescape
from yalibrary.debug_store.processor._common import pretty_date, pretty_time, pretty_delta
import library.python.resource as rs


def _sort_files_info(files):
    # type: (dict[str, dict[str, tp.Any]]) -> tp.Iterable[str, dict]
    for k_v in sorted(files.items(), key=lambda k_v: k_v[1]['orig_key']):
        yield k_v[0], k_v[1]


class HTMLGenerator:
    def __init__(
        self,
        debug_bundle,
        files,
        debug_bundle_file,
        is_last=True,
        path_to_repro=None,
        fully_restored_repo=False,
    ):
        self.logger = logging.getLogger("{}.{}".format(__name__, self.__class__.__name__))

        self.debug_bundle = debug_bundle
        self.files = files  # type: dict[str, dict[str, tp.Any]]
        self.debug_bundle_file = debug_bundle_file
        self.is_last = is_last
        self.path_to_repro = path_to_repro
        self.fully_restored_repo = fully_restored_repo

        self.env = Environment(autoescape=select_autoescape())
        self.env.filters['pretty_date'] = pretty_date
        self.env.filters['pretty_time'] = pretty_time
        self.env.filters['pretty_delta'] = pretty_delta
        self.env.filters['sort_files_info'] = _sort_files_info

        self.template = self.env.from_string(six.ensure_str(rs.resfs_read("template.jinja2")))

    def _found_tracebacks(self, log_file):
        if log_file is None:
            return

        log_file = Path(log_file)

        current_tb_lines = []
        just_found = False
        prev_line = None

        for line_index, line in enumerate(log_file.open("r").readlines()):
            if "Traceback (most recent call last):" in line or "ERROR" in line:
                just_found = True

                if prev_line:
                    current_tb_lines.append("{}:{}\n{}\n".format(log_file, line_index, "=" * 50))
                    current_tb_lines.append(prev_line)

            if current_tb_lines:
                current_tb_lines.append(line)

                if not just_found:
                    if (line[0].isnumeric() and "async exception" not in line) or len(
                        current_tb_lines
                    ) > 100:  # log continues
                        if len(current_tb_lines) > 100:
                            current_tb_lines.append("...\n")
                        yield ''.join(current_tb_lines).strip()
                        current_tb_lines = []

                just_found = False

            prev_line = line

        if current_tb_lines:
            yield ''.join(current_tb_lines).strip()

    def _get_local_place(self, orig_file_name):
        if orig_file_name is None:
            return None

        orig_file_name = str(orig_file_name)

        for path, item in self.files.items():
            if item['original_path'] == orig_file_name:
                if item['status'] != 'OK':
                    return None
                return path

    def generate(self, to_path):
        init_time = self.debug_bundle.get('__meta__', {}).get('init_time', None)

        orig_log_file = self.debug_bundle.get('log_file', None)
        log_file = self._get_local_place(orig_log_file)

        evlog_file = self._get_local_place(self.debug_bundle.get('evlog_file', None))

        username = self.debug_bundle.get('params', {}).get("username")

        runs, run_infos, run_errors = self._check_ymake_runs()

        finish_time = self.debug_bundle.get('finish_time')
        data = {
            'is_last': self.is_last,
            'path_to_repro': self.path_to_repro,
            'fully_restored_repo': self.fully_restored_repo,
            'cmd': ' '.join(self.debug_bundle.get('argv', [])),
            'cwd': self.debug_bundle.get('cwd'),
            'debug_bundle_file': self.debug_bundle_file,
            'init_time': init_time,
            'log_file': log_file,
            'evlog_file': evlog_file,
            'username': username,
            'files': self.files,
            'tracebacks': list(self._found_tracebacks(orig_log_file)),
            'runs': runs,
            'run_infos': run_infos,
            'run_errors': run_errors,
            'vcs_info': self.debug_bundle.get('vcs_info'),
            'ya_version_info': self.debug_bundle.get('ya_version_info'),
            'platforms': self.debug_bundle.get('platforms'),
            'system_info': self.debug_bundle.get('system_info'),
            'session_id': self.debug_bundle.get('session_id'),
            'handler': self.debug_bundle.get('handler'),
            'finish_time': finish_time,
            'duration': (finish_time - init_time) if finish_time else None,
            'resources': self.debug_bundle.get('resources', {}),
            'exit_code': self.debug_bundle.get('exit_code'),
        }
        to_path.write_text(six.ensure_text(self.template.render(**data)), encoding='utf-8')

    def _check_ymake_runs(self):
        info = {
            'start': float("+inf"),
            'finish': 0,
        }
        runs = info['runs'] = {}

        for key, value in self.debug_bundle.items():
            if "ymake" not in key or 'run' not in key or not isinstance(value, dict):
                continue

            run = runs[key] = {}
            run['stages'] = value['run']['stages']
            run['purpose'] = value['run']['purpose']

            for stage_info in value['run']['stages'].values():
                if stage_info['start'] < info['start']:
                    info['start'] = stage_info['start']

                if stage_info['finish'] > info['finish']:
                    info['finish'] = stage_info['finish']

        duration = info['finish'] - info['start']

        items = {}
        errors = []

        for key, run in runs.items():
            item = items[key] = {'purpose': run['purpose'], 'stages': {}}

            try:
                for stage, stage_info in run['stages'].items():
                    if None in stage_info.values():
                        errors.append("Error in stage {}: {}".format(stage, stage_info))
                        continue

                    item['stages'][stage] = {
                        'left': (stage_info['start'] - info['start']) / duration,
                        'width': stage_info['duration'] / duration,
                        'start': stage_info['start'],
                        'finish': stage_info['finish'],
                    }
            except Exception:
                self.logger.exception("While processing item %s", key)

        return (
            items,
            {
                'start': info['start'],
                'duration': info['finish'] - info['start'],
            },
            errors,
        )
