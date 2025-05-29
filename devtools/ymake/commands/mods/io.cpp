#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/config/config.h> // for GetOrInit()
#include <devtools/ymake/module_state.h> // for ConstrYDirDiag

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
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        return ProcessMany(ctx, names);
                },
                [&](const std::vector<std::string_view>& names) -> TMacroValues::TValue {
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        return ProcessMany(ctx, names);
                },
                [](auto&&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                },
            }, args[0]);
        }
    private:
        std::string_view ProcessPath(const TPreevalCtx& ctx, std::string_view path) const {
            // a combination of path normalization from TGeneralParser::AddCommandNodeDeps and tool name processing from MineVariables
            auto dir = NPath::IsExternalPath(path) ? TString{path} : NPath::ConstructYDir(path, TStringBuf(), ConstrYDirDiag);
            auto key = NPath::CutType(dir);
            return std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(key)));
        }
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name) const {
            return TMacroValues::TTool {.Data = ProcessPath(ctx, name)};
        };
        TMacroValues::TValue ProcessMany(const TPreevalCtx& ctx, auto& names) const {
            auto result = TMacroValues::TTools();
            for (auto& name : names)
                result.Data.push_back(ProcessPath(ctx, name));
            return result;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TResult: public TBasicModImpl {
        // more precisely, "result of the specified target";
        // this is basically `tool` without switching to the host platform;
        // so far, only single-target usage has been noticed
    public:
        TResult(): TBasicModImpl({.Id = EMacroFunction::Result, .Name = "result", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](std::string_view name) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        return ProcessMany(ctx, names);
                },
                [&](const std::vector<std::string_view>& names) -> TMacroValues::TValue {
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front());
                    else
                        return ProcessMany(ctx, names);
                },
                [](auto&&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                },
            }, args[0]);
        }
    private:
        std::string_view ProcessPath(const TPreevalCtx& ctx, std::string_view path) const {
            // a combination of path normalization from TGeneralParser::AddCommandNodeDeps and tool name processing from MineVariables
            auto dir = NPath::IsExternalPath(path) ? TString{path} : NPath::ConstructYDir(path, TStringBuf(), ConstrYDirDiag);
            auto key = NPath::CutType(dir);
            return std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(key)));
        }
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name) const {
            return TMacroValues::TResult {.Data = ProcessPath(ctx, name)};
        };
        TMacroValues::TValue ProcessMany(const TPreevalCtx&, auto& names) const {
            throw TBadArgType(Name, names);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TInput: public TBasicModImpl {
    public:
        TInput(): TBasicModImpl({.Id = EMacroFunction::Input, .Name = "input", .Arity = 0, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);

            auto _args = TArgs();
            if (args.size() == 1)
                _args.Path = args[0];
            else if (args.size() == 2) {
                auto arg0 = std::get<std::string_view>(args[0]);
                _args.Context = TFileConf::GetContextType(arg0);
                _args.Path = args[1];
            } else
                FailArgCount(args.size(), "1-2");

            return std::visit(TOverloaded{
                [&](std::string_view name) {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front(), _args.Context, false, false);
                    return ProcessMany(ctx, names, _args.Context, false, false);
                },
                [&](const std::vector<std::string_view>& names) {
                    if (names.size() == 1)
                        return ProcessOne(ctx, names.front(), _args.Context, false, false);
                    return ProcessMany(ctx, names, _args.Context, false, false);
                },
                [&](TMacroValues::TGlobPattern glob) {
                    if (glob.Data.size() == 1)
                        return ProcessOne(ctx, glob.Data.front(), _args.Context, true, false);
                    return ProcessMany(ctx, glob.Data, _args.Context, true, false);
                },
                [&](TMacroValues::TLegacyLateGlobPatterns glob) {
                    return ProcessMany(ctx, glob.Data, _args.Context, false, true);
                },
                [&](const auto& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
                },
            }, _args.Path);
        }
    private:
        struct TArgs {
            ELinkType Context = ELinkType::ELT_Default;
            TMacroValues::TValue Path;
        };
        auto ProcessCoord(const TPreevalCtx& ctx, std::string_view name, ELinkType context, bool isGlob, bool isLegacyGlob) const {
            if (!isLegacyGlob) {
                // WORKAROUND[mixed LATE_GLOBs]
                // see the respective note in `TCommands::TInliner::GetVariableDefinition`
                auto propName = CheckAndGetCmdName(name);
                if (propName == "LATE_GLOB")
                    isLegacyGlob = true;
            }
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue([&]() {
                if (context != ELT_Default)
                    return ctx.Values.InsertStr(TFileConf::ConstructLink(context, NPath::ConstructPath(name))); // lifted from TCommandInfo::ApplyMods
                else
                    return ctx.Values.InsertStr(name);
            }()));
            auto coord = ctx.Sink.Inputs.CollectCoord(pooledName);
            ctx.Sink.Inputs.UpdateCoord(coord, [=](auto& var) { var.IsGlob = isGlob; var.IsLegacyGlob = isLegacyGlob; });
            return coord;
        };
        TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, std::string_view name, ELinkType context, bool isGlob, bool isLegacyGlob) const {
            return TMacroValues::TInput {.Coord = ProcessCoord(ctx, name, context, isGlob, isLegacyGlob)};
        };
        TMacroValues::TValue ProcessMany(const TPreevalCtx& ctx, auto& names, ELinkType context, bool isGlob, bool isLegacyGlob) const {
            auto result = TMacroValues::TInputs();
            for (auto& name : names)
                result.Coords.push_back(ProcessCoord(ctx, name, context, isGlob, isLegacyGlob));
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
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            ctx.Sink.OutputIncludes.CollectCoord(pooledName);
            return pooledName;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TOutInclsFromInput: public TBasicModImpl {
        // TBD: the old engine seems to also support this for induced_deps, but this feature is unused; do we want it?
    public:
        TOutInclsFromInput(): TBasicModImpl({.Id = EMacroFunction::OutInclsFromInput, .Name = "from_input", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
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
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            auto ix = ctx.Sink.OutputIncludes.Index(pooledName);
            if (ix == NPOS) [[unlikely]]
                throw TConfigurationError() << "Unknown output-include [[bad]]" << name << "[[rst]], could not mark as from-input";
            ctx.Sink.OutputIncludes.Update(ix, [&](auto& var) {
                var.OutInclsFromInput = true;
            });
            return pooledName;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TInducedDeps: public TBasicModImpl {
        // this is basically parameterized `output_include`
    public:
        TInducedDeps(): TBasicModImpl({.Id = EMacroFunction::InducedDeps, .Name = "induced_deps", .Arity = 2, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto exts = std::get<std::string_view>(args[0]);
            auto& dst = ctx.Sink.OutputIncludesForType[exts];
            return std::visit(TOverloaded{
                [&](std::string_view name) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(name)); // TODO get rid of this
                    auto result = std::vector<std::string_view>();
                    result.reserve(names.size());
                    for (auto& name : names)
                        result.push_back(ProcessOne(ctx, name, dst));
                    return result;
                },
                [&](std::vector<std::string_view> names) -> TMacroValues::TValue {
                    for (auto& name : names)
                        name = ProcessOne(ctx, name, dst);
                    return std::move(names);
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[1]);
        }
    private:
        std::string_view ProcessOne(const TPreevalCtx& ctx, std::string_view name, NCommands::TCompiledCommand::TOutputIncludes& dst) const {
            auto pooledName = std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(name)));
            dst.CollectCoord(pooledName);
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
                [&](TTermNothing) {
                    // valid - do nothing
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
        TContext(): TBasicModImpl({.Id = EMacroFunction::Context_Deprecated, .Name = "context", .Arity = 2, .MustPreevaluate = true, .CanPreevaluate = true}) {
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
                        var.Context_Deprecated = context;
                    });
                return *arg1;
            }
            if (auto arg1 = std::get_if<TMacroValues::TInput>(&args[1])) {
                ctx.Sink.Inputs.UpdateCoord(arg1->Coord, [=](auto& var) {
                    var.Context_Deprecated = context;
                });
                return *arg1;
            }
            throw TConfigurationError() << "Modifier [[bad]]" << ToString(Id) << "[[rst]] must be applied to a valid input sequence";
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TInputOutputFlagger: public TBasicModImpl {
    public:
        TInputOutputFlagger(TModMetadata metadata): TBasicModImpl(metadata) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [&](TMacroValues::TInput input) {
                    ctx.Sink.Inputs.UpdateCoord(input.Coord, [&](auto& var) {Do(var);});
                },
                [&](const TMacroValues::TInputs& inputs) {
                    std::for_each(inputs.Coords.begin(), inputs.Coords.end(), [&](const auto coord) {
                        ctx.Sink.Inputs.UpdateCoord(coord, [&](auto& var) {Do(var);});
                    });
                },
                [&](TMacroValues::TOutput output) {
                    ctx.Sink.Outputs.UpdateCoord(output.Coord, [&](auto& var) {Do(var);});
                },
                [&](const TMacroValues::TOutputs& outputs) {
                    std::for_each(outputs.Coords.begin(), outputs.Coords.end(), [&](const auto coord) {
                        ctx.Sink.Outputs.UpdateCoord(coord, [&](auto& var) {Do(var);});
                    });
                },
                [&](const auto& x) {
                    throw TBadArgType(Name, x);
                }
            }, args.front());
            return args.front();
        }
    protected:
        virtual void Do(TCompiledCommand::TInput&) const {
            throw TBadArgType(Name, TMacroValues::TInput());
        }
        virtual void Do(TCompiledCommand::TOutput&) const {
            throw TBadArgType(Name, TMacroValues::TOutput());
        }
    };

    class TNoAutoSrc: public TInputOutputFlagger {
    public:
        TNoAutoSrc(): TInputOutputFlagger({.Id = EMacroFunction::NoAutoSrc, .Name = "noauto", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.NoAutoSrc = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TNoRel: public TInputOutputFlagger {
    public:
        TNoRel(): TInputOutputFlagger({.Id = EMacroFunction::NoRel, .Name = "norel", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.NoRel = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TToBinDir: public TInputOutputFlagger {
    public:
        TToBinDir(): TInputOutputFlagger({.Id = EMacroFunction::ResolveToBinDir, .Name = "tobindir", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TInput& input) const override {
            input.ResolveToBinDir = true;
        }
        void Do(TCompiledCommand::TOutput& output) const override {
            output.ResolveToBinDir = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TNoTransformRelativeBuildDir: public TInputOutputFlagger {
    public:
        TNoTransformRelativeBuildDir(): TInputOutputFlagger({.Id = EMacroFunction::NoTransformRelativeBuildDir, .Name = "notransformbuilddir", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TInput& input) const override {
            input.NoTransformRelativeBuildDir = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TAddToIncl: public TInputOutputFlagger {
    public:
        TAddToIncl(): TInputOutputFlagger({.Id = EMacroFunction::AddToIncl, .Name = "addincl", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.AddToIncl = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TGlobal: public TInputOutputFlagger {
    public:
        TGlobal(): TInputOutputFlagger({.Id = EMacroFunction::Global, .Name = "global", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.IsGlobal = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TMain: public TInputOutputFlagger {
    public:
        TMain(): TInputOutputFlagger({.Id = EMacroFunction::Main, .Name = "main", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.Main = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TAddToModOutputs: public TInputOutputFlagger {
    public:
        TAddToModOutputs(): TInputOutputFlagger({.Id = EMacroFunction::AddToModOutputs, .Name = "add_to_outs", .Arity = 1, .MustPreevaluate = true, .CanPreevaluate = true}) {
        }
    protected:
        void Do(TCompiledCommand::TOutput& output) const override {
            output.AddToModOutputs = true;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
