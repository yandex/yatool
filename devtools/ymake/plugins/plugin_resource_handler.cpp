#include "plugin_resource_handler.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/yndex/yndex.h>
#include <devtools/ymake/symbols/file_store.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/algorithm.h>
#include <util/generic/yexception.h>
#include <util/string/join.h>
#include <util/string/split.h>

namespace {
    TString PathId(TStringBuf path, TStringBuf salt) {
        const unsigned int lengthLimit = 26;
        TString pid = MD5::Calc(TString::Join(path, salt)).substr(0, lengthLimit);
        pid.to_lower(0, lengthLimit);
        return pid;
    }

    TString ListId(TVector<TStringBuf>&& list, TStringBuf salt) {
        Sort(list);
        TString strRepr = JoinSeq(",", list);
        return PathId(strRepr, salt);
    }

    template <typename TStringType>
    THashMap<TStringBuf, TStringBuf> PrepareInputs(const TVector<TStringType>& inputs, TVector<TString>& linksBuffer) {
        linksBuffer.reserve(inputs.size());
        Transform(inputs.begin(), inputs.end(), std::back_inserter(linksBuffer), [](TStringBuf in) {
            return TFileConf::ConstructLink(ELinkType::ELT_Text, NPath::ConstructPath(in));
        });
        THashMap<TStringBuf, TStringBuf> inputToLink;
        for (size_t inputNum = 0; inputNum < inputs.size(); inputNum++) {
            inputToLink[inputs[inputNum]] = linksBuffer[inputNum];
        }
        return inputToLink;
    };

    void ReplaceInputs(TVector<TStringBuf>& args, const THashMap<TStringBuf, TStringBuf>& inputsMap) {
        for (auto& arg : args) {
            const auto it = inputsMap.find(arg);
            if (it != inputsMap.end()) {
                arg = it->second;
            }
        }
    }
}

