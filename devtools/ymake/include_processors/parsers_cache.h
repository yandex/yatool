#pragma once

#include "parser_id.h"

#include <devtools/ymake/include_parsers/base.h>
#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/include_parsers/includes_parser_type.h>
#include <devtools/ymake/include_parsers/ros_parser.h>
#include <devtools/ymake/symbols/name_store.h>

#include <util/generic/hash.h>
#include <util/generic/variant.h>
#include <util/generic/vector.h>
#include <util/generic/ylimits.h>
#include <util/system/types.h>

#include <functional>

namespace NParsersCache {
    struct TIncludeSavedState {
        ui32 PathId;
        union {
            ui8 AllFlags = 0;
            struct {
                bool IsNative : 1;
                bool IsInduced : 1;
                bool IsImport : 1;
            };
        };

        Y_SAVELOAD_DEFINE(PathId, AllFlags);
    };

    struct TCythonIncludeSavedState {
        ui32 PathId;
        TCythonDep::EKind Kind;
        TVector<ui32> ImportList;

        Y_SAVELOAD_DEFINE(PathId, Kind, ImportList);
    };

    struct TRosIncludeSavedState {
        ui32 PackageNameId;
        ui32 MessageNameId;

        Y_SAVELOAD_DEFINE(PackageNameId, MessageNameId);
    };

    using TParseResult = std::variant<TVector<TIncludeSavedState>, TVector<TCythonIncludeSavedState>, TVector<TRosIncludeSavedState>>;

    template <typename TDst, typename TSrc>
    TDst Convert(TNameStore& names, const TSrc& from);

    template <typename TDst, typename TSrc>
    TVector<TDst> Convert(TNameStore& names, const TVector<TSrc>& from) {
        TVector<TDst> result;
        result.reserve(from.size());
        for (const auto& item : from) {
            result.push_back(Convert<TDst>(names, item));
        }
        return result;
    }

    ui64 GetResultId(TParserId parserId, ui32 fileId);
    ui32 GetFileIdFromResultId(ui64 resultId);
    TParserId GetParserIdFromResultId(ui64 resultId);
}

class TParsersCache {
private:
    TNameStore Names;
    THashMap<ui64, NParsersCache::TParseResult> ResultsMap;
    std::function<TParserId(EIncludesParserType)> ParserTypeToParserIdMapper;

public:
    static constexpr const TParserId BAD_PARSER_ID = TParserId{Max<ui32>()};

public:
    template <typename TParsedInclude>
    void Add(ui64 resultId, const TVector<TParsedInclude>& includes) {
        ResultsMap[resultId] = NParsersCache::Convert<NParsersCache::TIncludeSavedState>(Names, includes);
    }

    void Add(ui64 resultId, const TVector<TCythonDep>& includes) {
        ResultsMap[resultId] = NParsersCache::Convert<NParsersCache::TCythonIncludeSavedState>(Names, includes);
    }

    void Add(ui64 resultId, const TVector<TRosDep>& includes) {
        ResultsMap[resultId] = NParsersCache::Convert<NParsersCache::TRosIncludeSavedState>(Names, includes);
    }

    void Add(ui64 resultId, const NParsersCache::TParseResult& includes) {
        ResultsMap[resultId] = includes;
    }

    template <typename TParsedInclude>
    bool Get(ui64 resultId, TVector<TParsedInclude>& result)  {
        auto it = ResultsMap.find(resultId);
        if (it == ResultsMap.end()) {
            return false;
        }
        const auto& cached = std::get<TVector<NParsersCache::TIncludeSavedState>>(it->second);
        result = NParsersCache::Convert<TParsedInclude>(Names, cached);
        return true;
    }

    bool Get(ui64 resultId, TVector<TCythonDep>& result)  {
        auto it = ResultsMap.find(resultId);
        if (it == ResultsMap.end()) {
            return false;
        }
        const auto& cached = std::get<TVector<NParsersCache::TCythonIncludeSavedState>>(it->second);
        result = NParsersCache::Convert<TCythonDep>(Names, cached);
        return true;
    }

    bool Get(ui64 resultId, TVector<TRosDep>& result) {
        auto it = ResultsMap.find(resultId);
        if (it == ResultsMap.end()) {
            return false;
        }

        const auto& cached = std::get<TVector<NParsersCache::TRosIncludeSavedState>>(it->second);

        result = NParsersCache::Convert<TRosDep>(Names, cached);

        return true;
    }

    EIncludesParserType GetParserType(ui32 fileId) const;

    void SetParserTypeToParserIdMapper(std::function<TParserId(EIncludesParserType)>&& mapper) {
        ParserTypeToParserIdMapper = std::move(mapper);
    }

    void Save(TMultiBlobBuilder& builder);

    void Load(TBlob& multi);

    void RemapKeys(std::function<ui64(ui64)> keysConvertor);

    void Clear();
};
