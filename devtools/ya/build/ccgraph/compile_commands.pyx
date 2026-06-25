from libcpp cimport bool

from devtools.ya.build.ccgraph.ccgraph cimport Graph, TGraphPtr
from util.generic.string cimport TString
from util.generic.vector cimport TVector


cdef extern from "devtools/ya/cpp/graph/compile_commands.h" namespace "NYa::NGraph":
    cdef cppclass TPatternMap:
        pass

    cdef struct TExtractOptions:
        TVector[TString] FilePrefixes
        bool SkipGenerated
        bool DontStripCompilerPath
        TVector[TString] ExtraArgs

    cdef struct TCompileCommand:
        TString Command
        TString Directory
        TString File


cdef extern from "devtools/ya/build/ccgraph/compile_commands_adapter.h" namespace "NYa::NCCGraph" nogil:
    TPatternMap BuildPatterns(
        TGraphPtr graph,
        const TString& sourceRoot,
        const TString& buildRoot,
        const TString& toolRoot,
        const TString& platform,
    ) except+

    TVector[TCompileCommand] ExtractCompileCommands(
        TGraphPtr graph,
        const TPatternMap& patterns,
        const TString& directory,
        const TExtractOptions& opts,
    ) except+

    void WriteCompileCommandsToFile(
        const TVector[TCompileCommand]& commands,
        const TString& targetFile,
    ) except+

    void WriteCompileCommandsToStdout(
        const TVector[TCompileCommand]& commands,
    ) except+

    void MergeAndWriteCompileCommands(
        const TVector[TCompileCommand]& newCmds,
        const TString& existingFile,
        const TString& targetFile,
    ) except+


def dump_compile_commands(
    Graph graph,
    str source_root,
    str build_root,
    str tool_root,
    str platform,
    list file_prefixes,
    bool skip_generated,
    bool dont_strip_compiler_path,
    object target_file,
    bool update,
    list extra_args,
):
    """Extract compile commands from a C++ graph and write them to target_file.

    Args:
        graph: A ccgraph.Graph instance built from the ymake output.
        source_root: Absolute path to the Arcadia source root.
        build_root: Absolute path to the build root (use source_root if empty).
        tool_root: Absolute path to the tools cache root.
        platform: One of "linux", "darwin", "win32".
        file_prefixes: List of source-root-relative path prefixes to filter by.
        skip_generated: If True, omit entries for generated (BUILD_ROOT) files.
        dont_strip_compiler_path: If True, keep the full compiler path.
        target_file: Path to the output file, or None to write to stdout.
        update: If True and target_file is set, merge with an existing database.
        extra_args: Extra arguments appended verbatim to every command string.
    """
    cdef TString c_source_root = source_root.encode()
    cdef TString c_build_root = build_root.encode() if build_root else b""
    cdef TString c_tool_root = tool_root.encode()
    cdef TString c_platform = platform.encode()

    cdef TPatternMap patterns
    with nogil:
        patterns = BuildPatterns(
            graph.graph,
            c_source_root,
            c_build_root,
            c_tool_root,
            c_platform,
        )

    cdef TExtractOptions opts
    opts.SkipGenerated = skip_generated
    opts.DontStripCompilerPath = dont_strip_compiler_path
    for prefix in file_prefixes:
        opts.FilePrefixes.push_back(prefix.encode())
    for arg in extra_args:
        opts.ExtraArgs.push_back(arg.encode())

    cdef TVector[TCompileCommand] commands
    with nogil:
        commands = ExtractCompileCommands(graph.graph, patterns, c_source_root, opts)

    cdef TString c_target_file
    if target_file is None:
        with nogil:
            WriteCompileCommandsToStdout(commands)
    elif update:
        c_target_file = target_file.encode()
        with nogil:
            MergeAndWriteCompileCommands(commands, c_target_file, c_target_file)
    else:
        c_target_file = target_file.encode()
        with nogil:
            WriteCompileCommandsToFile(commands, c_target_file)
