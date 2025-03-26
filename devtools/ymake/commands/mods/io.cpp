#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/config/config.h> // for GetOrInit()
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

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
            return std::visit(TOverloaded{
                [&](std::string_view name) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    if (names.empty())
                        return std::monostate();
                    else if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        throw std::runtime_error{"Tool arrays are not supported"};
                },
                [&](const std::vector<std::string_view>& names) -> TMacroValues::TValue {
                    if (names.empty())
                        return std::monostate();
                    else if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        throw std::runtime_error{"Tool arrays are not supported"};
                },
                [](auto&&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                },
            }, args[0]);
        }
    private:
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name) const {
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            return TMacroValues::TTool {.Data = pooledName};
        };
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
            return std::visit(TOverloaded{
                [&](std::string_view name) {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front(), false, false);
                    return ProcessMany(ctx, names, false, false);
                },
                [&](const std::vector<std::string_view>& names) {
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front(), false, false);
                    return ProcessMany(ctx, names, false, false);
                },
                [&](TMacroValues::TGlobPattern glob) {
                    if (glob.Data.size() == 1)
                        return ProcessOne(ctx, glob.Data.front(), true, false);
                    return ProcessMany(ctx, glob.Data, true, false);
                },
                [&](TMacroValues::TLegacyLateGlobPatterns glob) {
                    return ProcessMany(ctx, glob.Data, false, true);
                },
                [](auto&&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                },
            }, args[0]);
        }
    private:
        auto ProcessCoord(const TPreevalCtx& ctx, std::string_view name, bool isGlob, bool isLegacyGlob) const {
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            auto coord = ctx.Sink.Inputs.CollectCoord(pooledName);
            ctx.Sink.Inputs.UpdateCoord(coord, [=](auto& var) { var.IsGlob = isGlob; var.IsLegacyGlob = isLegacyGlob; });
            return coord;
        };
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name, bool isGlob, bool isLegacyGlob) const {
            return TMacroValues::TInput {.Coord = ProcessCoord(ctx, name, isGlob, isLegacyGlob)};
        };
        TMacroValues::TValue ProcessMany(const TPreevalCtx& ctx, auto& names, bool isGlob, bool isLegacyGlob) const {
            auto result = TMacroValues::TInputs();
            for (auto& name : names)
                result.Coords.push_back(ProcessCoord(ctx, name, isGlob, isLegacyGlob));
            std::sort(result.Coords.begin(), result.Coords.end());
            result.Coords.erase(std::unique(result.Coords.begin(), result.Coords.end()), result.Coords.end());
            return result;
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
            auto& nameArg = args[0];
            return std::visit(TOverloaded{
                [&](std::string_view name) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    return ProcessMany(ctx, names);
                },
                [&](const std::vector<std::string_view>& names) -> TMacroValues::TValue {
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    return ProcessMany(ctx, names);
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, nameArg);
        }
    private:
        ui32 ProcessCoord(const TPreevalCtx& ctx, std::string_view name) const {
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            auto result = ctx.Sink.Outputs.CollectCoord(pooledName);
            if (Y_UNLIKELY(Id == EMacroFunction::Tmp))
                ctx.Sink.Outputs.UpdateCoord(result, [](auto& x) {x.IsTmp = true;});
            return result;
        }
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name) const {
            return TMacroValues::TOutput {.Coord = ProcessCoord(ctx, name)};
        };
        TMacroValues::TValue ProcessMany(const TPreevalCtx& ctx, auto& names) const {
            auto result = TMacroValues::TOutputs();
            for (auto& name : names)
                result.Coords.push_back(ProcessCoord(ctx, name));
            return result;
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

    class TOutputInclude: public TBasicModImpl {
    public:
        TOutputInclude(): TBasicModImpl({.Id = EMacroFunction::OutputInclude, .Name = "output_include", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](std::string_view name) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    auto result = std::vector<std::string_view>();
                    result.reserve(names.size());
                    for (auto& name : names)
                        result.push_back(ProcessOne(ctx, name));
                    return result;
                },
                [&](std::vector<std::string_view> names) -> TMacroValues::TValue {
                    for (auto& name : names)
                        name = ProcessOne(ctx, name);
                    return std::move(names);
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[0]);
        }
    private:
        std::string_view ProcessOne(const TPreevalCtx& ctx, std::string_view name) const {
            // TODO we appear to always hide output_includes, so just drop it
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            ctx.Sink.OutputIncludes.CollectCoord(pooledName);
            return pooledName;
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
            TMacroValues::TGlobPattern result;
            std::visit(TOverloaded{
                [&](std::string_view glob) {
                    result.Data.push_back(TString(glob));
                },
                [&](const std::vector<std::string_view>& globs) {
                    result.Data.reserve(globs.size());
                    for (auto& glob : globs)
                        result.Data.push_back(TString(glob));
                },
                [](auto&) {
                    throw std::bad_variant_access();
                }
            }, args[0]);
            return result;
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
                    ctx.Sink.Inputs.UpdateCoord(coord, [=](auto& var) {
                        var.Context = context;
                    });
                return *arg1;
            }
            if (auto arg1 = std::get_if<TMacroValues::TInput>(&args[1])) {
                ctx.Sink.Inputs.UpdateCoord(arg1->Coord, [=](auto& var) {
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
            if (auto arg0 = std::get_if<TMacroValues::TOutput>(&args[0])) {
                ctx.Sink.Outputs.UpdateCoord(arg0->Coord, [&](auto& var) {Do(var);});
                return *arg0;
            }
            if (auto arg0 = std::get_if<TMacroValues::TOutputs>(&args[0])) {
                std::for_each(arg0->Coords.begin(), arg0->Coords.end(), [&](const auto coord) {
                    ctx.Sink.Outputs.UpdateCoord(coord, [&](auto& var) {Do(var);});
                });
                return *arg0;
            }
            throw TConfigurationError() << "Modifier [[bad]]" << ToString(Id) << "[[rst]] must be applied to a valid output";
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

    class TAddToIncl: public TOutputFlagger {
    public:
        TAddToIncl(): TOutputFlagger({.Id = EMacroFunction::AddToIncl, .Name = "addincl", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.AddToIncl = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
