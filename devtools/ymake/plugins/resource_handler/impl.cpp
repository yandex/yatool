#include "impl.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/yndex/yndex.h>
#include <devtools/ymake/symbols/file_store.h>

#include "yasm.h"
#include "compiler.h"
#include "objcopy.h"

namespace NYMake {
    namespace NPlugins {

        static const TSourceLocation docLink = __LOCATION__;
        void TPluginResourceHandler::Execute(TPluginUnit& unit, const TVector<TStringBuf>& params) {
            TVector<TStringBuf>::const_iterator bucketStart = params.cbegin();
            bool useTextContext = false;
            bool dontYasm = false;
            bool objCopy = unit.Enabled("YMAKE_USE_OBJCOPY");
            {
                auto end = params.cbegin() + std::min<size_t>(params.size(), 2ULL);
                useTextContext = std::find(params.cbegin(), end, "DONT_PARSE"sv) != end;
                dontYasm = std::find(params.cbegin(), end, "DONT_COMPRESS"sv) != end;
                bucketStart += int(useTextContext) + int(dontYasm);
            }
            dontYasm = dontYasm ||
                       unit.Enabled("ARCH_AARCH64"sv) ||
                       unit.Enabled("ARCH_ARM"sv) ||
                       unit.Enabled("ARCH_PPC64LE"sv) ||
                       unit.Enabled("ARCH_WASM32"sv) ||
                       unit.Enabled("ARCH_WASM64"sv);

            auto IterateOverResources = [&](auto handle) {
                auto it = bucketStart;
                while ((it + 1) < params.cend()) {
                    auto path = it;
                    auto name = (it + 1);
                    handle(path, name);
                    it += 2;
                }
                return it == params.cend() ? 0 : 1; // Non-zero exit status means an error
            };

            if (IsSemanticsRendering) {
                TVector<TStringBuf> inputs;
                TVector<TStringBuf> keys;
                TVector<TStringBuf> opts;
                TVector<TStringBuf> args;
                IterateOverResources([&](auto path, auto name) {
                    if (*path == "-") {
                        opts.emplace_back(*path);
                        opts.emplace_back(*name);
                    } else {
                        inputs.emplace_back(*path);
                        keys.emplace_back(*name);
                    }
                });

                append_if(!inputs.empty(), args, "INPUTS"sv, inputs, "KEYS"sv, keys);
                append_if(!opts.empty(), args, "OPTS"sv, opts);
                if (!args.empty()) {
                    unit.CallMacro("_RESOURCE_SEM", args);
                }
                return;
            }

            using namespace NYMake::NResourcePacker;

            std::unique_ptr<IResourcePacker> objCopyPacker = std::make_unique<TObjCopyResourcePacker>(unit, useTextContext);
            std::unique_ptr<IResourcePacker> rawPacker = std::make_unique<TRawResourcePacker>(unit, useTextContext);
            std::unique_ptr<IResourcePacker> yasmPacker = std::make_unique<TYASMResourcePacker>(unit, useTextContext);

            bool err = IterateOverResources([&](auto path, auto name) {
                if (objCopy && TObjCopyResourcePacker::CanHandle(*path, *name)) {
                    objCopyPacker->HandleResource(*path, *name);
                    return;
                }

                IResourcePacker* packer = dontYasm ? rawPacker.get() : yasmPacker.get();
                packer->HandleResource(*path, *name);
            });

            if (err) {
                ythrow yexception() << "Error in RESOURCE plugin";
            }

            objCopyPacker->Finalize(true /*force*/);
            rawPacker->Finalize(true /*force*/);
            yasmPacker->Finalize(true /*force*/);
        }

        void TPluginResourceHandler::RegisterMacro(TBuildConfiguration& conf) {
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
            auto macro = MakeSimpleShared<TPluginResourceHandler>(conf.RenderSemantics);
            macro->Definition = {
                std::move(docText),
                TString{docLink.File},
                static_cast<size_t>(docLink.Line) + 1,
                0,
                static_cast<size_t>(docLink.Line) + 1,
                0
            };
            conf.RegisterPluginMacro("RESOURCE", macro);
        }
    } // namespace NPlugins
} // namespace NYMake
