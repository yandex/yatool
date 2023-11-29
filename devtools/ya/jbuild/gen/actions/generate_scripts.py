import os

from . import parse
import jbuild.gen.base as base
import jbuild.gen.consts as consts
import jbuild.gen.makelist_parser2 as mp
import jbuild.gen.node as node
import jbuild.commands as commands

import itertools


def parse_words(words):
    kv = parse.extract_words(words, {consts.GENERATE_SCRIPT_OUT_FILE, consts.GENERATE_SCRIPT_TEMPLATE})
    ws = []
    for item in (consts.GENERATE_SCRIPT_OUT_FILE, consts.GENERATE_SCRIPT_TEMPLATE):
        for i, word in list(enumerate(kv[item])):
            if word == consts.GENERATE_SCRIPT_PROPERTY_SEP:
                ws += kv[item][i:]
                kv[item] = kv[item][:i]
    tepmlates = kv[consts.GENERATE_SCRIPT_TEMPLATE]
    outputs = kv[consts.GENERATE_SCRIPT_OUT_FILE]
    assert len(outputs) >= len(tepmlates)
    if ws and ws[0] != consts.GENERATE_SCRIPT_PROPERTY_SEP:
        raise mp.ParseError('''Can't parse {}'''.format(ws))
    custom_props = []
    for item in ws:
        if item == consts.GENERATE_SCRIPT_PROPERTY_SEP:
            custom_props.append([])
        else:
            custom_props[-1].append(item)
    for o, t in itertools.izip_longest(outputs, tepmlates):
        yield o, t, custom_props


def generate_scripts(path, target, ctx):
    jdk_resource = base.resolve_jdk(
        ctx.global_resources, prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'), opts=ctx.opts
    )

    def gen_node(out, tmpl, raw_props):
        props = dict()
        props['CLASSPATH'] = '::'.join([os.path.basename(i) for i in ctx.classpath(path)])
        props['JAR_NAME'] = props['CLASSPATH'].split('::')[0]
        props['TARGET_OS'] = ctx.target_platform or ''
        props['PATHSEP'] = ';' if ctx.target_platform == 'win32' else ':'
        props['IS_UBERJAR'] = 'yes' if target.plain.get(consts.UBERJAR) else 'no'
        props['PROJECT_DIR'] = path
        props['JAR_BASENAME'] = os.path.splitext(target.output_jar_name() or '')[0]
        for prop in raw_props:
            if not prop:
                raise mp.ParseError('Empty {}'.format(consts.GENERATE_SCRIPT_PROPERTY_SEP))
            prop_name, prop_val = prop[0], ' '.join(prop[1:])
            props[prop_name] = prop_val
        return node.JNode(
            path,
            [commands.run_gen_script(out, tmpl, props, jdk_resource)],
            node.files([tmpl]) if tmpl else [],
            node.files([out]),
            tag='RUN',
            res=path in ctx.rclosure,
            resources=['SCRIPTGEN'],
            fake_id=target.fake_id(),
        )

    user_scripts = target.plain.get(consts.GENERATE_SCRIPT, [])
    if user_scripts:
        for words in target.plain.get(consts.GENERATE_SCRIPT, []):
            for arg in parse_words(words):
                yield gen_node(*arg)
    elif consts.JAVA_PROGRAM in target.plain and not getattr(ctx.opts, 'disable_scriptgen', False):
        script_name = 'run.bat' if ctx.target_platform == 'win32' else 'run.sh'
        yield gen_node(script_name, None, [['GENERATE_DEFAULT_RUNNER']])
