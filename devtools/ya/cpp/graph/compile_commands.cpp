#include "compile_commands.h"

#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_value.h>

#include <util/generic/hash_set.h>
#include <util/stream/buffered.h>
#include <util/stream/file.h>
#include <util/stream/mem.h>
#include <util/string/ascii.h>

#include <algorithm>
#include <variant>

namespace NYa::NGraph {

    namespace {

        // ---------------------------------------------------------------------------
        // Resource resolution helpers (ported from devtools/ya/ccj/resolver.cpp)
        // ---------------------------------------------------------------------------

        TString SbrId(const TStringBuf& uri) {
            constexpr TStringBuf prefix = "sbr:";
            if (uri.StartsWith(prefix)) {
                return TString(uri.substr(prefix.size()));
            }
            return {};
        }

        TString ResolveUri(const TStringBuf& uri, const TString& toolRoot) {
            const TString id = SbrId(uri);
            if (id.empty()) {
                return {};
            }
            return toolRoot + "/" + id;
        }

        TString SelectPlatformResource(
            const TVector<TGlobalResourceBundleItem>& bundle,
            const TString& platform)
        {
            const TString lowerPlatform = to_lower(platform);

            // Pass 1: exact case-insensitive match.
            for (const auto& item : bundle) {
                if (to_lower(TString(item.Platform)) == lowerPlatform) {
                    return TString(item.Resource);
                }
            }
            // Pass 2: bundle entry starts with our platform string (e.g. "linux" not "linux-aarch64").
            for (const auto& item : bundle) {
                const TString lowerEntry = to_lower(TString(item.Platform));
                if (lowerEntry.StartsWith(lowerPlatform) &&
                    (lowerEntry.size() == lowerPlatform.size() ||
                     lowerEntry[lowerPlatform.size()] == '-'))
                {
                    return TString(item.Resource);
                }
            }
            // Pass 3: platform name appears anywhere in the entry (fallback).
            for (const auto& item : bundle) {
                if (to_lower(TString(item.Platform)).Contains(lowerPlatform)) {
                    return TString(item.Resource);
                }
            }
            return {};
        }

        // ---------------------------------------------------------------------------
        // Variable substitution (ported from devtools/ya/ccj/resolver.cpp)
        // ---------------------------------------------------------------------------

        TString Resolve(TStringBuf s, const TPatternMap& patterns) {
            const size_t firstDollar = s.find("$(");
            if (firstDollar == TStringBuf::npos) {
                return TString(s);
            }

            TString result;
            result.reserve(s.size() + 64);

            size_t pos = 0;
            while (pos < s.size()) {
                const size_t dollarPos = s.find("$(", pos);
                if (dollarPos == TStringBuf::npos) {
                    result.append(s.data() + pos, s.size() - pos);
                    break;
                }
                if (dollarPos > pos) {
                    result.append(s.data() + pos, dollarPos - pos);
                }
                const size_t closePos = s.find(')', dollarPos + 2);
                if (closePos == TStringBuf::npos) {
                    result.append(s.data() + dollarPos, s.size() - dollarPos);
                    break;
                }
                const TStringBuf key(s.data() + dollarPos + 2, closePos - dollarPos - 2);
                const auto it = patterns.find(key);
                if (it != patterns.end()) {
                    result.append(it->second);
                } else {
                    result.append(s.data() + dollarPos, closePos - dollarPos + 1);
                }
                pos = closePos + 1;
            }
            return result;
        }

        // ---------------------------------------------------------------------------
        // JSON string writer (ported from devtools/ya/ccj/writer.cpp)
        // ---------------------------------------------------------------------------

        void WriteJsonString(IOutputStream& out, TStringBuf s) {
            static const char* HEX = "0123456789abcdef";
            out.Write('"');
            const char* p = s.data();
            const char* end = p + s.size();
            const char* chunkStart = p;
            while (p < end) {
                const unsigned char c = static_cast<unsigned char>(*p);
                char esc = 0;
                bool needsUEscape = false;
                switch (c) {
                    case '"':  esc = '"';  break;
                    case '\\': esc = '\\'; break;
                    case '\n': esc = 'n';  break;
                    case '\r': esc = 'r';  break;
                    case '\t': esc = 't';  break;
                    case '\b': esc = 'b';  break;
                    case '\f': esc = 'f';  break;
                    default:
                        if (c < 0x20) {
                            needsUEscape = true;
                        }
                        break;
                }
                if (esc || needsUEscape) {
                    if (p > chunkStart) {
                        out.Write(chunkStart, p - chunkStart);
                    }
                    if (esc) {
                        char buf[2] = {'\\', esc};
                        out.Write(buf, 2);
                    } else {
                        char buf[6] = {'\\', 'u', '0', '0',
                                       HEX[(c >> 4) & 0xf], HEX[c & 0xf]};
                        out.Write(buf, 6);
                    }
                    chunkStart = p + 1;
                }
                ++p;
            }
            if (p > chunkStart) {
                out.Write(chunkStart, p - chunkStart);
            }
            out.Write('"');
        }

