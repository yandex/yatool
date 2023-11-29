import re

import six


class ResolvingError(Exception):
    pass


def resolve(text, resolver):
    while True:
        resolved = re.sub(r'\$\{(?P<path>[^{}]+?)\}', lambda m: resolver(m.group('path')), text)
        if resolved == text:
            return resolved
        text = resolved


def resolver(pom, upward_parents, props):
    def _resolver(path):
        parts = path.split('.')
        head, tail = parts[0], parts[1:]

        if head in ['project', 'pom']:

            def res(p):
                try:
                    e = p
                    for x in tail:
                        e = e.find(x)
                    return e.text
                except AttributeError:
                    raise ResolvingError("Can't resolve {}".format(path))

            for p in [pom] + upward_parents:
                try:
                    return res(p)
                except ResolvingError:
                    continue

            raise ResolvingError("Can't resolve {}".format(path))

        elif head in ['env', 'settings', 'java']:
            raise ResolvingError('{} is not supported'.format(head))
        else:
            try:
                return props[path]
            except KeyError:
                raise ResolvingError("Can't resolve {}".format(path))

    return _resolver


def extract_raw_properties(pom):
    props = {}
    try:
        for elem in pom.find('properties'):
            props[elem.tag] = elem.text or ''
    except TypeError:
        pass

    return props


def keep_going(resolver):
    def r(path):
        try:
            return resolver(path)
        except ResolvingError:
            return '${%s}' % path

    return r


def relax_properties(props):
    for k, v in six.iteritems(props):
        resolved_v = resolve(v, keep_going(resolver(None, [], props)))
        if resolved_v != v:
            props[k] = resolved_v
            return True
    return False


def extract_all_this_resolved_stuff_from_above(pom, pom_getter):
    props = extract_raw_properties(pom)

    while relax_properties(props):
        pass

    res = resolver(pom, [], props)

    try:
        parent_elem = pom.find('parent')

        parent_group = resolve(parent_elem.find('groupId').text, res)
        parent_artifact = resolve(parent_elem.find('artifactId').text, res)
        parent_version = resolve(parent_elem.find('version').text, res)

        parent = pom_getter(parent_group, parent_artifact, parent_version)

        parent_props, parent_deps, parent_depman, upward_parents = extract_all_this_resolved_stuff_from_above(
            parent, pom_getter
        )

        parent_props.update(props)

        props = parent_props

        dependency_management = parent_depman
        dependencies = parent_deps
    except AttributeError:
        upward_parents = []
        dependency_management = {}
        dependencies = {}

    def res(text, keepon=False):
        r = resolver(pom, upward_parents, props)

        if keepon:
            r = keep_going(r)

        return resolve(text, r)

    def extract_dep_values(dep):
        group = res(dep.find('groupId').text)
        artifact = res(dep.find('artifactId').text)

        try:
            version = res(dep.find('version').text)
        except (ResolvingError, AttributeError):
            version = None

        try:
            scope = res(dep.find('scope').text)
        except (ResolvingError, AttributeError):
            scope = None

        try:
            type_ = res(dep.find('type').text)
        except (ResolvingError, AttributeError):
            type_ = 'jar'

        try:
            classifier = res(dep.find('classifier').text)
        except (ResolvingError, AttributeError):
            classifier = None

        try:
            optional = res(dep.find('optional').text) == 'true'
        except (ResolvingError, AttributeError):
            optional = False

        return {
            'group': group,
            'artifact': artifact,
            'version': version,
            'scope': scope,
            'type': type_,
            'classifier': classifier,
            'optional': optional,
        }

    try:
        for dep in pom.find('dependencyManagement').find('dependencies'):
            dep_vals = extract_dep_values(dep)

            if dep_vals['scope'] == 'import':
                dep_pom = pom_getter(dep_vals['group'], dep_vals['artifact'], dep_vals['version'])

                _, _, dep_depman, _ = extract_all_this_resolved_stuff_from_above(dep_pom, pom_getter)

                dep_depman.update(dependency_management)

                dependency_management = dep_depman
            else:
                dependency_management[
                    (dep_vals['group'], dep_vals['artifact'], dep_vals['type'], dep_vals['classifier'])
                ] = dep_vals

    except (TypeError, AttributeError):
        pass

    try:
        for dep in pom.find('dependencies'):
            dep_vals = extract_dep_values(dep)

            if dep_vals['scope'] == 'import':
                dep_pom = pom_getter(dep_vals['group'], dep_vals['artifact'], dep_vals['version'])

                _, deps, _, _ = extract_all_this_resolved_stuff_from_above(dep_pom, pom_getter)

                deps.update(dependencies)

                dependencies = deps

            elif dep_vals['scope'] not in ['test', 'provided', 'system'] and not dep_vals['optional']:
                if dep_vals['version'] is None:
                    v = dependency_management[
                        (dep_vals['group'], dep_vals['artifact'], dep_vals['type'], dep_vals['classifier'])
                    ]
                else:
                    v = dep_vals

                assert v['version']

                dependencies[(v['group'], v['artifact'], v['type'], v['classifier'])] = (
                    v['group'],
                    v['artifact'],
                    v['version'],
                )

    except (TypeError, AttributeError):
        pass

    return props, dependencies, dependency_management, [pom] + upward_parents


def extract_deps(pom, pom_getter):
    _, deps, _, _ = extract_all_this_resolved_stuff_from_above(pom, pom_getter)

    return deps
