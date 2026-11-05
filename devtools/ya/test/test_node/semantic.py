"""Shared dependency trimming for semantic-linter report nodes."""

from devtools.ya.test.test_types.common import SemanticLinterSuite


def strip_dependencies(plan, suites):
    strip_dependency_inputs(
        plan,
        (
            output
            for suite in suites
            if isinstance(suite, SemanticLinterSuite)
            for output in suite.semantic_linter_inputs
        ),
    )


def strip_dependency_inputs(plan, outputs):
    def get_diff(lhs, rhs):
        return list(sorted(set(lhs) - set(rhs)))

    seen_uids = {}

    for out in outputs:
        uid = plan.get_uids_by_outputs(out)
        if not uid:
            continue

        node = plan.get_node_by_uid(uid[0])
        uids_to_remove = set()
        inputs_to_remove = set()
        for dep in node["deps"]:
            if dep in seen_uids:
                inputs = seen_uids[dep]
            else:
                dep_node = plan.get_node_by_uid(dep)
                if dep_node["kv"].get("p") != "CC":
                    inputs = dep_node["outputs"]
                else:
                    inputs = []
                seen_uids[dep] = inputs
            if inputs:
                inputs_to_remove.update(inputs)
                uids_to_remove.add(dep)

        node["inputs"] = get_diff(node["inputs"], inputs_to_remove)
        node["deps"] = get_diff(node["deps"], uids_to_remove)
