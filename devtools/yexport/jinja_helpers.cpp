#include "jinja_helpers.h"

#include <util/string/builder.h>

namespace NYexport {
    void Dump(IOutputStream& out, const jinja2::Value& value, int depth, bool isLastItem) {
        auto Indent = [](int depth) {
            return std::string(depth * 4, ' ');
        };
        if (value.isMap()) {
            if (!depth) out << Indent(depth);
            out << "{\n";
            const auto& map = value.asMap();
            if (!map.empty()) {
                // Sort map keys before dumping
                TVector<std::string> keys;
                keys.reserve(map.size());
                for (const auto& [key, _] : map) {
                    keys.emplace_back(key);
                }
                Sort(keys);
                for (const auto& key : keys) {
                    out << Indent(depth + 1) << key << ": ";
                    const auto it = map.find(key);
                    Dump(out, it->second, depth + 1, &key == &keys.back());
                }
            }
            out << Indent(depth) << "}";
        } else if (value.isList()) {
            out << "[\n";
            const auto& vals = value.asList();
            for (const auto& val : vals) {
                out << Indent(depth + 1);
                Dump(out, val, depth + 1, &val == &vals.back());
            }
            out << Indent(depth) << "]";
        }  else if (value.isString()) {
            out << '"' << value.asString() << '"';
        } else if (value.isEmpty()) {
            out << "EMPTY";
        } else {
            if (std::holds_alternative<bool>(value.data())) {
                out << (value.get<bool>() ? "true" : "false");
            } else if (std::holds_alternative<int64_t>(value.data())) {
                out << value.get<int64_t>();
            } else if (std::holds_alternative<double>(value.data())) {
                out << value.get<double>();
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
