import logging

import six

logger = logging.getLogger(__name__)


def iter_missing_java_paths(errs):
    logger.debug('Checking out target dependencies')

    for path, err in six.iteritems(errs):
        for p in err.missing_inputs:
            yield p
