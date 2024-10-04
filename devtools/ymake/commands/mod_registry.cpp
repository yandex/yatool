#include "mod_registry.h"

#include <devtools/ymake/config/config.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/exec.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>
#include <util/generic/yexception.h>

using namespace NCommands;

namespace {
    auto& ModDescriptions() {
        static THashMap<EMacroFunction, TModImpl*> modDescriptions;
        return modDescriptions;
    }
}

NCommands::TModImpl::TModImpl(TModMetadata metadata): TModMetadata(metadata) {
    Y_ABORT_IF(ModDescriptions().contains(Id));
    ModDescriptions().insert({Id, this});
}

NCommands::TModImpl::~TModImpl() {
    ModDescriptions().erase(Id);
}

namespace {

    constexpr ui16 FunctionArity(EMacroFunction func) noexcept {
        ui16 res = 0;
        switch (func) {
            // Variadic fucntions
            case EMacroFunction::Terms:
            case EMacroFunction::Cat:
                break;

            // Unary functions
            case EMacroFunction::Output:
            case EMacroFunction::Tmp:
            case EMacroFunction::Tool:
            case EMacroFunction::Clear:
            case EMacroFunction::Quo:
            case EMacroFunction::QuoteEach:
            case EMacroFunction::ToUpper:
            case EMacroFunction::ToLower:
            case EMacroFunction::Cwd:
            case EMacroFunction::AsStdout:
            case EMacroFunction::SetEnv:
            case EMacroFunction::RootRel:
            case EMacroFunction::CutPath:
            case EMacroFunction::LastExt:
            case EMacroFunction::LateOut:
            case EMacroFunction::TagsCut:
            case EMacroFunction::TODO1:
            case EMacroFunction::NoAutoSrc:
            case EMacroFunction::NoRel:
            case EMacroFunction::ResolveToBinDir:
            case EMacroFunction::Glob:
                res = 1;
                break;

            // Binary functions
            case EMacroFunction::Pre:
            case EMacroFunction::Suf:
            case EMacroFunction::HasDefaultExt:
            case EMacroFunction::Join:
            case EMacroFunction::ExtFilter:
            case EMacroFunction::TagsIn:
            case EMacroFunction::TagsOut:
            case EMacroFunction::Context:
            case EMacroFunction::TODO2:
                res = 2;
                break;

            // ported out
            case EMacroFunction::Cmds:
            case EMacroFunction::Args:
            case EMacroFunction::Input:
            case EMacroFunction::Hide:
            case EMacroFunction::KeyValue:
            case EMacroFunction::CutExt:
                Y_ABORT();

            // suppress the "enumeration value 'Count' not handled in switch" silliness
            case EMacroFunction::Count:
                Y_ABORT();
        }
        return res;
    }

}

NCommands::TModRegistry::TModRegistry() {
    THashSet<EMacroFunction> done;
    Descriptions.fill(nullptr);
    for (auto& m : ModDescriptions()) {
        auto& _m = *m.second;
        Y_ABORT_IF(done.contains(_m.Id));
        Y_ABORT_IF(Index.contains(_m.Name));
        done.insert(_m.Id);
        Descriptions.at(ToUnderlying(_m.Id)) = &_m;
        if (!_m.Internal)
            Index[_m.Name] = _m.Id;
    }
}

ui16 TModRegistry::FuncArity(EMacroFunction func) const noexcept {
    if (auto desc = At(func))
        return desc->Arity;
    return FunctionArity(func);
}

NPolexpr::TFuncId TModRegistry::Func2Id(EMacroFunction func) const noexcept {
    return NPolexpr::TFuncId{FuncArity(func), static_cast<ui32>(func)};
}

EMacroFunction TModRegistry::Id2Func(NPolexpr::TFuncId id) const noexcept {
    return static_cast<EMacroFunction>(id.GetIdx());
}

//
//
//

namespace {

    // lifted from EMF_TagsIn/EMF_TagsOut processing

    TVector<TVector<TStringBuf>> ParseMacroTags(TStringBuf value) {
        TVector<TVector<TStringBuf>> tags;
        for (const auto& it : StringSplitter(value).Split('|').SkipEmpty()) {
            TVector<TStringBuf> subTags = StringSplitter(it.Token()).Split(',').SkipEmpty();
            if (!subTags.empty()) {
                tags.push_back(std::move(subTags));
            }
        }
        return tags;
    }

