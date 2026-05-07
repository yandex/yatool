import os
import logging
import exts.yjson as json

logger = logging.getLogger(__name__)


# In cases where the change list exceeds this threshold,
# The full list of changed files may be to heavy for sending.
# In this case we will send a statistics per file types.
BIG_CHANGE_FILES_THRESHOLD = 100

# In cases where the change list exceeds this threshold,
# we assume that calculating of full information may be too expensive.
# So in this case we will only have to send the size of the change list.
HUGE_CHANGE_FILES_THRESHOLD = 200


def _extract_paths_from_cl_data(cl_data, paths, deleted_paths):
    for entry in cl_data:
        for name_entry in entry.get('names', []):
            path = name_entry.get('path')
            status = name_entry.get('status')
            if not path:
                continue
            if status == 'deleted':
                deleted_paths.add(path)
            else:
                paths.add(path)


def _extract_paths_from_store(store_path):
    paths = set()
    deleted_paths = set()
    for filename in os.listdir(store_path):
        if not filename.endswith('.cl'):
            continue
        filepath = os.path.join(store_path, filename)
        try:
            with open(filepath, 'r') as f:
                cl_data = json.load(f)

            _extract_paths_from_cl_data(cl_data, paths, deleted_paths)
        except Exception as e:
            logger.debug("Failed to read changelist file %s: %s", filepath, e)
    return list(paths), list(deleted_paths)


def _get_mtime(arc_root, path):
    if not arc_root:
        return None
    full_path = os.path.join(arc_root, path)
    try:
        return os.path.getmtime(full_path)
    except OSError:
        return None


def _calculate_dropped_details_stats(paths, deleted_paths):
    return {'level': 'dropped', 'total_files': len(paths), 'total_deleted_files': len(deleted_paths), 'data': {}}


def _calculate_aggregated_stats(paths, deleted_paths, arc_root):
    aggregated = {}
    for path in paths:
        mtime = _get_mtime(arc_root, path)

        _, ext = os.path.splitext(path)
        if ext not in aggregated:
            aggregated[ext] = {'count': 0, 'min_mtime': None, 'max_mtime': None}

        agg = aggregated[ext]
        agg['count'] += 1
        if mtime is not None:
            if agg['min_mtime'] is None or mtime < agg['min_mtime']:
                agg['min_mtime'] = mtime
            if agg['max_mtime'] is None or mtime > agg['max_mtime']:
                agg['max_mtime'] = mtime

    deleted_aggregated = {}
    for path in deleted_paths:
        _, ext = os.path.splitext(path)
        if ext not in deleted_aggregated:
            deleted_aggregated[ext] = {'count': 0}
        deleted_aggregated[ext]['count'] += 1

    return {
        'level': 'aggregated',
        'total_files': len(paths),
        'total_deleted_files': len(deleted_paths),
        'data': aggregated,
        'deleted_data': deleted_aggregated,
    }


def _calculate_detailed_stats(paths, deleted_paths, arc_root):
    res = []
    for path in paths:
        mtime = _get_mtime(arc_root, path)
        if mtime is not None:
            mtime = int(mtime)
        res.append({'path': path, 'mtime': mtime})

    return {
        'level': 'detailed',
        'total_files': len(paths),
        'total_deleted_files': len(deleted_paths),
        'data': res,
        'deleted_data': deleted_paths,
    }


def calculate_changelist_stats(paths, deleted_paths, arc_root):
    total_files = len(paths)

    if total_files > HUGE_CHANGE_FILES_THRESHOLD:
        return _calculate_dropped_details_stats(paths, deleted_paths)

    if total_files > BIG_CHANGE_FILES_THRESHOLD:
        return _calculate_aggregated_stats(paths, deleted_paths, arc_root)

    return _calculate_detailed_stats(paths, deleted_paths, arc_root)


def get_changelist_stats():
    try:
        import app_ctx

        store_path = getattr(app_ctx, 'changelist_store', None)
        if not store_path or not os.path.exists(store_path):
            return None

        paths, deleted_paths = _extract_paths_from_store(store_path)

        params = getattr(app_ctx, 'params', None)
        arc_root = getattr(params, 'arc_root', None) if params else None

        return calculate_changelist_stats(paths, deleted_paths, arc_root)
    except Exception as e:
        logger.debug("Failed to get changelist stats: %s", e)
        return None