namespace NYMake {
    namespace NPlugins {
        static const TSourceLocation docLink = __LOCATION__;
        void TPluginResourceHandler::Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>*) {
            TVector<TString> outs, srcsGen, compressed, compressedInput, compressedOutput;
            TVector<TStringBuf> rawGen, rawInputs;
            TVector<TString> rawKeys;
            TVector<TString> compressedOuts;
            const unsigned int rootLength = 200;
            const unsigned int limit = 8000;
            unsigned int length = 0;
            TVector<TStringBuf>::const_iterator bucketStart = params.cbegin();
            TVector<TStringBuf>::const_iterator path = params.cend();
            bool useTextContext = false;
            bool dontCompress = false;

            while (true) {
                // Allow at most one of each in any order
                if (!useTextContext && bucketStart != params.cend() && *bucketStart == "DONT_PARSE"sv) {
                    useTextContext = true;
                    bucketStart++;
                    continue;
                }
                if (!dontCompress && bucketStart != params.cend() && *bucketStart == "DONT_COMPRESS"sv) {
                    dontCompress = true;
                    bucketStart++;
                    continue;
                }
                break;
            }

            dontCompress = dontCompress || unit.Enabled("ARCH_AARCH64"sv) || unit.Enabled("ARCH_ARM"sv) || unit.Enabled("ARCH_PPC64LE"sv);

            if (IsSemanticsRendering) {
                TVector<TStringBuf> inputs;
                TVector<TStringBuf> keys;
                TVector<TStringBuf> opts;
                TVector<TStringBuf> args;
                for (auto name = bucketStart; name != params.cend(); name++) {
                    if (path == params.end()) {
                        path = name;
                        continue;
                    }
                    if (*path == "-") {
                        opts.emplace_back(*path);
                        opts.emplace_back(*name);
                    } else {
                        inputs.emplace_back(*path);
                        keys.emplace_back(*name);
                    }
                    path = params.end();
                }

                if (!inputs.empty()) {
                    args.emplace_back("INPUTS");
                    args.insert(args.end(), inputs.begin(), inputs.end());
                    args.emplace_back("KEYS");
                    args.insert(args.end(), keys.begin(), keys.end());
                }
                if (!opts.empty()) {
                    args.emplace_back("OPTS");
                    args.insert(args.end(), opts.begin(), opts.end());
                }
                if (!args.empty()) {
                    unit.CallMacro("_RESOURCE_SEM", args);
                }
                return;
            }

            TStringBuf auxCppExt = unit.Get("SEPARATE_AUX_CPP") == "yes" ? ".auxcpp"sv : ".cpp"sv;
            TStringBuf moduleTag = unit.Get("MODULE_TAG"sv);

            auto handleBucket = [&]() {
                TString lid = ListId(TVector<TStringBuf>(bucketStart, path), moduleTag);

                if (compressed) {
                    TString fakeYasm = TString::Join("_", lid, ".yasm");
                    TVector<TStringBuf> cmdArgs({unit.Get(TStringBuf("_TOOL_RESCOMPRESSOR")), fakeYasm});
                    if (unit.Enabled("DARWIN"sv) || unit.Enabled("IOS"sv) || unit.Enabled("WINDOWS"sv) && unit.Enabled("ARCH_TYPE_32"sv)) {
                        cmdArgs.push_back(TStringBuf("--prefix"));
                    }
                    cmdArgs.push_back(TStringBuf("$YASM_DEBUG_INFO_DISABLE_CACHE__NO_UID__"));
                    cmdArgs.insert(cmdArgs.end(), compressed.begin(), compressed.end());
                    if (compressedInput) {
                        cmdArgs.push_back(TStringBuf("IN"));
                        cmdArgs.insert(cmdArgs.end(), compressedInput.begin(), compressedInput.end());
                    }

                    TVector<TString> inputsLinks;
                    if (useTextContext) {
                        auto inputsMap = PrepareInputs(compressedInput, inputsLinks);
                        ReplaceInputs(cmdArgs, inputsMap);
                    }

                    cmdArgs.push_back(TStringBuf("OUT_NOAUTO"));
                    cmdArgs.push_back(fakeYasm);
                    cmdArgs.insert(cmdArgs.end(), compressedOutput.begin(), compressedOutput.end());

                    unit.CallMacro(TStringBuf("RUN_PROGRAM"), cmdArgs);
                    compressedOuts.push_back(fakeYasm);
                }

                if (srcsGen) {
                    TString output = TString::Join(lid, auxCppExt);
                    TVector<TStringBuf> cmdArgs({unit.Get(TStringBuf("_TOOL_RORESCOMPILER")), output});
                    cmdArgs.insert(cmdArgs.end(), srcsGen.begin(), srcsGen.end());
                    cmdArgs.push_back(TStringBuf("OUT_NOAUTO"));
                    cmdArgs.push_back(output);
                    unit.CallMacro(TStringBuf("RUN_PROGRAM"), cmdArgs);
                    outs.push_back(output);
                }

                if (rawGen) {
                    TString output = TString::Join(lid, "_raw", auxCppExt);
                    TVector<TStringBuf> cmdArgs({unit.Get(TStringBuf("_TOOL_RESCOMPILER")), output});
                    cmdArgs.insert(cmdArgs.end(), rawGen.begin(), rawGen.end());
                    if (rawInputs) {
                        cmdArgs.push_back(TStringBuf("IN"));
                        cmdArgs.insert(cmdArgs.end(), rawInputs.begin(), rawInputs.end());
                    }
                    TVector<TString> inputsLinks;
                    if (useTextContext) {
                        auto inputsMap = PrepareInputs(rawInputs, inputsLinks);
                        ReplaceInputs(cmdArgs, inputsMap);
                    }
                    cmdArgs.push_back(TStringBuf("OUT_NOAUTO"));
                    cmdArgs.push_back(output);
                    unit.CallMacro(TStringBuf("RUN_PROGRAM"), cmdArgs);
                    unit.CallMacro(TStringBuf("SRCS"), {TStringBuf("GLOBAL"), output});
                }

                srcsGen.clear();
                rawGen.clear();
                rawInputs.clear();
                compressed.clear();
                compressedInput.clear();
                compressedOutput.clear();
                bucketStart = path;
                length = 0;
            };

            if (dontCompress) {
                rawKeys.reserve(params.size());
            }

            for (TVector<TStringBuf>::const_iterator name = bucketStart; name != params.cend(); name++) {
                if (path == params.end()) {
                    path = name;
                    continue;
                }

                const unsigned int additionalLength = rootLength + path->length() + name->length();

                if (length && additionalLength + length > limit) {
                    handleBucket();
                }

                length += additionalLength;

                if (dontCompress) {
                    rawGen.push_back(*path);

                    if (*path != "-") {
                        rawInputs.push_back(*path);

                        rawKeys.push_back(TString::Join("-", *name));
                        rawGen.push_back(rawKeys.back());
                    } else {
                        rawGen.push_back(*name);
                    }

                    path = params.cend();
                    continue;
                }

                TString lid = TString::Join("_", PathId(TString::Join(*path, *name, unit.UnitPath()), moduleTag));
                TString output = TString::Join(lid, ".rodata");

                if (*path == "-") {
                    TStringBuf splitedPath, splitedName;
                    name->Split('=', splitedName, splitedPath);
                    compressed.push_back("-");
                    compressed.push_back(TString(splitedPath));
                    compressed.push_back(TString(output));
                    srcsGen.push_back(TString::Join(lid, "=", splitedName));
                    if (splitedPath.empty() || splitedName.empty()) {
                        YConfErr(Syntax) << "Syntax error: empty KEY or VALUE in RESOURCE(- KEY=VALUE), guard with \"\"" << Endl;
                    }
                } else {
                    compressed.push_back(TString(*path));
                    compressed.push_back(output);
                    compressedInput.push_back(TString(*path));
                    compressedOutput.push_back(output);
                    srcsGen.push_back(TString::Join(lid, "=", *name));
                }

                path = params.cend();
            }

            if (path != params.cend()) {
                ythrow yexception() << "Error in RESOURCE plugin";
            }

            if (compressed || srcsGen || rawGen) {
                handleBucket();
            }

            if (outs) {
                if (outs.size() > 1) {
                    outs.insert(outs.begin(), TString::Join("join_", ListId(TVector<TStringBuf>(outs.begin(), outs.end()), moduleTag), auxCppExt));
                    unit.CallMacro(TStringBuf("JOIN_SRCS_GLOBAL"), TVector<TStringBuf>(outs.begin(), outs.end()));
                } else {
                    outs.insert(outs.begin(), "GLOBAL");
                    unit.CallMacro(TStringBuf("SRCS"), TVector<TStringBuf>(outs.begin(), outs.end()));
                }
            }
            if (compressedOuts) {
                if (compressedOuts.size() > 1) {
                    compressedOuts.insert(compressedOuts.begin(),
                                          TString::Join("join_", ListId(TVector<TStringBuf>(compressedOuts.begin(), compressedOuts.end()), moduleTag),
                                                        ".yasm"));
                    unit.CallMacro("FLAT_JOIN_SRCS_GLOBAL", TVector<TStringBuf>(compressedOuts.begin(), compressedOuts.end()));
                } else {
                    unit.CallMacro("SRCS", {*compressedOuts.begin()});
                }
            }
        }

