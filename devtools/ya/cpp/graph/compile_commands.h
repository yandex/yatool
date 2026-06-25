#pragma once

#include "graph.h"

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>

namespace NYa::NGraph {

    // Map from $(PATTERN_NAME) variable names to their resolved filesystem paths.
    // Keys are the raw pattern strings without the $(...) wrapper,
    // e.g. "SOURCE_ROOT", "BUILD_ROOT", "TOOL_ROOT", or a resource pattern.
    using TPatternMap = THashMap<TString, TString>;

    struct TCompileCommand {
        TString Command;
        TString Directory;
        TString File;

        bool operator<(const TCompileCommand& other) const {
            if (File != other.File) {
                return File < other.File;
            }
            return Command < other.Command;
        }
    };

    struct TExtractOptions {
        // If non-empty, only emit entries whose resolved file path starts with one of these.
        TVector<TString> FilePrefixes;
        // If true, skip files that live under $(BUILD_ROOT) (generated files).
        bool SkipGenerated = false;
        // If true, keep the full resolved compiler path; otherwise strip to basename.
        bool DontStripCompilerPath = false;
        // Extra arguments appended verbatim (shell-quoted) to every command string.
        TVector<TString> ExtraArgs;
    };

    // Build the variable→path substitution table from the graph conf resources.
    //
    // Always adds:
    //   SOURCE_ROOT → sourceRoot
    //   BUILD_ROOT  → buildRoot (or sourceRoot if buildRoot is empty)
    //   TOOL_ROOT   → toolRoot
    //
    // For each conf resource:
    //   - TGlobalSingleResource  → toolRoot + "/" + sbr_id(Resource)
    //   - TGlobalResourceBundle  → pick entry matching `platform` (case-insensitive),
    //                              then toolRoot + "/" + sbr_id(chosen Resource)
    //   - base64 / non-sbr URIs are silently skipped.
    //
    // `platform` should be one of: "linux", "darwin", "win32".
    TPatternMap BuildPatterns(
        const TGraph& graph,
        const TString& sourceRoot,
        const TString& buildRoot,
        const TString& toolRoot,
        const TString& platform);

    // Extract compile commands from the build graph.
    // Only CC/CU nodes with exactly one .o output and one command are emitted.
    // The result is sorted by (file, command).
    TVector<TCompileCommand> ExtractCompileCommands(
        const TGraph& graph,
        const TPatternMap& patterns,
        const TString& directory,
        const TExtractOptions& opts);

    // Serialize compile commands as a JSON array to `out`.
    // Format: 4-space indent, keys sorted (command, directory, file).
    void WriteCompileCommands(
        IOutputStream& out,
        const TVector<TCompileCommand>& commands);

    // Update-mode write: load existing compile_commands.json from `existingFile`
    // (OSError is silently ignored when the file does not exist), merge with `newCmds`
    // (new entries win on file conflicts), sort, and write to `targetFile`.
    void MergeAndWriteCompileCommands(
        const TVector<TCompileCommand>& newCmds,
        const TString& existingFile,
        const TString& targetFile);

}  // namespace NYa::NGraph
