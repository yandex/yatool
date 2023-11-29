#include "addincls.h"

#include "parser_manager.h"

#include <util/generic/algorithm.h>
#include <util/generic/typetraits.h>
#include <util/string/vector.h>

#include <functional>

template <>
void Out<TModuleIncDirs::TLanguageIncDirs>(IOutputStream& out, TTypeTraits<TModuleIncDirs::TLanguageIncDirs>::TFuncParam v) {
    auto incDirsRepr = [](const auto& dirs) {
        return dirs ? JoinStrings(dirs->begin(), dirs->end(), " ") : TString();
    };

    auto write = [&](const char* description, const auto& dirs) {
        out << "\t\t" << description << ": " << incDirsRepr(dirs) << '\n';
    };

    write("LocalUserGlobal", v.LocalUserGlobal);
    write("UserGlobal", v.UserGlobal);
    write("Global", v.Global);

    write("UserGlobalPropagated", v.UserGlobalPropagated);
    write("GlobalPropagated", v.GlobalPropagated);
}

void TModuleIncDirs::TLanguageIncDirs::Add(TFileView dir, EIncDirScope scope) {
    GetOrInit(LocalUserGlobal).Push(dir);

    if (scope == EIncDirScope::User || scope == EIncDirScope::Global) {
        GetOrInit(UserGlobal).Push(dir);
    }

    if (scope == EIncDirScope::Global) {
        GetOrInit(Global).Push(dir);
    }
}

void TModuleIncDirs::TLanguageIncDirs::PropagateTo(TModuleIncDirs::TLanguageIncDirs& other) const {
    auto propagate_dirs = [](const auto& src, auto& dst) {
        if (src) {
            src->AddTo(GetOrInit(dst));
        }
    };

    propagate_dirs(UserGlobal, other.UserGlobalPropagated);
    propagate_dirs(GlobalPropagated, other.UserGlobalPropagated);

    propagate_dirs(Global, other.GlobalPropagated);
    propagate_dirs(GlobalPropagated, other.GlobalPropagated);
}

TIterableCollections<TDirs> TModuleIncDirs::TLanguageIncDirs::Get() const {
    TIterableCollections<TDirs> result;
    if (LocalUserGlobal) {
        result.Append(LocalUserGlobal.Get());
    }
    if (UserGlobalPropagated) {
        result.Append(UserGlobalPropagated.Get());
    }
    return result;
}

void TModuleIncDirs::TLanguageIncDirs::Load(const TModuleIncDirs::TLanguageIncDirs::TSavedState& from, const TSymbols& symbols) {
    auto loadDirs = [&](const auto& src, auto& dst) {
        if (src) {
            GetOrInit(dst).RestoreFromsIds(src, symbols);
        }
    };

    loadDirs(from.LocalUserGlobal, LocalUserGlobal);
    loadDirs(from.UserGlobal, UserGlobal);
    loadDirs(from.Global, Global);
}

void TModuleIncDirs::TLanguageIncDirs::Save(TModuleIncDirs::TLanguageIncDirs::TSavedState& to) const {
    auto saveDirs = [](const auto& src, auto& dst) {
        if (src) {
            dst = src->SaveAsIds();
        }
    };

    saveDirs(LocalUserGlobal, to.LocalUserGlobal);
    saveDirs(UserGlobal, to.UserGlobal);
    saveDirs(Global, to.Global);
}

TModuleIncDirs::TModuleIncDirs(TSymbols& symbols)
    : Symbols(symbols)
    , UsedLanguages(NLanguages::LanguagesCount(), false)
{
}

void TModuleIncDirs::Add(TFileView dir, EIncDirScope scope, TLangId langId) {
    IncDirsByLang[langId].Add(dir, scope);
}

void TModuleIncDirs::Add(TStringBuf dir, EIncDirScope scope, TLangId langId) {
    ui32 id = Symbols.AddName(EMNT_File, dir);
    Add(Symbols.FileNameById(id), scope, langId);
}

void TModuleIncDirs::MarkLanguageAsUsed(TLangId langId) const {
    UsedLanguages[static_cast<size_t>(langId)] = true;
}

