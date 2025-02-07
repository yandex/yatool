#pragma once

#include "generator_spec.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/value.h>
#include <contrib/libs/toml11/include/toml11/types.hpp>

#include <util/stream/output.h>

namespace NYexport {
    void MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree);

    void Dump(IOutputStream& out, const jinja2::Value& value, int depth = 0, bool isLastItem = true);
    std::string Dump(const jinja2::Value& value, int depth = 0);

    jinja2::Value ParseValue(const toml::value& value);
    jinja2::ValuesMap ParseTable(const toml::table& table);
    jinja2::ValuesList ParseArray(const toml::array& array);
}
