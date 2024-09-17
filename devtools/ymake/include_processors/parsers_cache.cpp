#include "parsers_cache.h"

#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/include_parsers/go_parser.h>
#include <devtools/ymake/include_parsers/incldep.h>
#include <devtools/ymake/include_parsers/ragel_parser.h>
#include <devtools/ymake/include_parsers/ros_parser.h>

namespace NParsersCache {
    template <>
    TString Convert(TNameStore& names, const TIncludeSavedState& saved) {
        return TString(names.GetName<TFileView>(saved.PathId).GetTargetStr());
    }

    template <>
    TRagelInclude Convert(TNameStore& names, const TIncludeSavedState& saved) {
        return {TString(names.GetName<TFileView>(saved.PathId).GetTargetStr()), saved.IsNative ? TRagelInclude::EKind::Native : TRagelInclude::EKind::Cpp};
    }

    template <>
    TInclDep Convert(TNameStore& names, const TIncludeSavedState& saved) {
        return {TString(names.GetName<TFileView>(saved.PathId).GetTargetStr()), saved.IsInduced};
    }

    template <>
    TParsedFile Convert(TNameStore& names, const TIncludeSavedState& saved) {
        return {TString(names.GetName<TFileView>(saved.PathId).GetTargetStr()), saved.IsImport ? TParsedFile::EKind::Import : TParsedFile::EKind::Include};
    }

    template <>
    TCythonDep Convert(TNameStore& names, const TCythonIncludeSavedState& saved) {
        TCythonDep restored(names.GetName<TFileView>(saved.PathId).GetTargetStr(), saved.Kind);
        restored.List.reserve(saved.ImportList.size());
        for (const auto& id : saved.ImportList) {
            restored.List.push_back(TString(names.GetName<TFileView>(id).GetTargetStr()));
        }
        return restored;
    }

    template <>
    TRosDep Convert(TNameStore& names, const TRosIncludeSavedState& saved) {
        return {
            TString(names.GetName<TFileView>(saved.PackageNameId).GetTargetStr()),
            TString(names.GetName<TFileView>(saved.MessageNameId).GetTargetStr()),
        };
    }

    template <>
    TIncludeSavedState Convert(TNameStore& names, const TString& parsed) {
        TIncludeSavedState saved;
        saved.PathId = names.Add(parsed);
        return saved;
    }

    template <>
    TIncludeSavedState Convert(TNameStore& names, const TRagelInclude& parsed) {
        TIncludeSavedState saved;
        saved.PathId = names.Add(parsed.Include);
        saved.IsNative = parsed.Kind == TRagelInclude::EKind::Native;
        return saved;
    }

    template <>
    TIncludeSavedState Convert(TNameStore& names, const TInclDep& parsed) {
        TIncludeSavedState saved;
        saved.PathId = names.Add(parsed.Path);
        saved.IsInduced = parsed.IsInduced;
        return saved;
    }

    template <>
    TIncludeSavedState Convert(TNameStore& names, const TParsedFile& parsed) {
        TIncludeSavedState saved;
        saved.PathId = names.Add(parsed.ParsedFile);
        saved.IsImport = parsed.Kind == TParsedFile::EKind::Import;
        return saved;
    }

    template <>
    TCythonIncludeSavedState Convert(TNameStore& names, const TCythonDep& parsed) {
        TCythonIncludeSavedState saved;
        saved.PathId = names.Add(parsed.Path);
        saved.Kind = parsed.Kind;
        saved.ImportList.reserve(saved.ImportList.size());
        for (const auto& str : parsed.List) {
            saved.ImportList.push_back(names.Add(str));
        }
        return saved;
    }

    template <>
    TRosIncludeSavedState Convert(TNameStore& names, const TRosDep& parsed) {
        TRosIncludeSavedState saved;
        saved.PackageNameId = names.Add(parsed.PackageName);
        saved.MessageNameId = names.Add(parsed.MessageName);
        return saved;
    }

    ui64 GetResultId(ui32 parserId, ui32 fileId) {
        return static_cast<ui64>(parserId) << 32 | fileId;
    }

    ui32 GetFileIdFromResultId(ui64 resultId) {
        return static_cast<ui32>(resultId);
    }

    ui32 GetParserIdFromResultId(ui64 resultId) {
        return static_cast<ui32>(resultId >> 32);
    }
}

EIncludesParserType TParsersCache::GetParserType(ui32 fileId) const {
    ui32 parserTypesCount = static_cast<ui32>(EIncludesParserType::PARSERS_COUNT);
    for (ui32 i = 0; i < parserTypesCount; i++) {
        EIncludesParserType parserType = static_cast<EIncludesParserType>(i);
        ui32 parserId = ParserTypeToParserIdMapper(parserType);
        if (parserId != BAD_PARSER_ID && ResultsMap.contains(NParsersCache::GetResultId(parserId, fileId))) {
            return parserType;
        }
    }
    return EIncludesParserType::BAD_PARSER;
}

void TParsersCache::Save(TMultiBlobBuilder& builder) {
    auto multi = MakeHolder<TMultiBlobBuilder>();
    Names.Save(*multi.Get());
    builder.AddBlob(multi.Release());

    TBuffer buffer;
    {
        TBufferOutput output(buffer);
        TMapSerializer<decltype(ResultsMap)>::Save(&output, ResultsMap);
    }
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
}

void TParsersCache::Load(TBlob& multi) {
    TSubBlobs blob(multi);
    Names.Load(blob[0]);
    TMemoryInput input(blob[1].Data(), blob[1].Size());
    TMapSerializer<decltype(ResultsMap)>::Load(&input, ResultsMap);
}

void TParsersCache::RemapKeys(std::function<ui64(ui64)> keysConvertor) {
    decltype(ResultsMap) newResultsMap;
    for (auto& [id, data] : ResultsMap) {
        auto newId = keysConvertor(id);
        newResultsMap[newId] = std::move(data);
    }
    ResultsMap = std::move(newResultsMap);
}

void TParsersCache::Clear() {
    ResultsMap.clear();
    Names.Clear();
}