        void TPluginResourceHandler::RegisterMacro() {
            TString docText = "@usage: RESOURCE([DONT_PARSE ][Src Key]* [- Key=Value]*)\n"
                              "Add data (resources, random files, strings) to the program)\n"
                              "The common usage is to place Src file into binary. The Key is used to access it using library/cpp/resource or library/cpp/resource/python.\n"
                              "Alternative syntax with '- Key=Value' allows placing Value string as resource data into binary and make it accessible by Key.\n"
                              "\n"
                              "This is a simpler but less flexible option than ARCHIVE(), because in the case of ARCHIVE(), you have to use the data explicitly,\n"
                              "and in the case of RESOURCE(), the data will fall through SRCS() or SRCS(GLOBAL) to binary linking.\n"
                              "\n"
                              "Use the DONT_PARSE parameter to explicitly mark all Src files as plain text files: they will not be parsed unless used elsewhere."
                              "\n"
                              "@example: https://wiki.yandex-team.ru/yatool/howtowriteyamakefiles/#a2ispolzujjtekomanduresource\n\n"
                              "@example:\n\n"
                              "\tLIBRARY()\n"
                              "\n"
                              "\tOWNER(user1)\n"
                              "\n"
                              "\tRESOURCE(\n"
                              "\tpath/to/file1 /key/in/program/1\n"
                              "\tpath/to/file2 /key2\n"
                              "\t)\n"
                              "\n"
                              "\tEND()\n";
            NYndex::TSourceRange range = {static_cast<size_t>(docLink.Line) + 1, 0, static_cast<size_t>(docLink.Line) + 1, 0};
            NYndex::TSourceLocation link(TString(docLink.File), range);
            NYndex::TDefinition definition("RESOURCE", docText, link, NYndex::EDefinitionType::Macro);
            GlobalConf()->CommandDefinitions.AddDefinition(definition);
            MacroFacade()->RegisterMacro("RESOURCE", new TPluginResourceHandler(GlobalConf()->RenderSemantics));
        }
    }
}
