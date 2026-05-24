"""
Helpers for creating a recipe execution context (recipe.context) inside the
shallow root directory.  Persistent recipes use this instead of the regular
YA_TEST_CONTEXT_FILE so that yatest.common sees correct paths.
"""

import json
import os
import sys

RECIPE_CONTEXT_FILE = 'recipe.context'


def write_recipe_context(shallow_recipe_dir: str, options: object) -> str:
    """
    Write recipe.context into *shallow_recipe_dir*.

    Returns the path to the written file (passed to the recipe as
    YA_TEST_CONTEXT_FILE env variable).
    """
    work_path = os.path.join(shallow_recipe_dir, 'work')
    env_file = os.path.join(shallow_recipe_dir, 'env.json.txt')

    os.makedirs(work_path, exist_ok=True)

    context = {
        'runtime': {
            'build_root': os.path.join(shallow_recipe_dir, 'package'),
            'work_path': work_path,
            # XXX: must set output_path and source_root otherwise they will get
            # detected automatically, we don't want that
            'output_path': 'n/a',
            'source_root': 'n/a',
            'test_tool_path': sys.argv[0],
            'python_bin': options.python_bin,
        },
        'resources': {
            'global': options.global_resources,
        },
        'internal': {
            'env_file': env_file,
        },
    }

    ctx_path = os.path.join(shallow_recipe_dir, RECIPE_CONTEXT_FILE)
    with open(ctx_path, 'w') as f:
        json.dump(context, f, indent=2)
    return ctx_path