        // ---------------------------------------------------------------------------
        // Argument quoting (ported from devtools/ya/ccj/writer.cpp)
        // ---------------------------------------------------------------------------

        TString QuoteSingleArg(TStringBuf arg) {
            const bool needQuote = arg.empty() ||
                                   arg.find(' ')  != TStringBuf::npos ||
                                   arg.find('\t') != TStringBuf::npos;

            TString result;
            result.reserve(arg.size() + 4);

            auto appendBackslashes = [&](size_t n) {
                for (size_t i = 0; i < n; ++i) { result += '\\'; }
            };

            if (!needQuote) {
                size_t numBs = 0;
                for (char c : arg) {
                    if (c == '\\') {
                        ++numBs;
                    } else if (c == '"') {
                        appendBackslashes(numBs * 2);
                        numBs = 0;
                        result += '\\';
                        result += '"';
                    } else {
                        appendBackslashes(numBs);
                        numBs = 0;
                        result += c;
                    }
                }
                appendBackslashes(numBs);
            } else {
                result += '"';
                size_t numBs = 0;
                for (char c : arg) {
                    if (c == '\\') {
                        ++numBs;
                    } else if (c == '"') {
                        appendBackslashes(numBs * 2);
                        numBs = 0;
                        result += '\\';
                        result += '"';
                    } else {
                        appendBackslashes(numBs);
                        numBs = 0;
                        result += c;
                    }
                }
                appendBackslashes(numBs * 2);
                result += '"';
            }
            return result;
        }

        // ---------------------------------------------------------------------------
        // Source file extension check
        // ---------------------------------------------------------------------------

        constexpr TStringBuf SOURCE_EXTS[] = {
            ".cpp", ".c", ".cc", ".cxx", ".cu", ".m", ".mm",
        };

        bool IsSourceFile(TStringBuf path) {
            for (const auto ext : SOURCE_EXTS) {
                if (path.EndsWith(ext)) {
                    return true;
                }
            }
            return false;
        }

        bool HasPrefix(const TString& path, const TVector<TString>& prefixes) {
            if (prefixes.empty()) {
                return true;
            }
            for (const auto& prefix : prefixes) {
                if (path.StartsWith(prefix)) {
                    return true;
                }
            }
            return false;
        }

        // Write a string literal of known compile-time length without a terminating NUL.
        template <size_t N>
        static void WriteLit(IOutputStream& out, const char (&lit)[N]) {
            out.Write(lit, N - 1);
        }

    }  // namespace

    // ---------------------------------------------------------------------------
    // BuildPatterns
    // ---------------------------------------------------------------------------

    TPatternMap BuildPatterns(
        const TGraph& graph,
        const TString& sourceRoot,
        const TString& buildRoot,
        const TString& toolRoot,
        const TString& platform)
    {
        TPatternMap patterns;
        patterns["SOURCE_ROOT"] = sourceRoot;
        patterns["BUILD_ROOT"]  = buildRoot.empty() ? sourceRoot : buildRoot;
        patterns["TOOL_ROOT"]   = toolRoot;

        for (const auto& res : graph.Conf.Resources) {
            TString uri;
            std::visit([&](const auto& info) {
                using T = std::decay_t<decltype(info)>;
                if constexpr (std::is_same_v<T, TGlobalSingleResource>) {
                    uri = TString(info.Resource);
                } else if constexpr (std::is_same_v<T, TGlobalResourceBundle>) {
                    uri = SelectPlatformResource(info.Resources, platform);
                }
            }, res.ResourceInfo);

            const TString path = ResolveUri(uri, toolRoot);
            if (!path.empty()) {
                patterns[TString(res.Pattern)] = path;
            }
        }

        return patterns;
    }

