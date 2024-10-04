#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    template<typename TLinks>
    ui32 CollectCoord(TStringBuf s, TLinks& links) {
        return links.Push(s).first + links.Base;
    }

    template<typename TLinks, typename FUpdater>
    void UpdateCoord(TLinks& links, ui32 coord, FUpdater upd) {
        links.Update(coord - links.Base, upd);
    }

    //
    //
    //

    class TInput: public NCommands::TBasicModImpl {
    public:
        TInput(): TBasicModImpl({.Id = EMacroFunction::Input, .Name = "input", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& unwrappedArgs
        ) const override {
            CheckArgCount(unwrappedArgs);
            auto processInput = [&ctx](std::string_view arg0, bool isGlob) -> TMacroValues::TValue {
                auto names = SplitArgs(TString(arg0));
                if (names.size() == 1) {
                    // one does not simply reuse the original argument,
                    // for it might have been transformed (e.g., dequoted)
                    auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(names.front())));
                    auto input = TMacroValues::TInput {.Coord = CollectCoord(pooledName, ctx.Sink.Inputs)};
                    UpdateCoord(ctx.Sink.Inputs, input.Coord, [&isGlob](auto& var) { var.IsGlob = isGlob; });
                    return input;
                }
                auto result = TMacroValues::TInputs();
                for (auto& name : names) {
                    auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
                    result.Coords.push_back(CollectCoord(pooledName, ctx.Sink.Inputs));
                    UpdateCoord(ctx.Sink.Inputs, result.Coords.back(), [&isGlob](auto& var) { var.IsGlob = isGlob; });
                }
                return result;
            };
            if (auto glob = std::get_if<TMacroValues::TGlobPattern>(&unwrappedArgs[0])) {
                return processInput(glob->Data, true);
            }
            auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
            return processInput(arg0, false);
        }
    } Input;

}
