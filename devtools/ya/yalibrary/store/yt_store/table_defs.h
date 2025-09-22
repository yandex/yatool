#pragma once

#include <library/cpp/yson/node/node.h>

namespace NYa {
    constexpr TStringBuf YT_CACHE_METADATA_SCHEMA_V2 = R"***([
        {
            name = tablet_hash;
            expression = "farm_hash(uid)";
            type = uint64;
            sort_order = ascending;
        };
        {name = uid; type_v3 = string; sort_order = ascending};
        {name = chunks_count; type = uint64};
        {name = data_size; type = uint64};
        {name = access_time; type = timestamp};
        {name = hash; type = string};
        {name = name; type = string};
        {name = hostname; type = string};
        {name = GSID; type = string};
        {name = codec; type = string};
    ])***";

    constexpr TStringBuf YT_CACHE_METADATA_SCHEMA_V3 = R"***([
        {
            name = tablet_hash;
            expression = "farm_hash(self_uid)";
            type = uint64;
            sort_order = ascending;
        };
        {name = self_uid; type_v3 = string; sort_order = ascending};
        {name = uid; type_v3 = string; sort_order = ascending};
        {name = chunks_count; type = uint64};
        {name = data_size; type = uint64};
        {name = create_time; type = timestamp};
        {name = access_time; type = timestamp};
        {name = hash; type = string};
        {name = name; type = string};
        {name = hostname; type = string};
        {name = GSID; type = string};
        {name = codec; type = string};
    ])***";

    constexpr TStringBuf YT_CACHE_DATA_SCHEMA = R"***([
        {
            name = tablet_hash;
            expression = "farm_hash(hash)";
            type = uint64;
            sort_order = ascending;
            group = light;
        };
        {name = hash; type_v3 = string; sort_order = ascending; group = light};
        {name = chunk_i; type_v3 = uint64; sort_order = ascending; group = light};
        {name = create_time; type = timestamp; group = light};
        {name = data; type = string; group = heavy};
    ])***";

    constexpr TStringBuf YT_CACHE_STAT_SCHEMA = R"***([
        {
            name = tablet_hash;
            expression = "farm_hash(timestamp, salt)";
            type = uint64;
            sort_order = ascending;
        };
        {name = timestamp; type_v3 = timestamp; sort_order = ascending};
        {name = salt; type_v3 = uint64; sort_order = ascending};
        {name = key; type_v3 = string; sort_order = ascending};
        {name = value; type_v3 = {type_name = optional; item = yson}};
    ])***";

    constexpr std::array<TStringBuf, 4> YT_CACHE_METADATA_SCHEMAS = {
        "",
        "",
        YT_CACHE_METADATA_SCHEMA_V2,
        YT_CACHE_METADATA_SCHEMA_V3,
    };
}
