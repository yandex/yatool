#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/conf.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

    class TRootRel: public TBasicModImpl {
    public:
        TRootRel(): TBasicModImpl({.Id = EMacroFunction::RootRel, .Name = "rootrel", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [&ctx](TString s) {
                // lifted from EMF_PrnRootRel processing:
                return TString(NPath::CutType(ctx.BuildConf.CanonPath(s)));
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
                [&](const TTaggedStrings& v) -> TTermValue {
                    TVector<TString> vv(v.size());
                    std::transform(v.begin(), v.end(), vv.begin(), [&](auto& s) {
                        return apply(s.Data);
                    });
                    return std::move(vv);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TCutPath: public TBasicModImpl {
    public:
        TCutPath(): TBasicModImpl({.Id = EMacroFunction::CutPath, .Name = "nopath", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](std::string_view path) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(path)); // TODO get rid of this
                    if (names.size() != 1)
                        throw std::runtime_error{"nopath modifier requires a single argument"};
                    return ProcessOne(ctx, names.front());
                },
                [&](const std::vector<std::string_view>& paths) -> TMacroValues::TValue {
                    if (paths.size() != 1)
                        throw std::runtime_error{"nopath modifier requires a single argument"};
                    return ProcessOne(ctx, paths.front());
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[0]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return TString(Cut(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = TString(Cut(s));
                    return std::move(v);
                },
                [&](TTaggedStrings x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    private:
        static TStringBuf Cut(TStringBuf path) {
            // lifted from EMF_CutPath processing:
            size_t slash = path.rfind(NPath::PATH_SEP);
            if (slash != TString::npos)
                path = path.substr(slash + 1);
            return path;
        }
        static TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, TStringBuf path) {
            return ctx.Values.GetValue(ctx.Values.InsertStr(Cut(path)));
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TCutExt: public TBasicModImpl {
    public:
        TCutExt(): TBasicModImpl({.Id = EMacroFunction::CutExt, .Name = "noext", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](std::string_view path) -> TMacroValues::TValue {
                    auto names = SplitArgs(TString(path)); // TODO get rid of this
                    if (names.size() != 1)
                        throw std::runtime_error{"noext modifier requires a single argument"};
                    return ProcessOne(ctx, names.front());
                },
                [&](const std::vector<std::string_view>& paths) -> TMacroValues::TValue {
                    if (paths.size() != 1)
                        throw std::runtime_error{"noext modifier requires a single argument"};
                    return ProcessOne(ctx, paths.front());
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[0]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return TString(Cut(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = TString(Cut(s));
                    return std::move(v);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    private:
        static TStringBuf Cut(TStringBuf path) {
            // lifted from EMF_CutExt processing:
            size_t slash = path.rfind(NPath::PATH_SEP); //todo: windows slash!
            if (slash == TString::npos)
                slash = 0;
            size_t dot = path.rfind('.');
            if (dot != TString::npos && dot >= slash)
                path = path.substr(0, dot);
            return path;
        }
        static TMacroValues::TValue ProcessOne(const TPreevalCtx& ctx, TStringBuf path) {
            return ctx.Values.GetValue(ctx.Values.InsertStr(Cut(path)));
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TLastExt: public TBasicModImpl {
    public:
        TLastExt(): TBasicModImpl({.Id = EMacroFunction::LastExt, .Name = "lastext", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [](TString s) {
                // lifted from EMF_LastExt processing:
                // It would be nice to use some common utility function from common/npath.h,
                // but Extension function implements rather strange behaviour
                auto slash = s.rfind(NPath::PATH_SEP);
                auto dot = s.rfind('.');
                if (dot != TStringBuf::npos && (slash == TStringBuf::npos || slash < dot)) {
                    s = s.substr(dot + 1);
                } else {
                    s.clear();
                }
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
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class THasDefaultExt: public TBasicModImpl {
    public:
        THasDefaultExt(): TBasicModImpl({.Id = EMacroFunction::HasDefaultExt, .Name = "defext", .Arity = 2, .CanPreevaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            auto arg1 = std::get<std::string_view>(args[1]);
            // cf. EMF_HasDefaultExt handling
            size_t dot = arg1.rfind('.');
            size_t slash = arg1.rfind(NPath::PATH_SEP);
            bool hasSpecExt = slash != TString::npos ? (dot > slash) : true;
            if (dot != TString::npos && hasSpecExt)
                return ctx.Values.GetValue(ctx.Values.InsertStr(arg1));
            auto id = ctx.Values.InsertStr(TString::Join(arg1, arg0));
            return ctx.Values.GetValue(id);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TExtFilter: public TBasicModImpl {
    public:
        TExtFilter(): TBasicModImpl({.Id = EMacroFunction::ExtFilter, .Name = "ext", .Arity = 2, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto ext = std::get<TString>(args[0]);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return s.EndsWith(ext) ? args[1] : TTermNothing();
                },
                [&](TVector<TString> v) -> TTermValue {
                    v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return !s.EndsWith(ext); }), v.end());
                    return std::move(v);
                },
                [&](TTaggedStrings v) -> TTermValue {
                    v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return !s.Data.EndsWith(ext); }), v.end());
                    return std::move(v);
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
