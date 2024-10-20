#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/config/config.h> // for GetOrInit()
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

    class TTool: public TBasicModImpl {
    public:
        TTool(): TBasicModImpl({.Id = EMacroFunction::Tool, .Name = "tool", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            auto names = SplitArgs(TString(arg0));
            if (names.size() == 1) {
                // one does not simply reuse the original argument,
                // for it might have been transformed (e.g., dequoted)
                auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(names.front())));
                return TMacroValues::TTool {.Data = pooledName};
            }
            throw std::runtime_error{"Tool arrays are not supported"};
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TInput: public TBasicModImpl {
    public:
        TInput(): TBasicModImpl({.Id = EMacroFunction::Input, .Name = "input", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
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
            if (auto glob = std::get_if<TMacroValues::TGlobPattern>(&args[0])) {
                return processInput(glob->Data, true);
            }
            auto arg0 = std::get<std::string_view>(args[0]);
            return processInput(arg0, false);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TOutput: public TBasicModImpl {
    public:
        TOutput(): TBasicModImpl({.Id = EMacroFunction::Output, .Name = "output", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TOutput(TModMetadata metadata): TBasicModImpl(metadata) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            auto names = SplitArgs(TString(arg0));
            if (names.size() == 1) {
                // one does not simply reuse the original argument,
                // for it might have been transformed (e.g., dequoted)
                auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(names.front())));
                auto result = TMacroValues::TOutput {.Coord = CollectCoord(pooledName, ctx.Sink.Outputs)};
                if (Id == EMacroFunction::Tmp)
                    UpdateCoord(ctx.Sink.Outputs, result.Coord, [](auto& x) {x.IsTmp = true;});
                return result;
            }
            throw std::runtime_error{"Output arrays are not supported"};
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TTmp: public TOutput {
    public:
        TTmp(): TOutput({.Id = EMacroFunction::Tmp, .Name = "tmp", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TGlob: public TBasicModImpl {
    public:
        TGlob(): TBasicModImpl({.Id = EMacroFunction::Glob, .Name = "glob", .Arity = 1, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            return TMacroValues::TGlobPattern{.Data = arg0};
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TLateOut: public TBasicModImpl {
    public:
        TLateOut(): TBasicModImpl({.Id = EMacroFunction::LateOut, .Name = "late_out", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
                },
                [&](const TVector<TString>& v) {
                    for (auto& s : v)
                        GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TContext: public TBasicModImpl {
    public:
        TContext(): TBasicModImpl({.Id = EMacroFunction::Context, .Name = "context", .Arity = 2, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            auto context = TFileConf::GetContextType(arg0);
            if (auto arg1 = std::get_if<TMacroValues::TInputs>(&args[1])) {
                for (auto& coord : arg1->Coords)
                    UpdateCoord(ctx.Sink.Inputs, coord, [=](auto& var) {
                        var.Context = context;
                    });
                return *arg1;
            }
            if (auto arg1 = std::get_if<TMacroValues::TInput>(&args[1])) {
                UpdateCoord(ctx.Sink.Inputs, arg1->Coord, [=](auto& var) {
                    var.Context = context;
                });
                return *arg1;
            }
            throw TConfigurationError() << "Modifier [[bad]]" << ToString(Id) << "[[rst]] must be applied to a valid input sequence";
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TOutputFlagger: public TBasicModImpl {
    public:
        TOutputFlagger(TModMetadata metadata): TBasicModImpl(metadata) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get_if<TMacroValues::TOutput>(&args[0]);
            if (!arg0)
                throw TConfigurationError() << "Modifier [[bad]]" << ToString(Id) << "[[rst]] must be applied to a valid output";
            UpdateCoord(ctx.Sink.Outputs, arg0->Coord, [&](auto& var) {Do(var);});
            return *arg0;
        }
    protected:
        virtual void Do(TCompiledCommand::TOutput& output) const = 0;
    };

    class TNoAutoSrc: public TOutputFlagger {
    public:
        TNoAutoSrc(): TOutputFlagger({.Id = EMacroFunction::NoAutoSrc, .Name = "noauto", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.NoAutoSrc = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TNoRel: public TOutputFlagger {
    public:
        TNoRel(): TOutputFlagger({.Id = EMacroFunction::NoRel, .Name = "norel", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.NoRel = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TToBinDir: public TOutputFlagger {
    public:
        TToBinDir(): TOutputFlagger({.Id = EMacroFunction::ResolveToBinDir, .Name = "tobindir", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.ResolveToBinDir = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
