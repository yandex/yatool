#pragma once

#include <string_view>
#include <string>

namespace NYexport {

    inline const char ATTR_DIVIDER = '-'; // Divider char for tree of attributes
    inline const std::string LIST_ITEM_TYPE = std::string(1, ATTR_DIVIDER) + "ITEM"; // Magic suffix for set list item type

    class TFlatAttribute {
    public:
        class TBottomUpIterator {
        public:
            bool operator==(const TBottomUpIterator& other);
            bool operator!=(const TBottomUpIterator& other);
            const std::string_view& operator*() const;
            const std::string_view* operator->() const;

            TBottomUpIterator& operator++();

        private:
            TBottomUpIterator(const TFlatAttribute* attr, std::string_view state);

        private:
            friend class TFlatAttribute;

            const TFlatAttribute* Attribute_;
            std::string_view Value_;
        };

        class TBottomUpRange {
        public:
            TBottomUpIterator begin() const {
                return Begin_;
            }
            TBottomUpIterator end() const {
                return End_;
            }

        private:
            friend class TFlatAttribute;

            TBottomUpRange(TBottomUpIterator begin, TBottomUpIterator end)
                : Begin_(begin)
                , End_(end)
            {
            }
            TBottomUpIterator Begin_;
            TBottomUpIterator End_;
        };

        TFlatAttribute(const std::string& attribute, char divider = ATTR_DIVIDER);

        TBottomUpRange BottomUpRange() const;
        TBottomUpIterator BottomUpBegin() const;
        TBottomUpIterator BottomUpEnd() const;

        operator const std::string&() const;

    private:
        const std::string Attribute_;
        char Divider_;
    };

}