TIterableCollections<TDirs> TModuleIncDirs::GetByLang(TLangId langId) const {
    const auto it = IncDirsByLang.find(langId);
    return it == IncDirsByLang.end() ? TIterableCollections<TDirs>() : it->second.Get();
}

TIterableCollections<TDirs> TModuleIncDirs::Get(TLangId langId) const {
    return GetByLang(langId);
}

TVector<TFileView> TModuleIncDirs::GetOwned(TLangId langId) const {
    const auto it = IncDirsByLang.find(langId);
    if (it == IncDirsByLang.end()) {
        return {};
    }
    const auto& langDirs = it->second;

    // Нужен список только локальных include directories. Получаем его, вычитая UserGlobal из LocalUserGlobal.
    if (!langDirs.LocalUserGlobal) {
        return {};
    }
    if (!langDirs.UserGlobal) {
        return {langDirs.LocalUserGlobal->begin(), langDirs.LocalUserGlobal->end()};
    }

    TVector<TFileView> local;
    for (const TFileView& dir : *langDirs.LocalUserGlobal) {
        if (!langDirs.UserGlobal->has(dir)) {
            local.push_back(dir);
        }
    }
    return local;
}

TArrayRef<const TFileView> TModuleIncDirs::GetGlobal(TLangId langId) const {
    const auto it = IncDirsByLang.find(langId);
    if (it == IncDirsByLang.end() || !it->second.UserGlobal) {
        return {};
    }
    return it->second.UserGlobal->Data();
}

const decltype(TModuleIncDirs::IncDirsByLang)& TModuleIncDirs::GetAll() const {
    return IncDirsByLang;
}

const decltype(TModuleIncDirs::UsedLanguages)& TModuleIncDirs::GetAllUsed() const {
    return UsedLanguages;
}

void TModuleIncDirs::PropagateTo(TModuleIncDirs& other) const {
    for (const auto& [lang, dirs] : IncDirsByLang) {
        dirs.PropagateTo(other.IncDirsByLang[lang]);
    }
}

void TModuleIncDirs::Load(const TModuleIncDirs::TSavedState& from) {
    for (const auto& [langId, dirs] : from) {
        auto [it, fresh] = IncDirsByLang.try_emplace(static_cast<TLangId>(langId));
        Y_ASSERT(fresh);
        it->second.Load(dirs, Symbols);
    }
}

void TModuleIncDirs::Save(TModuleIncDirs::TSavedState& to) const {
    for (const auto& [langId, dirs] : IncDirsByLang) {
        dirs.Save(to[static_cast<ui32>(langId)]);
    }
}

void TModuleIncDirs::Dump(IOutputStream& out) const {
    TVector<std::reference_wrapper<const decltype(IncDirsByLang)::value_type>> sorted(IncDirsByLang.begin(), IncDirsByLang.end());
    SortBy(sorted, [](const auto& element) {
        return element.get().first;
    });
    for (const auto& it : sorted) {
        out << "\tIncDirs for " << NLanguages::GetLanguageName(it.get().first) << ":\n" << it.get().second;
    }
}

void TModuleIncDirs::DumpJson(NJsonWriter::TBuf& json) const {
    TVector<std::reference_wrapper<const decltype(IncDirsByLang)::value_type>> sorted(IncDirsByLang.begin(), IncDirsByLang.end());
    SortBy(sorted, [](const auto& element) {
        return element.get().first;
    });
    json.WriteKey("inc-dirs");
    json.BeginObject();
    for (const auto& it : sorted) {
        json.WriteKey(NLanguages::GetLanguageName(it.get().first));
        json.BeginList();
        for (const auto dir : it.get().second.Get()) {
            json.WriteString(dir.GetTargetStr());
        }
        json.EndList();
    }
    json.EndObject();
}

const TString& TModuleIncDirs::GetIncludeVarName(TLangId langId) {
    Y_ASSERT(langId != BAD_LANG);
    return NLanguages::GetLanguageIncludeName(langId);
}