    inline bool MatchTags(const TVector<TVector<TStringBuf>>& macroTags, const TVector<TString> peerTags) {
        if (macroTags.empty())
            return true;
        if (peerTags.empty())
            return false;
        for (const auto& chunk : macroTags)
            if (AllOf(chunk, [&peerTags] (const auto tag) { return FindPtr(peerTags, tag); }))
                return true;
        return false;
    }

    void CheckArgCount(std::span<const TTermValue> args, size_t expected, const char* name) {
        if (args.size() != expected)
            throw yexception() << name << " requires " << expected << " argument(s)";
    }

    template<typename T> const char *PrintableTypeName();
    template<> [[maybe_unused]] const char *PrintableTypeName<TTermNothing    >() {return "Unit";}
    template<> [[maybe_unused]] const char *PrintableTypeName<TString         >() {return "String";}
    template<> [[maybe_unused]] const char *PrintableTypeName<TVector<TString>>() {return "Strings";}
    template<> [[maybe_unused]] const char *PrintableTypeName<TTaggedStrings  >() {return "TaggedStrings";}

}

#define BAD_ARGUMENT_TYPE(f, x) \
    throw TNotImplemented() << "type " << PrintableTypeName<std::remove_cvref_t<decltype(x)>>() << " is not supported by " << f

TTermValue NCommands::RenderTerms(std::span<const TTermValue> args) {
    static const char* fnName = "Terms";
    TString result;
    for (auto& arg : args)
        result += std::visit(TOverloaded{
            [](TTermError) -> TString {
                Y_ABORT();
            },
            [](TTermNothing x) -> TString {
                BAD_ARGUMENT_TYPE(fnName, x);
            },
            [&](const TString& s) -> TString {
                return s;
            },
            [&](const TVector<TString>& v) -> TString {
                if (v.empty())
                    return {};
                else if (v.size() == 1)
                    return v.front();
                else
                    throw TNotImplemented() << "Nested terms should not have multiple items";
            },
            [&](const TTaggedStrings& x) -> TString {
                BAD_ARGUMENT_TYPE(fnName, x);
            }
        }, arg);
    return result;
}

TTermValue NCommands::RenderCat(std::span<const TTermValue> args) {
    return RenderTerms(args);
}

TTermValue NCommands::RenderClear(std::span<const TTermValue> args) {
    static const char* fnName = "Clear";
    CheckArgCount(args, 1, fnName);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            return TTermNothing();
        },
        [&](const TString&) -> TTermValue {
            return TString();
        },
        [&](const TVector<TString>& v) -> TTermValue {
            if (v.empty())
                return TTermNothing();
            return TString();
        },
        [&](const TTaggedStrings& x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

TTermValue NCommands::RenderPre(std::span<const TTermValue> args) {
    static const char* fnName = "Pre";
    CheckArgCount(args, 2, fnName);
    return std::visit(TOverloaded{

        [](TTermError) -> TTermValue {
            Y_ABORT();
        },

        [](TTermNothing) -> TTermValue {
            return TTermNothing();
        },

        [&](const TString& body) {
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing x) -> TTermValue {
                    BAD_ARGUMENT_TYPE(fnName, x);
                },
                [&](const TString& prefix) -> TTermValue {
                    if (prefix.EndsWith(' ')) {
                        auto trimmedPrefix = prefix.substr(0, 1 + prefix.find_last_not_of(' '));
                        return TVector<TString>{std::move(trimmedPrefix), body};
                    }
                    return prefix + body;
                },
                [&](const TVector<TString>& prefixes) -> TTermValue {
                    TVector<TString> result;
                    result.reserve(prefixes.size());
                    for (auto& prefix : prefixes)
                        result.push_back(prefix + body);
                    return std::move(result);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    BAD_ARGUMENT_TYPE(fnName, x);
                }
            }, args[0]);
        },

        [&](const TVector<TString>& bodies) -> TTermValue {
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing x) -> TTermValue {
                    BAD_ARGUMENT_TYPE(fnName, x);
                },
                [&](const TString& prefix) -> TTermValue {
                    TVector<TString> result;
                    if (prefix.EndsWith(' ')) {
                        auto trimmedPrefix = prefix.substr(0, 1 + prefix.find_last_not_of(' '));
                        result.reserve(bodies.size() * 2);
                        for (auto& body : bodies) {
                            result.push_back(trimmedPrefix);
                            result.push_back(body);
                        }
                    } else {
                        result.reserve(bodies.size());
                        for (auto& body : bodies)
                            result.push_back(prefix + body);
                    }
                    return std::move(result);
                },
                [&](const TVector<TString>& prefixes) -> TTermValue {
                    Y_UNUSED(prefixes);
                    ythrow TNotImplemented() << "Pre arguments should not both be arrays";
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    BAD_ARGUMENT_TYPE(fnName, x);
                }
            }, args[0]);
        },

        [&](const TTaggedStrings& x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }

    }, args[1]);
}

