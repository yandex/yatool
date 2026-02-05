#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

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

    class TTagsFlt: public TBasicModImpl {
    public:
        TTagsFlt(EMacroFunction id, TStringBuf name, bool exclude):
            TBasicModImpl({.Id = id, .Name = name, .Arity = 2}),
            Exclude(exclude)
        {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto tags = ParseMacroTags(std::get<TString>(args[0])); // TODO preparse
            auto items = std::visit(TOverloaded{
                [](TTermError) -> TTaggedStrings {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTaggedStrings {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& x) -> TTaggedStrings {
                    throw TBadArgType(Name, x);
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
                return MatchTags(tags, s.Tags) == Exclude;
            }), items.end());
            return std::move(items);
        }
    private:
        const bool Exclude;
    };

    class TTagsIn: public TTagsFlt {
    public:
        TTagsIn(): TTagsFlt(EMacroFunction::TagsIn, "tags_in", false) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TTagsOut: public TTagsFlt {
    public:
        TTagsOut(): TTagsFlt(EMacroFunction::TagsOut, "tags_out", true) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TTagsCut: public TBasicModImpl {
    public:
        TTagsCut(): TBasicModImpl({.Id = EMacroFunction::TagsCut, .Name = "tags_cut", .Arity = 1}) {
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
                [&](const TString& x) -> TTermValue {
                    throw TBadArgType(Name, x);
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
    } Y_GENERATE_UNIQUE_ID(Mod);

}
