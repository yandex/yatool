import os

import yalibrary.find_root

from build import makelist


class InvalidTargetSpecification(Exception):
    mute = True


class Targets(object):
    def __init__(self, root, targets):
        self.root = root
        self.targets = targets


def resolve(source_root, targets, cwd=None, root_detector=None):
    def root_detector_of(lst, detector):
        assert len(lst) >= 1
        roots = [detector(x) for x in lst]
        unique_roots = list(set(_f for _f in roots if _f))
        if len(unique_roots) > 1:
            raise InvalidTargetSpecification(
                'Cannot find common Arcadia root for all targets. Found roots: {}'.format(unique_roots)
            )
        elif len(unique_roots) == 0:
            raise InvalidTargetSpecification('Cannot find Arcadia root. Try to run command in Arcadia directory')
        return unique_roots[0]

    def fix_makelist_target(path):
        if os.path.basename(path) not in makelist.MAKELIST_FILENAMES:
            return path
        return os.path.dirname(path)

    assert targets is not None

    if cwd is None:
        cwd = os.getcwd()

    if not os.path.isabs(cwd):
        raise InvalidTargetSpecification('cwd must be absolute')

    if root_detector is None:
        root_detector = yalibrary.find_root.detect_root

    if source_root is not None:
        source_root = os.path.normpath(os.path.join(cwd, source_root))

    if source_root is not None and root_detector(source_root) != source_root:
        raise InvalidTargetSpecification('Source root {} is invalid'.format(source_root))

    targets = [os.path.join(source_root or cwd, x) for x in targets]
    targets = [fix_makelist_target(p) for p in targets]
    targets = [os.path.normpath(p) for p in targets]

    targets_root = None
    if targets:
        targets_root = root_detector_of(targets, root_detector)

        if targets_root is None:
            raise InvalidTargetSpecification('Targets root is invalid')

    if source_root is not None and targets:
        if source_root != targets_root:
            raise InvalidTargetSpecification(
                'Source root {} differ from {} targets root'.format(source_root, targets_root)
            )
        return Targets(targets_root, targets)

    cwd_root = root_detector(cwd)

    if source_root is not None:
        if cwd_root == source_root:
            return Targets(cwd_root, [cwd])
        return Targets(source_root, [source_root])

    if targets:
        return Targets(targets_root, targets)

    if cwd_root is None:
        raise InvalidTargetSpecification('Current directory is not in the source root')

    return Targets(cwd_root, [cwd])