TTermValue NCommands::RenderSuf(std::span<const TTermValue> args) {
    static const char* fnName = "Suf";
    CheckArgCount(args, 2, fnName);
    auto suffix = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            return TTermNothing();
        },
        [&](const TString& body) -> TTermValue {
            return body + suffix;
        },
        [&](const TVector<TString>& bodies) -> TTermValue {
            TVector<TString> result;
            result.reserve(bodies.size());
            for (auto&& body : bodies)
                result.push_back(body + suffix);
            return std::move(result);
        },
        [&](const TTaggedStrings& x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[1]);
}

TTermValue NCommands::RenderJoin(std::span<const TTermValue> args) {
    static const char* fnName = "Join";
    CheckArgCount(args, 2, fnName);
    auto glue = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            return TTermNothing();
        },
        [&](const TString& body) -> TTermValue {
            return body;
        },
        [&](const TVector<TString>& bodies) -> TTermValue {
            return JoinSeq(glue, bodies);
        },
        [&](const TTaggedStrings& x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[1]);
}

TTermValue NCommands::RenderQuo(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "Quo");
    // "quo" is used to wrap pieces of the unparsed command line;
    // the quotes in question should disappear after argument extraction,
    // so for the arg-centric model this modifier is effectively a no-op
    return args[0];
}

TTermValue NCommands::RenderQuoteEach(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "QuoteEach");
    return args[0]; // NOP for the same reason as "quo"
}

