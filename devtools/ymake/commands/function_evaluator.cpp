#include "function_evaluator.h"

#include <devtools/ymake/config/config.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/exec.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>
#include <util/generic/yexception.h>

using namespace NCommands;

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

}

TTermValue NCommands::RenderArgs(std::span<const TTermValue> args) {
    TVector<TString> result;
    for (auto& arg : args)
        result.push_back(std::visit(TOverloaded{
            [](TTermError) -> TString {
                Y_ABORT();
            },
            [](TTermNothing) -> TString {
                throw TNotImplemented();
            },
            [&](const TString& s) -> TString {
                return s;
            },
            [&](const TVector<TString>&) -> TString {
                throw TNotImplemented();
            },
            [&](const TTaggedStrings&) -> TString {
                throw TNotImplemented();
            }
        }, arg));
    return result;
}

TTermValue NCommands::RenderTerms(std::span<const TTermValue> args) {
    TString result;
    for (auto& arg : args)
        result += std::visit(TOverloaded{
            [](TTermError) -> TString {
                Y_ABORT();
            },
            [](TTermNothing) -> TString {
                throw TNotImplemented();
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
                    throw yexception() << "Nested terms should not have multiple items";
            },
            [&](const TTaggedStrings&) -> TString {
                throw TNotImplemented();
            }
        }, arg);
    return result;
}

TTermValue NCommands::RenderCat(std::span<const TTermValue> args) {
    return RenderTerms(args);
}

TTermValue NCommands::RenderClear(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "Clear");
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
        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[0]);
}

TTermValue NCommands::RenderPre(std::span<const TTermValue> args) {
    CheckArgCount(args, 2, "Pre");
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
                [](TTermNothing) -> TTermValue {
                    ythrow TNotImplemented() << "Unexpected empty prefix";
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
                [&](const TTaggedStrings&) -> TTermValue {
                    throw TNotImplemented();
                }
            }, args[0]);
        },

        [&](const TVector<TString>& bodies) -> TTermValue {
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    ythrow TNotImplemented() << "Unexpected empty prefix";
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
                [&](const TTaggedStrings&) -> TTermValue {
                    throw TNotImplemented();
                }
            }, args[0]);
        },

        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
        }

    }, args[1]);
}

TTermValue NCommands::RenderSuf(std::span<const TTermValue> args) {
    CheckArgCount(args, 2, "Suf");
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
        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[1]);
}

TTermValue NCommands::RenderJoin(std::span<const TTermValue> args) {
    CheckArgCount(args, 2, "Join");
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
        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
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
    if (args.size() != 1) {
        throw yexception() << "QuoteEach requires 1 argument";
    }
    return args[0]; // NOP for the same reason as "quo"
}

TTermValue NCommands::RenderToUpper(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "ToUpper requires 1 argument";
    }
    auto apply = [](TString s) {
        s.to_upper();
        return s;
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](TTaggedStrings) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[0]);
}

void NCommands::RenderCwd(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "Cwd");
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing) {
            throw TNotImplemented();
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
                throw yexception() << "Cwd does not support arrays";
        },
        [&](const TTaggedStrings&) {
            throw TNotImplemented();
        }
    }, args[0]);
}

void NCommands::RenderStdout(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "StdOut requires 1 argument";
    }
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing) {
            throw TNotImplemented();
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
                throw yexception() << "StdOut does not support arrays";
        },
        [&](const TTaggedStrings&) {
            throw TNotImplemented();
        }
    }, args[0]);
}

void NCommands::RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "Env");
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing) {
            throw TNotImplemented();
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
                throw yexception() << "Env does not support arrays";
        },
        [&](const TTaggedStrings&) {
            throw TNotImplemented();
        }
    }, args[0]);
}

void NCommands::RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "KeyValue");
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing) {
            throw TNotImplemented();
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
                throw TNotImplemented();
        },
        [&](const TTaggedStrings&) {
            throw TNotImplemented();
        }
    }, args[0]);
}

void NCommands::RenderLateOut(const TEvalCtx& ctx, std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "LateOut requires 1 argument";
    }
    std::visit(TOverloaded{
        [](TTermError) {
            Y_ABORT();
        },
        [](TTermNothing) {
            throw TNotImplemented();
        },
        [&](const TString& s) {
            GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
        },
        [&](const TVector<TString>& v) {
            for (auto& s : v)
                GetOrInit(ctx.CmdInfo.LateOuts).push_back(s);
        },
        [&](const TTaggedStrings&) {
            throw TNotImplemented();
        }
    }, args[0]);
}

TTermValue NCommands::RenderTagFilter(std::span<const TTermValue> args, bool exclude) {
    auto selfName = "TagFilter";
    CheckArgCount(args, 2, selfName);
    auto tags = ParseMacroTags(std::get<TString>(args[0])); // TODO preparse
    auto items = std::visit(TOverloaded{
        [](TTermError) -> TTaggedStrings {
            Y_ABORT();
        },
        [](TTermNothing) -> TTaggedStrings {
            throw TNotImplemented();
        },
        [&](const TString&) -> TTaggedStrings {
            throw TNotImplemented();
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
    CheckArgCount(args, 1, "TagCut");
    auto items = std::get<TTaggedStrings>(args[0]);
    TVector<TString> result(items.size());
    std::transform(items.begin(), items.end(), result.begin(), [](auto& s) {return s.Data;});
    return result;
}

TTermValue NCommands::RenderRootRel(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "RootRel");
    auto apply = [](TString s) {
        // lifted from EMF_PrnRootRel processing:
        return TString(NPath::CutType(GlobalConf()->CanonPath(s)));
    };
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
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
    if (args.size() != 1) {
        throw yexception() << "Nopath requires 1 argument";
    }
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
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](TTaggedStrings) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[0]);
}

TTermValue NCommands::RenderCutExt(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "NoExt");
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
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[0]);
}

TTermValue NCommands::RenderLastExt(std::span<const TTermValue> args) {
    CheckArgCount(args, 1, "LastExt");
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
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        },
        [&](const TTaggedStrings&) -> TTermValue {
            throw TNotImplemented();
        }
    }, args[0]);
}

TTermValue NCommands::RenderExtFilter(std::span<const TTermValue> args) {
    CheckArgCount(args, 2, "ExtFilter");
    auto ext = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](TTermError) -> TTermValue {
            Y_ABORT();
        },
        [](TTermNothing) -> TTermValue {
            throw TNotImplemented();
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
    CheckArgCount(args, 1, "TODO1");
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
        [&](const TTaggedStrings&) -> TString {
            throw TNotImplemented();
        }
    }, args[0]);
    return fmt::format("TODO1({})", arg0);
}

TTermValue NCommands::RenderTODO2(std::span<const TTermValue> args) {
    CheckArgCount(args, 2, "TODO2");
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
        [&](const TTaggedStrings&) -> TString {
            throw TNotImplemented();
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
        [&](const TTaggedStrings&) -> TString {
            throw TNotImplemented();
        }
    }, args[1]);
    return fmt::format("TODO2({}, {})", arg0, arg1);
}
