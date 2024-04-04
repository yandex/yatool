#include "function_evaluator.h"

#include <devtools/ymake/config/config.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/exec.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>
#include <util/generic/yexception.h>

using namespace NCommands;

TTermValue NCommands::RenderArgs(std::span<const TTermValue> args) {
    TVector<TString> result;
    for (auto& arg : args)
        result.push_back(std::visit(TOverloaded{
            [](std::monostate) -> TString {
                throw TNotImplemented();
            },
            [&](const TString& s) -> TString {
                return s;
            },
            [&](const TVector<TString>&) -> TString {
                throw TNotImplemented();
            }
        }, arg));
    return result;
}

TTermValue NCommands::RenderTerms(std::span<const TTermValue> args) {
    TString result;
    for (auto& arg : args)
        result += std::visit(TOverloaded{
            [](std::monostate) -> TString {
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
            }
        }, arg);
    return result;
}

TTermValue NCommands::RenderClear(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "Clear requires 1 argument";
    }
    return std::visit(TOverloaded{
        [](std::monostate) -> TTermValue {
            return std::monostate();
        },
        [&](const TString&) -> TTermValue {
            return TString();
        },
        [&](const TVector<TString>& v) -> TTermValue {
            if (v.empty())
                return std::monostate();
            return TString();
        }
    }, args[0]);
}

TTermValue NCommands::RenderPre(std::span<const TTermValue> args) {
    if (args.size() != 2) {
        throw yexception() << "Pre requires 2 arguments";
    }
    return std::visit(TOverloaded{

        [](std::monostate) -> TTermValue {
            return std::monostate();
        },

        [&](const TString& body) {
            return std::visit(TOverloaded{
                [](std::monostate) -> TTermValue {
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
                }
            }, args[0]);
        },

        [&](const TVector<TString>& bodies) -> TTermValue {
            return std::visit(TOverloaded{
                [](std::monostate) -> TTermValue {
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
                }
            }, args[0]);
        }

    }, args[1]);
}

TTermValue NCommands::RenderSuf(std::span<const TTermValue> args) {
    if (args.size() != 2) {
        throw yexception() << "Suf requires 2 arguments";
    }
    auto suffix = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](std::monostate) -> TTermValue {
            return std::monostate();
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
        }
    }, args[1]);
}

TTermValue NCommands::RenderQuo(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "Quo requires 1 argument";
    }
    // "quo" is used to wrap pieces of the unparsed command line;
    // the quotes in question should disappear after argument extraction,
    // so for the arg-centric model this modifier is effectively a no-op
    return args[0];
}

void NCommands::RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "Env requires 1 argument";
    }
    std::visit(TOverloaded{
        [](std::monostate) {
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
        }
    }, args[0]);
}

void NCommands::RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "KeyValue requires 1 argument";
    }
    std::visit(TOverloaded{
        [](std::monostate) {
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
        }
    }, args[0]);
}

TTermValue NCommands::RenderCutExt(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "Noext requires 1 argument";
    }
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
        [](std::monostate) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        }
    }, args[0]);
}

TTermValue NCommands::RenderLastExt(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "Lastext requires 1 argument";
    }
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
        [](std::monostate) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return apply(std::move(s));
        },
        [&](TVector<TString> v) -> TTermValue {
            for (auto& s : v)
                s = apply(std::move(s));
            return std::move(v);
        }
    }, args[0]);
}

TTermValue NCommands::RenderExtFilter(std::span<const TTermValue> args) {
    if (args.size() != 2) {
        throw yexception() << "Ext requires 2 arguments";
    }
    auto ext = std::get<TString>(args[0]);
    return std::visit(TOverloaded{
        [](std::monostate) -> TTermValue {
            throw TNotImplemented();
        },
        [&](TString s) -> TTermValue {
            return s.EndsWith(ext) ? args[1] : std::monostate();
        },
        [&](TVector<TString> v) -> TTermValue {
            v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return !s.EndsWith(ext); }), v.end());
            return std::move(v);
        }
    }, args[1]);
}

TTermValue NCommands::RenderTODO1(std::span<const TTermValue> args) {
    if (args.size() != 1) {
        throw yexception() << "TODO1 requires 1 argument";
    }
    auto arg0 = std::visit(TOverloaded{
        [](std::monostate) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        }
    }, args[0]);
    return fmt::format("TODO1({})", arg0);
}

TTermValue NCommands::RenderTODO2(std::span<const TTermValue> args) {
    if (args.size() != 2) {
        throw yexception() << "TODO2 requires 2 arguments";
    }
    auto arg0 = std::visit(TOverloaded{
        [](std::monostate) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        }
    }, args[0]);
    auto arg1 = std::visit(TOverloaded{
        [](std::monostate) {
            return TString("-");
        },
        [&](const TString& s) {
            return s;
        },
        [&](const TVector<TString>& v) -> TString {
            return fmt::format("{}", fmt::join(v, " "));
        }
    }, args[1]);
    return fmt::format("TODO2({}, {})", arg0, arg1);
}
