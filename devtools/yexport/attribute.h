#pragma once

#include <util/generic/hash.h>
#include <util/system/types.h>
#include <util/generic/vector.h>

#include <string_view>
#include <string>

namespace NYexport {

    inline const char ATTR_DIVIDER = '-';        // Divider char for tree of attributes
    inline const std::string ITEM_TYPE = "ITEM"; // Magic suffix for set list item type
    inline const std::string ITEM_SUFFIX = ATTR_DIVIDER + ITEM_TYPE;

    class TAttr {
    public:
        TAttr() = default;
        TAttr(const char* attrName);
        TAttr(const std::string& attrName);
        TAttr(std::string_view attrName);

        bool IsItem() const;
        size_t Size() const;
        std::string_view GetPart(size_t i) const;
        std::string_view GetFirstParts(size_t i) const;

        std::string_view GetUpperPart() const;
        std::string_view GetBottomPart() const;

        operator std::string() const;
        const std::string& str() const;

        bool operator==(const TAttr& other) const = default;
        bool operator!=(const TAttr& other) const = default;

    private:
        std::string_view GetPart(size_t from, size_t to) const;

        const std::string Attr_;
        TVector<size_t> Dividers_;
    };

}

template <>
struct THash<NYexport::TAttr>: public THash<std::string> {
    size_t operator()(const NYexport::TAttr& val) const {
        return static_cast<const THash<std::string>&>(*this)(val.str());
    }
};

template <>
struct TEqualTo<NYexport::TAttr> {
    template <class TOther>
    inline bool operator()(const NYexport::TAttr& a, const TOther& b) const {
        return a == b;
    }
};
