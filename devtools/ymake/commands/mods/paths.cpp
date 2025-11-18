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

    class TPrnOnlyRoot: public TBasicModImpl {
    public:
        TPrnOnlyRoot(): TBasicModImpl({.Id = EMacroFunction::PrnOnlyRoot, .Name = "rootdir", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [&ctx](TString s) {
                // lifted from EMF_PrnOnlyRoot processing:
                return TString(ctx.BuildConf.RealPathRoot(ctx.BuildConf.CanonPath(s)));
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

    class TNoBuildRoot: public TBasicModImpl {
    public:
        TNoBuildRoot(): TBasicModImpl({.Id = EMacroFunction::NoBuildRoot, .Name = "nobuildroot", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            TStringBuf buildRoot = ctx.BuildConf.BuildRoot.c_str();
            auto apply = [buildRoot](TStringBuf name) {
                // lifted from EMF_Namespace/ApplyNamespaceModifier processing:
                Y_ASSERT(name.StartsWith(buildRoot));
                size_t slash = name.find(buildRoot);
                if (slash != TString::npos) {
                    name = name.substr(slash + buildRoot.size() + 1);
                }
                return name;
            };
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return TString(apply(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = apply(s);
                    return v;
                },
                [&](const TTaggedStrings& v) -> TTermValue {
                    TVector<TString> vv(v.size());
                    std::transform(v.begin(), v.end(), vv.begin(), [&](auto& s) {
                        return apply(s.Data);
                    });
                    return vv;
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
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](const TMacroValues::TXString& path) -> TMacroValues::TValue {
                    auto names = SplitArgs(path.Data); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(names.front());
                    return ProcessMany(names);
                },
                [&](const TMacroValues::TXStrings& paths) -> TMacroValues::TValue {
                    if (paths.Data.size() == 1)
                        return ProcessOne(paths.Data.front());
                    return ProcessMany(paths.Data);
                },
                [](const auto&) -> TMacroValues::TValue {
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
        static TMacroValues::TValue ProcessOne(TStringBuf path) {
            return TMacroValues::TXString{std::string(Cut(path))};
        }
        static TMacroValues::TValue ProcessMany(auto& paths) {
            auto result = TMacroValues::TXStrings();
            result.Data.reserve(paths.size());
            for (auto& path : paths)
                result.Data.push_back(std::string(Cut(path)));
            return result;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TCutSomeExt: public TBasicModImpl {
    public:
        TCutSomeExt(bool total, TModMetadata metadata): TBasicModImpl(metadata), TotalAnnihilation(total) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](const TMacroValues::TXString& path) -> TMacroValues::TValue {
                    auto names = SplitArgs(path.Data); // TODO get rid of this
                    if (names.size() == 1)
                        return ProcessOne(names.front());
                    return ProcessMany(names);
                },
                [&](const TMacroValues::TXStrings& paths) -> TMacroValues::TValue {
                    if (paths.Data.size() == 1)
                        return ProcessOne(paths.Data.front());
                    return ProcessMany(paths.Data);
                },
                [](const auto&) -> TMacroValues::TValue {
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
        const bool TotalAnnihilation;
        TStringBuf Cut(TStringBuf path) const {
            // lifted from EMF_CutExt & EMF_CutAllExt processing:
            size_t slash = path.rfind(NPath::PATH_SEP); //todo: windows slash!
            if (slash == TString::npos)
                slash = 0;
            size_t dot = TotalAnnihilation ? path.find('.', slash) : path.rfind('.');
            if (dot != TString::npos && dot >= slash)
                path = path.substr(0, dot);
            return path;
        }
        TMacroValues::TValue ProcessOne(TStringBuf path) const {
            return TMacroValues::TXString{std::string(Cut(path))};
        }
        TMacroValues::TValue ProcessMany(auto& paths) const {
            auto result = TMacroValues::TXStrings();
            result.Data.reserve(paths.size());
            for (auto& path : paths)
                result.Data.push_back(std::string(Cut(path)));
            return result;
        }
    };

    class TCutExt: public TCutSomeExt {
    public:
        TCutExt(): TCutSomeExt(false, {.Id = EMacroFunction::CutExt, .Name = "noext", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TCutAllExt: public TCutSomeExt {
    public:
        TCutAllExt(): TCutSomeExt(true, {.Id = EMacroFunction::CutAllExt, .Name = "noallext", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
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
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<TMacroValues::TXString>(args[0]);
            auto arg1 = std::get<TMacroValues::TXString>(args[1]);
            // cf. EMF_HasDefaultExt handling
            size_t dot = arg1.Data.rfind('.');
            size_t slash = arg1.Data.rfind(NPath::PATH_SEP);
            bool hasSpecExt = slash != TString::npos ? (dot > slash) : true;
            if (dot != TString::npos && hasSpecExt)
                return arg1;
            return TMacroValues::TXString{TString::Join(arg1.Data, arg0.Data)};
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TExtFilter: public TBasicModImpl {
    public:
        TExtFilter(): TBasicModImpl({.Id = EMacroFunction::ExtFilter, .Name = "ext", .Arity = 2, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            std::span<const TTermValue> args,
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
                    return !s.EndsWith(ext) ? TTermNothing() : args[1];
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

        TMacroValues::TValue Preevaluate([[maybe_unused]] const TPreevalCtx &ctx, std::span<TMacroValues::TValue> args) const override {
            CheckArgCount(args);
            const TStringBuf ext = std::get<TMacroValues::TXString>(args[0]).Data;
            return std::visit(TOverloaded{
                [&](TMacroValues::TXString item) -> TMacroValues::TValue {
                    return item.Data.ends_with(ext) ? item : TMacroValues::TValue{};
                },
                [&](TMacroValues::TXStrings list) -> TMacroValues::TValue {
                    auto toErase = std::ranges::remove_if(list.Data, [&](const std::string& item){return !item.ends_with(ext);});
                    list.Data.erase(toErase.begin(), toErase.end());
                    return std::move(list);
                },
                [](const auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TSkipByExtFilter: public TBasicModImpl {
    public:
        TSkipByExtFilter(): TBasicModImpl({.Id = EMacroFunction::SkipByExtFilter, .Name = "skip_by_ext", .Arity = 2, .CanEvaluate = true}) {
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
                    return s.EndsWith(ext) ? TTermNothing() : args[1];
                },
                [&](TVector<TString> v) -> TTermValue {
                    v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return s.EndsWith(ext); }), v.end());
                    return std::move(v);
                },
                [&](TTaggedStrings v) -> TTermValue {
                    v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return s.Data.EndsWith(ext); }), v.end());
                    return std::move(v);
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