TTermValue NCommands::RenderToUpper(std::span<const TTermValue> args) {
    static const char* fnName = "ToUpper";
    CheckArgCount(args, 1, fnName);
    auto apply = [](TString s) {
        s.to_upper();
        return s;
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](TTaggedStrings x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

TTermValue NCommands::RenderToLower(std::span<const TTermValue> args) {
    static const char* fnName = "ToLower";
    CheckArgCount(args, 1, fnName);
    auto apply = [](TString s) {
        s.to_lower();
        return s;
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](TTaggedStrings x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

void NCommands::RenderCwd(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    static const char* fnName = "Cwd";
    CheckArgCount(args, 1, fnName);
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& s) {
            writer->WriteCwd(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
        },
        [&](const TVector<TString>& v) {
            if (v.empty())
                return;
            else if (v.size() == 1)
                writer->WriteCwd(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
            else
                throw TNotImplemented() << "Cwd does not support arrays";
        },
        [&](const TTaggedStrings& x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

void NCommands::RenderStdout(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    static const char* fnName = "StdOut";
    CheckArgCount(args, 1, fnName);
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& s) {
            writer->WriteStdout(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
        },
        [&](const TVector<TString>& v) {
            if (v.empty())
                return;
            else if (v.size() == 1)
                writer->WriteStdout(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
            else
                throw TNotImplemented() << "StdOut does not support arrays";
        },
        [&](const TTaggedStrings& x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

void NCommands::RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    static const char* fnName = "Env";
    CheckArgCount(args, 1, fnName);
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& s) {
            writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
        },
        [&](const TVector<TString>& v) {
            if (v.empty())
                return;
            else if (v.size() == 1)
                writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
            else
                throw TNotImplemented() << "Env does not support arrays";
        },
        [&](const TTaggedStrings& x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

void NCommands::RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args) {
    static const char* fnName = "KeyValue";
    CheckArgCount(args, 1, fnName);
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& s) {
            // lifted from EMF_KeyValue processing
            TString kvValue = ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false);
            TStringBuf name(kvValue);
            TStringBuf before;
            TStringBuf after;
            if (name.TrySplit(' ', before, after)) {
                TString val = TString{after};
                GetOrInit(ctx.CmdInfo.KV)[before] = val;
            } else {
                GetOrInit(ctx.CmdInfo.KV)[name] = "yes";
            }
        },
        [&](const TVector<TString>& v) {
            if (v.size() == 2)
                GetOrInit(ctx.CmdInfo.KV)[v[0]] = v[1];
            else if (v.size() == 1)
                GetOrInit(ctx.CmdInfo.KV)[v[0]] = "yes";
            else
                throw TNotImplemented() << "bad KV item count";
        },
        [&](const TTaggedStrings& x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

void NCommands::RenderLateOut(const TEvalCtx& ctx, std::span<const TTermValue> args) {
    static const char* fnName = "LateOut";
    CheckArgCount(args, 1, fnName);
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& s) {
            GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
        },
        [&](const TVector<TString>& v) {
            for (auto& s : v)
                GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
        },
        [&](const TTaggedStrings& x) {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

TTermValue NCommands::RenderTagFilter(std::span<const TTermValue> args, bool exclude) {
    static const char* fnName = "TagFilter";
    CheckArgCount(args, 2, fnName);
    auto tags = ParseMacroTags(std::get<TString>(args[0])); // TODO preparse
    auto items = std::visit(TOverloaded{
        [](TTermError) -> TTaggedStrings {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTaggedStrings {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& x) -> TTaggedStrings {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TVector<TString>& v) -> TTaggedStrings {
            // when PEERS is empty, we cannot detect its HasPeerDirTags and end up here
            Y_DEBUG_ABORT_UNLESS(v.empty());
            TTaggedStrings result(v.size());
            std::transform(v.begin(), v.end(), result.begin(), [](auto& s) {return TTaggedString{.Data = s};});
            return result;
        },
        [&](const TTaggedStrings& v) -> TTaggedStrings {
            return v;
        }
    }, args[1]);
    items.erase(std::remove_if(items.begin(), items.end(), [&](auto& s) {
        return MatchTags(tags, s.Tags) == exclude;
    }), items.end());
    return std::move(items);
}

TTermValue NCommands::RenderTagCut(std::span<const TTermValue> args) {
    static const char* fnName = "TagCut";
    CheckArgCount(args, 1, fnName);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TString& x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](const TVector<TString>& v) -> TTermValue {
            // when PEERS is empty, we cannot detect its HasPeerDirTags and end up here
            Y_DEBUG_ABORT_UNLESS(v.empty());
            return v;
        },
        [&](const TTaggedStrings& v) -> TTermValue {
            TVector<TString> result(v.size());
            std::transform(v.begin(), v.end(), result.begin(), [](auto& s) {return s.Data;});
            return result;
        }
    }, args[0]);
}

TTermValue NCommands::RenderRootRel(std::span<const TTermValue> args) {
    static const char* fnName = "RootRel";
    CheckArgCount(args, 1, fnName);
    auto apply = [](TString s) {
        // lifted from EMF_PrnRootRel processing:
        return TString(NPath::CutType(GlobalConf()->CanonPath(s)));
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
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

TTermValue NCommands::RenderCutPath(std::span<const TTermValue> args) {
    static const char* fnName = "Nopath";
    CheckArgCount(args, 1, fnName);
    auto apply = [](TString s) {
        // lifted from EMF_CutPath processing:
        size_t slash = s.rfind(NPath::PATH_SEP);
        if (slash != TString::npos)
            s = s.substr(slash + 1);
        return s;
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](TTaggedStrings x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

TTermValue NCommands::RenderLastExt(std::span<const TTermValue> args) {
    static const char* fnName = "LastExt";
    CheckArgCount(args, 1, fnName);
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
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
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
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
}

TTermValue NCommands::RenderExtFilter(std::span<const TTermValue> args) {
    static const char* fnName = "ExtFilter";
    CheckArgCount(args, 2, fnName);
    auto ext = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing x) -> TTermValue {
            BAD_ARGUMENT_TYPE(fnName, x);
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

TTermValue NCommands::RenderTODO1(std::span<const TTermValue> args) {
    static const char* fnName = "TODO1";
    CheckArgCount(args, 1, fnName);
    auto arg0 = std::visit(TOverloaded{
        [](TTermError) -> TString {
            Y_ABORT();
        },
        [](TTermNothing) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        },
        [&](const TTaggedStrings& x) -> TString {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
    return fmt::format("TODO1({})", arg0);
}

TTermValue NCommands::RenderTODO2(std::span<const TTermValue> args) {
    static const char* fnName = "TODO2";
    CheckArgCount(args, 2, fnName);
    auto arg0 = std::visit(TOverloaded{
        [](TTermError) -> TString {
            Y_ABORT();
        },
        [](TTermNothing) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        },
        [&](const TTaggedStrings& x) -> TString {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[0]);
    auto arg1 = std::visit(TOverloaded{
        [](TTermError) -> TString {
            Y_ABORT();
        },
        [](TTermNothing) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        },
        [&](const TTaggedStrings& x) -> TString {
            BAD_ARGUMENT_TYPE(fnName, x);
        }
    }, args[1]);
    return fmt::format("TODO2({}, {})", arg0, arg1);
}
