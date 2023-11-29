import logging

import six

logger = logging.getLogger(__name__)


def iter_missing_java_paths(errs):
    logger.debug('Checking out target dependencies')

    for path, err in six.iteritems(errs):
        if err.is_missing:
            yield path

        for p in err.missing_peerdirs + err.missing_recurses + err.missing_tools + err.missing_inputs:
            yield p
