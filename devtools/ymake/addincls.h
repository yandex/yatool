#pragma once

#include "dirs.h"

#include <devtools/ymake/common/iter_pair.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/resolver/path_resolver.h>
#include <devtools/ymake/symbols/symbols.h>

#include <library/cpp/json/writer/json.h>

#include <util/ysaveload.h>
#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <array>

enum class TLangId : ui32 {};

// This class provides seamless iteration over several collections.
template <typename TCollection, size_t MaxSize = 4>
class TIterableCollections : TMoveOnly {
private:
    std::array<const TCollection*, MaxSize> Collections;
    size_t Size = 0;

    class const_iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = TFileView;
        using difference_type = ptrdiff_t;
        using pointer = TFileView*;
        using reference = TFileView&;

        using ExternalIterator = typename decltype(Collections)::const_iterator;
        ExternalIterator ExternalIt;
        const ExternalIterator ExternalItEnd;
        typename TCollection::const_iterator InternalIt;

        void IterateToNotEmpty() {
            while (InternalIt == (*ExternalIt)->end()) {
                ExternalIt++;
                if (ExternalIt == ExternalItEnd) {
                    break;
                }
                InternalIt = (*ExternalIt)->begin();
            }
        }

    public:
        const_iterator(ExternalIterator begin, ExternalIterator end)
            : ExternalIt(begin)
            , ExternalItEnd(end)
        {
            if (ExternalIt != ExternalItEnd) {
                InternalIt = (*ExternalIt)->begin();
                IterateToNotEmpty();
            }
        }

        const_iterator& operator++() {
            Y_ASSERT(ExternalIt != ExternalItEnd);
            InternalIt++;
            IterateToNotEmpty();
            return *this;
        }

        const TFileView& operator*() const {
            Y_ASSERT(ExternalIt != ExternalItEnd);
            return *InternalIt;
        }

        bool operator==(const const_iterator& other) const {
            return ExternalIt == other.ExternalIt && (ExternalIt == ExternalItEnd || InternalIt == other.InternalIt);
        }

        bool operator!=(const const_iterator& other) const {
            return !(*this == other);
        }
    };

public:
    template <typename... Args, typename = std::enable_if_t<sizeof...(Args) <= MaxSize>>
    explicit TIterableCollections(Args&&... collections)
            : Collections({&collections...})
            , Size(sizeof...(Args))
    {
    }

    void Append(TIterableCollections&& other) {
        Y_ASSERT(Size + other.Size <= MaxSize);
        if (Size == 0) {
            Size = other.Size;
            Collections = std::move(other.Collections);
        } else {
            for (size_t to = Size, from = 0; from < other.Size; to++, from++) {
                Collections[to] = other.Collections[from];
            }
            Size += other.Size;
        }
    }

    void Append(const TCollection* element) {
        Y_ASSERT(Size + 1 <= MaxSize);
        Collections[Size++] = element;
    }

    const_iterator begin() const {
        return {Collections.begin(), std::next(Collections.begin(), Size)};
    }

    const_iterator end() const {
        return {std::next(Collections.begin(), Size), std::next(Collections.begin(), Size)};
    }

    size_t Total() const {
        size_t total = 0;
        for (const auto c : Collections) {
            total += c ? c->size() : 0;
        }
        return total;
    }
};

enum class EIncDirScope  {
    Local,
    User,
    Global
};

// TODO: Поправить документацию.

// TModuleIncDirs is a collection of language-separated ADDINCLs.
//
// Each ADDINCL can be: a) declared in this module as GLOBAL, b) declared in this module as local, c) induced from
// dependency by PEERDIR. On each PEERDIR child ADDINCL's are propagated to the parent's.
//
// Using for resolving: for resolving ADDINCLs are used in the following priority:
//   1. Module's own ADDINCLs specific for the language.
//   2. Induced ADDINCLs specific for the language.
//   3. Module's own ADDINCLs for default language.
//   4. Induced ADDINCLs for default language.
//
// Using for commands rendering: for rendering ADDINCLs are used in the following priority:
//   1. Module's own ADDINCLs for the language.
//   2. Induced ADDINCLs for the language.
// For each language (including default) an individual variable is rendered (see GetIncludeVarName()).
class TModuleIncDirs : private TMoveOnly {
public:
    struct TLanguageIncDirs : private TMoveOnly {
        THolder<TDirs> UserGlobalPropagated, GlobalPropagated;
        THolder<TDirs> LocalUserGlobal, UserGlobal, Global;

        struct TSavedState {
            TVector<ui32> LocalUserGlobal, UserGlobal, Global;
            Y_SAVELOAD_DEFINE(LocalUserGlobal, UserGlobal, Global);
        };

        void Add(TFileView dir, EIncDirScope scope);
        void PropagateTo(TLanguageIncDirs& other) const;

        TIterableCollections<TDirs> Get() const;

        void Load(const TSavedState& from, const TSymbols& symbols);
        void Save(TSavedState& to) const;
    };

    using TSavedState = THashMap<ui32, TLanguageIncDirs::TSavedState>;

    static const constexpr TLangId C_LANG = static_cast<TLangId>(0);
    static const constexpr TLangId BAD_LANG = static_cast<TLangId>(std::numeric_limits<ui32>::max());
    static const constexpr TStringBuf VAR_PREFIX = "_";
    static const constexpr TStringBuf VAR_SUFFIX = "__INCLUDE";

private:
    TSymbols& Symbols;
    THashMap<TLangId, TLanguageIncDirs> IncDirsByLang;
    mutable TVector<bool> UsedLanguages;

public:
    explicit TModuleIncDirs(TSymbols& symbols);

    void Add(TFileView dir, EIncDirScope scope, TLangId langId = C_LANG);
    void Add(TStringBuf dir, EIncDirScope scope, TLangId langId = C_LANG);

    void MarkLanguageAsUsed(TLangId langId) const;

    TIterableCollections<TDirs> Get(TLangId langId = C_LANG) const;

    TVector<TFileView> GetOwned(TLangId langId = C_LANG) const;
    TArrayRef<const TFileView> GetGlobal(TLangId langId = C_LANG) const;

    const decltype(IncDirsByLang)& GetAll() const;

    const decltype(UsedLanguages)& GetAllUsed() const;

    void PropagateTo(TModuleIncDirs& other) const;

    void Load(const TSavedState& from);

    void Save(TSavedState& to) const;

    void Dump(IOutputStream& out) const;

    void DumpJson(NJsonWriter::TBuf& json) const;

    static const TString& GetIncludeVarName(TLangId langId);

private:
    TIterableCollections<TDirs> GetByLang(TLangId langId) const;
};
