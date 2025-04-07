#include "jinja_helpers.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/generic_list_iterator.h>

#include <util/string/builder.h>

#include <spdlog/spdlog.h>

namespace NYexport {
    void MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree) {
        for (const auto& [attrName, attrValue]: tree) {
            if (attrValue.isMap()) {
                auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
                MergeTree(attrIt->second.asMap(), attrValue.asMap());
            } else {
                if (attrs.contains(attrName)) {
                    spdlog::warn("overwrite dict element {}", attrName);
                }
                attrs[attrName] = attrValue;
            }
        }
    }

    void Dump(IOutputStream& out, const jinja2::Value& value, int depth, bool isLastItem) {
        auto Indent = [](int depth) {
            return std::string(depth * 4, ' ');
        };
        if (value.isMap()) {
            if (!depth) out << Indent(depth);
            out << "{\n";
            const auto* genMap = value.getPtr<jinja2::GenericMap>();
            TVector<std::string> keys;
            if (genMap) {// workaround for GenericMap, asMap generate bad_variant_access for it
                auto genkeys = genMap->GetKeys();
                keys.insert(keys.end(), genkeys.begin(), genkeys.end());
            } else {
                const auto& map = value.asMap();
                for (const auto& [key, _] : map) {
                    keys.emplace_back(key);
                }
            }
            if (!keys.empty()) {
                // Sort map keys before dumping
                Sort(keys);
                for (const auto& key : keys) {
                    out << Indent(depth + 1) << key << ": ";
                    if (genMap) {
                        Dump(out, genMap->GetValueByName(key), depth + 1, &key == &keys.back());
                    } else {
                        Dump(out, value.asMap().at(key), depth + 1, &key == &keys.back());
                    }
                }
            }
            out << Indent(depth) << "}";
        } else if (value.isList()) {
            out << "[\n";
            const auto* genList = value.getPtr<jinja2::GenericList>();
            if (genList) {// workaround for GenericList
                auto size = genList->GetSize();
                if (size.has_value()) {
                    size_t last = size.value();
                    size_t i = 0;
                    for(const auto& val: *genList) {
                        out << Indent(depth + 1);
                        Dump(out, val, depth + 1, ++i == last);
                    }
                }
            } else {
                const auto& vals = value.asList();
                for (const auto& val : vals) {
                    out << Indent(depth + 1);
                    Dump(out, val, depth + 1, &val == &vals.back());
                }
            }
            out << Indent(depth) << "]";
        }  else if (value.isString()) {
            out << '"' << value.asString() << '"';
        }  else if (value.isWString()) {
            out << '"' << value.asWString() << "\"ws";
        } else if (value.isEmpty()) {
            out << "EMPTY";
        } else {
            if (std::holds_alternative<bool>(value.data())) {
                out << (value.get<bool>() ? "true" : "false");
            } else if (std::holds_alternative<int64_t>(value.data())) {
                out << value.get<int64_t>();
            } else if (std::holds_alternative<double>(value.data())) {
                out << value.get<double>();
            } else if (std::holds_alternative<std::string_view>(value.data())) {
                out << '"' << value.get<std::string_view>() << "\"sv";
            } else if (std::holds_alternative<std::wstring_view>(value.data())) {
                out << '"' << value.get<std::wstring_view>() << "\"wsv";
            } else {
                out << "???";
            }
        }
        if (!isLastItem) {
            out << ",";
        }
        out << "\n";
    }

    std::string Dump(const jinja2::Value& value, int depth) {
        TStringBuilder strBuilder;
        Dump(strBuilder.Out, value, depth);
        if (strBuilder.empty()) {
            return {};
        }
        return strBuilder;
    }


    jinja2::Value ParseValue(const toml::value& value) {
        if (value.is_table()) {
            return ParseTable(value.as_table());
        } else if (value.is_array()) {
            return ParseArray(value.as_array());
        } else if (value.is_string()) {
            return value.as_string();
        } else if (value.is_floating()) {
            return value.as_floating();
        } else if (value.is_integer()) {
            return value.as_integer();
        } else if (value.is_boolean()) {
            return value.as_boolean();
        } else if (value.is_local_date() || value.is_local_datetime() || value.is_offset_datetime()) {
            return value.as_string();
        } else {
            return {};
        }
    }

    jinja2::ValuesMap ParseTable(const toml::table& table) {
        jinja2::ValuesMap map;
        for (const auto& [key, value] : table) {
            map.emplace(key, ParseValue(value));
        }
        return map;
    }

    jinja2::ValuesList ParseArray(const toml::array& array) {
        jinja2::ValuesList list;
        for (const auto& value : array) {
            list.emplace_back(ParseValue(value));
        }
        return list;
    }

}
