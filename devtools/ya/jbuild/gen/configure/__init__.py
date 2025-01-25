import collections


def conflicted_versions(resolution, conflicts):
    if resolution is not None:
        yield resolution.split('/')[-1]
    for lib in conflicts:
        yield lib.split('/')[-1]


class ConfigureError(Exception):
    mute = True


class PathConfigureError(object):
    def __init__(self):
        self.is_missing = False
        self.missing_peerdirs = []
        self.missing_recurses = []
        self.missing_tools = []
        self.missing_inputs = []
        self.parse_error = None
        self.dm_recent_versions_peerdirs = collections.defaultdict(list)
        self.dm_different_versions_peerdirs = collections.defaultdict(list)
        self.conflicted_versions_peerdirs = collections.defaultdict(list)
        self.default_versions_peerdirs = []
        self.direct_peerdirs = []
        self.forbidden_deps = []
        self.without_dm = []

    def __str__(self):
        if self.is_missing:
            return 'is missing'

        elif self.parse_error:
            return self.parse_error

        s = []

        s.extend('missing peerdir ' + x for x in self.missing_peerdirs)
        s.extend('missing recurse ' + x for x in self.missing_recurses)
        s.extend('missing tool ' + x for x in self.missing_tools)
        s.extend('missing input ' + x for x in self.missing_inputs)
        s.extend('default version ' + x for x in self.default_versions_peerdirs)
        s.extend('direct version ' + x for x in self.direct_peerdirs)
        s.extend('resolved without dependency_management ' + x for x in self.without_dm)
        s.extend(
            'auto resolved versions conflict {} ({} chosen)'.format(', '.join(conflicted_versions(key, vals)), key)
            for key, vals in sorted(self.conflicted_versions_peerdirs.items())
        )
        s.extend(
            'more recent libraries versions in peerdirs {} ({} required by DEPENDENCY_MANAGEMENT)'.format(
                ', '.join(conflicted_versions(None, vals)), key
            )
            for key, vals in sorted(self.dm_recent_versions_peerdirs.items())
        )
        s.extend(
            'different libraries versions in peerdirs {} ({} required by DEPENDENCY_MANAGEMENT)'.format(
                ', '.join(conflicted_versions(key, vals)), key
            )
            for key, vals in sorted(self.dm_different_versions_peerdirs.items())
        )
        s.extend('forbidden dependency' + x for x in self.forbidden_deps)

        return '\n'.join(s)

    def get_colored_errors(self):
        if self.is_missing:
            yield 'Directory is missing', '-WBadDir'
            return
        elif self.parse_error:
            yield 'Parse error {}'.format(self.parse_error), '-WSyntax'
            return

        for x in self.missing_peerdirs:
            yield '[[alt1]]PEERDIR[[rst]] to missing directory: [[imp]]{}[[rst]]'.format(x), '-WBadDir'
        for x in self.missing_recurses:
            yield '[[alt1]]RECURSE[[rst]] to missing directory: [[imp]]{}[[rst]]'.format(x), '-WBadDir'
        for x in self.missing_tools:
            yield 'Trying to use [[alt1]]TOOL[[rst]] from missing directory: [[imp]]{}[[rst]]'.format(x), '-WBadDir'
        for x in self.missing_inputs:
            yield 'Trying to set a [[alt1]]JAVA_SRCS[[rst]] for a missing directory: [[imp]]{}[[rst]]'.format(
                x
            ), '-WBadDir'
        for x in self.default_versions_peerdirs:
            yield 'Dependency with [[alt1]]default[[rst]] version: [[imp]]{}[[rst]]'.format(x), '-WMisconfiguration'
        for x in self.direct_peerdirs:
            yield 'PEERDIR to [[alt1]]direct[[rst]] version: [[imp]]{}[[rst]]'.format(x), '-WMisconfiguration'
        for x in self.without_dm:
            yield 'Dependency version resolved [[alt1]]without DEPENDENCY_MANAGEMENT[[rst]]: [[imp]]{}[[rst]]'.format(
                x
            ), '-WMisconfiguration'
        for key, vals in sorted(self.conflicted_versions_peerdirs.items()):
            yield '[[alt1]]Auto resolved[[rst]] versions conflict: {} ([[imp]]{}[[rst]] chosen)'.format(
                ', '.join(conflicted_versions(key, vals)), key
            ), '-WMisconfiguration'
        for key, vals in sorted(self.dm_recent_versions_peerdirs.items()):
            yield '[[alt1]]More recent[[rst]] libraries versions in PEERDIRs: {} ([[imp]]{}[[rst]] required by DEPENDENCY_MANAGEMENT)'.format(
                ', '.join(conflicted_versions(None, vals)), key
            ), '-WMisconfiguration'
        for key, vals in sorted(self.dm_different_versions_peerdirs.items()):
            yield '[[alt1]]Different[[rst]] libraries versions in PEERDIRs: {} ([[imp]]{}[[rst]] required by DEPENDENCY_MANAGEMENT)'.format(
                ', '.join(conflicted_versions(key, vals)), key
            ), '-WMisconfiguration'
        for x in self.forbidden_deps:
            yield '[[alt1]]Forbidden dependency[[rst]]: [[imp]]{}[[rst]]'.format(x), '-WMisconfiguration'
