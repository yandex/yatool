#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    class TCutExt: public NCommands::TBasicModImpl {
    public:
        TCutExt(): TBasicModImpl({.Id = EMacroFunction::CutExt, .Name = "noext", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& unwrappedArgs
        ) const override {
            CheckArgCount(unwrappedArgs);
            auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
            auto names = SplitArgs(TString(arg0));
            if (names.size() != 1) {
                throw std::runtime_error{"noext modifier requires a single argument"};
            }
            // one does not simply reuse the original argument,
            // for it might have been transformed (e.g., dequoted)
            arg0 = names.front();
            // cf. RenderCutExt()
            size_t slash = arg0.rfind(NPath::PATH_SEP); //todo: windows slash!
            if (slash == TString::npos)
                slash = 0;
            size_t dot = arg0.rfind('.');
            if (dot != TString::npos && dot >= slash)
                arg0 = arg0.substr(0, dot);
            auto id = ctx.Values.InsertStr(arg0);
            return ctx.Values.GetValue(id);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [](TString s) {
                // lifted from EMF_CutExt processing:
                size_t slash = s.rfind(NPath::PATH_SEP); //todo: windows slash!
                if (slash == TString::npos)
                    slash = 0;
                size_t dot = s.rfind('.');
                if (dot != TString::npos && dot >= slash)
                    s = s.substr(0, dot);
                return s;
            };
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return apply(std::move(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = apply(std::move(s));
                    return std::move(v);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } CutExt;

}