    // ---------------------------------------------------------------------------
    // ExtractCompileCommands
    // ---------------------------------------------------------------------------

    TVector<TCompileCommand> ExtractCompileCommands(
        const TGraph& graph,
        const TPatternMap& patterns,
        const TString& directory,
        const TExtractOptions& opts)
    {
        constexpr TStringBuf BUILD_ROOT_PLACEHOLDER = "$(BUILD_ROOT)";
        constexpr TStringBuf WRAPCC_END = "--wrapcc-end";

        TVector<TCompileCommand> result;

        // Memoize resolved+quoted forms: same raw arg string → same output.
        struct TArgEntry {
            TString Resolved;
            TString Quoted;
        };
        THashMap<TStringBuf, TArgEntry> resolveCache;
        resolveCache.reserve(65536);

        auto resolveArg = [&](TStringBuf raw) -> const TArgEntry& {
            auto it = resolveCache.find(raw);
            if (it != resolveCache.end()) {
                return it->second;
            }
            TArgEntry entry;
            entry.Resolved = Resolve(raw, patterns);
            entry.Quoted   = QuoteSingleArg(entry.Resolved);
            return resolveCache.emplace(raw, std::move(entry)).first->second;
        };

        // Pre-quote extra args once — the same suffix is appended to every command.
        TVector<TString> quotedExtraArgs;
        quotedExtraArgs.reserve(opts.ExtraArgs.size());
        size_t extraArgsLen = 0;
        for (const auto& extra : opts.ExtraArgs) {
            quotedExtraArgs.push_back(QuoteSingleArg(extra));
            extraArgsLen += 1 + quotedExtraArgs.back().size(); // leading space + quoted arg
        }

        for (const TNodePtr& nodePtr : graph.Graph) {
            const TNode& node = *nodePtr;

            // Only CC/CU compilation nodes.
            const auto kvpIt = node.Kv.find(TGraphString{"p"});
            if (kvpIt == node.Kv.end()) {
                continue;
            }
            const TString kvp = kvpIt->second.GetStringSafe(TString{});
            if (kvp != "CC" && kvp != "CU") {
                continue;
            }

            // Exactly one output ending in .o / .obj.
            if (node.Outputs.size() != 1) {
                continue;
            }
            if (!TStringBuf(node.Outputs[0]).EndsWith(".o") && !TStringBuf(node.Outputs[0]).EndsWith(".obj")) {
                continue;
            }

            // Exactly one command.
            if (node.Cmds.size() != 1) {
                continue;
            }
            const TVector<TGraphString>& rawArgs = node.Cmds[0].CmdArgs;
            if (rawArgs.empty()) {
                continue;
            }

            // Find the unique source file: an input with a recognised extension
            // that also appears as a standalone argument in cmd_args.
            const TGraphString* rawSourceFile = nullptr;
            for (const auto& inp : node.Inputs) {
                if (!IsSourceFile(TStringBuf(inp))) {
                    continue;
                }
                for (auto it = rawArgs.rbegin(); it != rawArgs.rend(); ++it) {
                    if (*it == inp) {
                        rawSourceFile = &inp;
                        break;
                    }
                }
                if (rawSourceFile) {
                    break;
                }
            }
            if (!rawSourceFile) {
                continue;
            }

            // Count source-file inputs that appear in cmd_args to ensure uniqueness.
            int srcCountInCmd = 0;
            for (const auto& inp : node.Inputs) {
                if (!IsSourceFile(TStringBuf(inp))) {
                    continue;
                }
                for (auto it = rawArgs.rbegin(); it != rawArgs.rend(); ++it) {
                    if (*it == inp) {
                        ++srcCountInCmd;
                        break;
                    }
                }
            }
            if (srcCountInCmd != 1) {
                continue;
            }

            // Strip wrapcc prefix.
            size_t argsBegin = 0;
            for (size_t i = 0; i < rawArgs.size(); ++i) {
                if (TStringBuf(rawArgs[i]) == WRAPCC_END) {
                    argsBegin = i + 1;
                    break;
                }
            }
            if (argsBegin >= rawArgs.size()) {
                continue;
            }

            const TArgEntry& fileEntry = resolveArg(TStringBuf(*rawSourceFile));
            const TString& resolvedFile = fileEntry.Resolved;

            // --no-generated: skip files from BUILD_ROOT.
            if (opts.SkipGenerated && TStringBuf(*rawSourceFile).StartsWith(BUILD_ROOT_PLACEHOLDER)) {
                continue;
            }

            // --files-in: prefix filter after resolution.
            if (!HasPrefix(resolvedFile, opts.FilePrefixes)) {
                continue;
            }

            // Build the command string.
            TString compilerQuoted;
            size_t totalLen = rawArgs.size() - argsBegin - 1 + extraArgsLen; // spaces between graph args + extra args
            for (size_t i = argsBegin; i < rawArgs.size(); ++i) {
                if (i == argsBegin && !opts.DontStripCompilerPath) {
                    const TArgEntry& e = resolveArg(TStringBuf(rawArgs[i]));
                    const TStringBuf resolved = e.Resolved;
                    const auto slashPos = resolved.rfind('/');
                    const TStringBuf basename = (slashPos != TStringBuf::npos)
                        ? resolved.SubStr(slashPos + 1)
                        : resolved;
                    compilerQuoted = QuoteSingleArg(basename);
                    totalLen += compilerQuoted.size();
                } else {
                    totalLen += resolveArg(TStringBuf(rawArgs[i])).Quoted.size();
                }
            }

            TString command;
            command.reserve(totalLen);
            for (size_t i = argsBegin; i < rawArgs.size(); ++i) {
                if (i > argsBegin) {
                    command += ' ';
                }
                if (i == argsBegin && !opts.DontStripCompilerPath) {
                    command.append(compilerQuoted);
                } else {
                    command.append(resolveArg(TStringBuf(rawArgs[i])).Quoted);
                }
            }
            for (const auto& q : quotedExtraArgs) {
                command += ' ';
                command.append(q);
            }

            TCompileCommand cmd;
            cmd.Command   = std::move(command);
            cmd.Directory = directory;
            cmd.File      = resolvedFile;
            result.push_back(std::move(cmd));
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    // ---------------------------------------------------------------------------
    // WriteCompileCommands
    // ---------------------------------------------------------------------------

    void WriteCompileCommands(
        IOutputStream& out,
        const TVector<TCompileCommand>& commands)
    {
        // Wrap in a 4 MiB buffer to reduce syscall count.
        TBufferedOutput buf(&out, 4 * 1024 * 1024);

        WriteLit(buf, "[\n");
        for (size_t i = 0; i < commands.size(); ++i) {
            const auto& cmd = commands[i];
            WriteLit(buf, "    {\n        \"command\": ");
            WriteJsonString(buf, cmd.Command);
            WriteLit(buf, ",\n        \"directory\": ");
            WriteJsonString(buf, cmd.Directory);
            WriteLit(buf, ",\n        \"file\": ");
            WriteJsonString(buf, cmd.File);
            WriteLit(buf, "\n    }");
            if (i + 1 < commands.size()) {
                WriteLit(buf, ",\n");
            } else {
                buf.Write('\n');
            }
        }
        buf.Write(']');
    }

    // ---------------------------------------------------------------------------
    // MergeAndWriteCompileCommands
    // ---------------------------------------------------------------------------

    void MergeAndWriteCompileCommands(
        const TVector<TCompileCommand>& newCmds,
        const TString& existingFile,
        const TString& targetFile)
    {
        // Collect file paths covered by the new result.
        THashSet<TString> newFiles;
        newFiles.reserve(newCmds.size());
        for (const auto& cmd : newCmds) {
            newFiles.insert(cmd.File);
        }

        TVector<TCompileCommand> merged = newCmds;

        // Try to load the existing JSON database.
        try {
            TFileInput in(existingFile);
            NJson::TJsonValue root;
            NJson::ReadJsonTree(&in, &root, /*throwOnError=*/true);
            if (root.IsArray()) {
                for (const auto& entry : root.GetArray()) {
                    TString file;
                    if (!entry["file"].GetString(&file)) {
                        continue;
                    }
                    if (newFiles.contains(file)) {
                        continue;  // superseded by new result
                    }
                    TCompileCommand cmd;
                    cmd.File      = file;
                    cmd.Directory = entry["directory"].GetStringSafe(TString{});
                    cmd.Command   = entry["command"].GetStringSafe(TString{});
                    merged.push_back(std::move(cmd));
                }
            }
        } catch (...) {
            // File does not exist or is malformed — start fresh.
        }

        std::sort(merged.begin(), merged.end());

        TFileOutput out(targetFile);
        WriteCompileCommands(out, merged);
    }

}  // namespace NYa::NGraph
