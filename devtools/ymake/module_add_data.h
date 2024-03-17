#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>

#include <util/generic/ptr.h>

struct TCommandInfo;

/// @brief Structure capturing properties of Added nodes w.r.t to their modules
///        For module nodes also contains output file nodes loaded from cached graph

enum class EInputsStatus {
    EIS_NotChecked,
    EIS_NotChanged,
    EIS_Changed
};

struct TModAddData {
    TAutoPtr<TCommandInfo> CmdInfo;
    THolder<THashSet<ui32>> ParsedPeerdirs;

    union {
        ui8 AllFlags = 0;
        struct {  // 6 bits used
            bool UsedAsInput : 1;
            bool CheckIfUsed : 1;
            bool BadCmdInput : 1;
            bool AdditionalOutput : 1;
            bool Added : 1;
        };
    };

    EInputsStatus InputsStatus = EInputsStatus::EIS_NotChecked;

    TModAddData() = default;
    TModAddData(TModAddData&&) = default;
    TModAddData(const TModAddData&);

    ~TModAddData();

    bool IsParsedPeer(ui32 elemId) const;
};
