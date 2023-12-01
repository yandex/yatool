from __future__ import print_function
import os
import logging
import tempfile

from exts.shlex2 import quote
from exts.process import run_process

logger = logging.getLogger(__name__)


class Command(object):
    def __init__(self, args, stdout=None, cwd=None, echo=True):
        self.args = args
        self.stdout = stdout
        self.cwd = cwd
        self.echo = echo

    def dump(self):
        cmdline = "\t"
        if not self.echo:
            cmdline += "@"
        if self.cwd:
            cmdline += "cd {} && ".format(self.cwd)
        cmdline += ' '.join(map(quote, self.args))
        if self.stdout:
            cmdline += " > " + self.stdout
        print(cmdline)


class Echo(Command):
    def __init__(self, text):
        super(Echo, self).__init__(["echo", text], echo=False)


class Rule(object):
    def __init__(self, targets, prerequisites=None, phony=False):
        self.targets = targets
        self.prerequisites = prerequisites or []
        self.recipe = []
        self.phony = phony

    def dump(self):
        # Split the rule in two if we have more than one target
        if len(self.targets) > 1:
            for target in self.targets[1:]:
                print("%s\\" % target)
            print("        ::\\")
            print("        %s\\" % self.targets[0])
            print("")

        print("%s\\" % self.targets[0])
        print("        ::\\")
        for req in self.prerequisites:
            print("        %s\\" % req)
        print("")
        for cmd in self.recipe:
            cmd.dump()

        if self.phony:
            print(".PHONY : {}".format(" ".join(self.targets)))


class Clean(Rule):
    def __init__(self, makefile):
        super(Clean, self).__init__(["clean"], phony=True)

        for rule in makefile.rules:
            if rule.phony:
                continue

            self.recipe.append(Command(["rm", "-f"] + rule.targets))


class Help(Rule):
    def __init__(self, makefile):
        super(Help, self).__init__(["help"], phony=True)

        self.recipe.append(Echo("The following are some of the valid targets for this Makefile:"))

        for rule in makefile.rules:
            if not rule.phony:
                continue

            self.recipe += [Echo("    {}".format(target)) for target in rule.targets]

        self.recipe.append(Echo(""))
        self.recipe.append(Echo("Used variables:"))

        for var in makefile.vars:
            text = "    {: <15} {}".format(var.name, var.description)
            if var.value:
                text += " (default: {})".format(var.value)
            self.recipe.append(Echo(text))


class Variable(object):
    def __init__(self, name, value=None, description=None):
        self.name = name
        self.value = value
        self.description = description


class Dependency(object):
    def __init__(self, var):
        self.var = var
        self.path = None
        self.type = None
        self.version = []
        self.default = None

    def detect(self):
        pass

    def preprocess(self, makefile):
        def replace_path(arg):
            if arg == self.path:
                return "$({})".format(self.var)
            return arg

        for rule in makefile.rules:
            for cmd in rule.recipe:
                cmd.args = list(map(replace_path, cmd.args))

        makefile.vars.append(
            Variable(self.var, self.default, "Path to {} {}".format(self.type, ".".join(map(str, self.version))))
        )

    def dump(self):
        pass


class Compiler(Dependency):
    def __init__(self, var):
        super(Compiler, self).__init__(var)
        self.vars = []

    @staticmethod
    def get_vars(compiler, names):
        PREFIX = '__YA_'
        source = ['{}{name}={name}\n'.format(PREFIX, name=n) for n in names]
        fd, path = tempfile.mkstemp(suffix='.h')
        try:
            with os.fdopen(fd, 'w') as f:
                f.writelines(source)
            output = run_process(compiler, args=['-E', path])
        finally:
            os.remove(path)

        values = {}
        for line in output.split('\n'):
            parts = line.split('=', 1)
            if len(parts) == 2 and parts[0].startswith(PREFIX):
                name, value = parts[0][len(PREFIX) :], parts[1]
                if value != name:
                    values[name] = value

        return values

    def detect(self):
        self.path = os.environ.get(self.var)
        if not self.path:
            return

        compilers = [
            {'type': 'clang', 'vars': ['__clang_major__', '__clang_minor__']},
            {'type': 'gcc', 'vars': ['__GNUC__', '__GNUC_MINOR__']},
        ]

        values = Compiler.get_vars(self.path, [v for c in compilers for v in c['vars']])

        for compiler in compilers:
            if compiler['vars'][0] not in values:
                continue
            self.type = compiler['type']
            self.vars = compiler['vars']
            self.version = [values[n] for n in compiler['vars']]
            break

        if self.type:
            logger.info("Detected {} {}".format(self.type, ".".join(self.version)))

    def dump(self):
        if not self.type:
            return

        print("ifneq ($(MAKECMDGOALS),help)")  # Do not check for help target
        print("define _{}_TEST".format(self.var))
        print(" ".join(self.vars))
        print("endef")
        print("")
        print("_{var}_VERSION = $(shell echo '$(_{var}_TEST)' | $({var}) -E -P -)".format(var=self.var))
        print("$(info _{var}_VERSION = '$(_{var}_VERSION)')".format(var=self.var))
        print("")
        print("ifneq '$(_{}_VERSION)' '{}'".format(self.var, " ".join(self.version)))
        print("    $(error {} {} is required)".format(self.type, ".".join(self.version)))
        print("endif")
        print("endif")


class Python(Dependency):
    def __init__(self, version):
        super(Python, self).__init__("PYTHON")
        self.path = "$(PYTHON)/python"
        self.type = "Python"
        self.version = version
        self.default = "$(shell which python)"


class Makefile(object):
    def __init__(self, builtin=True, oneshell=False):
        self.builtin = builtin
        self.oneshell = oneshell
        self.vars = []
        self.deps = []
        self.rules = []

    def preprocess(self):
        for dep in self.deps:
            dep.detect()
            dep.preprocess(self)

        self.rules.append(Clean(self))
        self.rules.append(Help(self))

    def dump(self):
        if not self.builtin:
            print("MAKEFLAGS += --no-builtin-rules")

        if self.oneshell:
            print(".ONESHELL:")

        for var in self.vars:
            if not var.value:
                continue
            print("{} = {}".format(var.name, var.value))

        for dep in self.deps:
            dep.dump()

        for rule in self.rules:
            rule.dump()


class MakefileGenerator(object):
    def gen_makefile(self, graph):
        logger.info("Generating makefile...")

        makefile = Makefile(builtin=False, oneshell=True)
        makefile.vars.extend(
            [
                Variable("BUILD_ROOT", "$(shell pwd)", "Path to the build directory"),
                Variable("SOURCE_ROOT", "$(shell pwd)", "Path to the source directory"),
            ]
        )

        makefile.deps.extend([Compiler('CC'), Compiler('CXX'), Python([2, 7])])

        all = Rule(["all"], phony=True)
        makefile.rules.append(all)

        for node in graph["graph"]:
            rule = Rule(node["outputs"], node["inputs"])

            # Create directories for outputs
            output_dirs = set()
            for output in node["outputs"]:
                output_dirs.add(os.path.dirname(output))

            for output_dir in output_dirs:
                rule.recipe.append(Command(["mkdir", "-p", output_dir]))

            for cmd in node["cmds"]:
                rule.recipe.append(Command(cmd["cmd_args"], stdout=cmd.get("stdout"), cwd=cmd.get("cwd")))

            if node["uid"] in graph["result"]:
                all.prerequisites += node["outputs"]

            makefile.rules.append(rule)

        makefile.preprocess()
        makefile.dump()
