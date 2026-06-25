#pragma once

// Thin adapter between the Python/Cython layer (which holds a TGraphPtr) and
// the pure-C++ compile-commands API (which operates on TGraph&).
//
// All functions here are inline so that no extra translation unit is needed.

#include <devtools/ya/cpp/graph/compile_commands.h>
#include <devtools/ya/cpp/graph/graph.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>

namespace NYa::NCCGraph {

    using NYa::NGraph::TGraphPtr;
    using NYa::NGraph::TExtractOptions;
    using NYa::NGraph::TCompileCommand;

    // Build the variable->path substitution table from graph conf resources.
    inline NYa::NGraph::TPatternMap BuildPatterns(
        TGraphPtr graph,
        const TString& sourceRoot,
        const TString& buildRoot,
        const TString& toolRoot,
        const TString& platform)
    {
        return NYa::NGraph::BuildPatterns(*graph, sourceRoot, buildRoot, toolRoot, platform);
    }

    // Extract compile commands from the graph.
    inline TVector<TCompileCommand> ExtractCompileCommands(
        TGraphPtr graph,
        const NYa::NGraph::TPatternMap& patterns,
        const TString& directory,
        const TExtractOptions& opts)
    {
        return NYa::NGraph::ExtractCompileCommands(*graph, patterns, directory, opts);
    }

    // Write compile commands to a file path.
    inline void WriteCompileCommandsToFile(
        const TVector<TCompileCommand>& commands,
        const TString& targetFile)
    {
        TFileOutput out(targetFile);
        NYa::NGraph::WriteCompileCommands(out, commands);
    }

    // Write compile commands to stdout.
    inline void WriteCompileCommandsToStdout(
        const TVector<TCompileCommand>& commands)
    {
        NYa::NGraph::WriteCompileCommands(Cout, commands);
    }

    // Merge new commands with an existing JSON file and write the result.
    inline void MergeAndWriteCompileCommands(
        const TVector<TCompileCommand>& newCmds,
        const TString& existingFile,
        const TString& targetFile)
    {
        NYa::NGraph::MergeAndWriteCompileCommands(newCmds, existingFile, targetFile);
    }

}  // namespace NYa::NCCGraph
