
#include "attribute.h"
#include "diag/exception.h"

namespace NYexport {

    TAttribute::TAttribute(const char* attribute)
        : TAttribute(std::string_view(attribute))
    {
    }

    TAttribute::TAttribute(const std::string& attribute)
        : TAttribute(std::string_view(attribute))
    {
    }

    TAttribute::TAttribute(std::string_view attribute)
        : Attribute_(attribute)
    {
        YEXPORT_VERIFY(!attribute.empty(), "Cannot create empty attribute");
        std::string_view attr = attribute;
        size_t startPos = 0;
        size_t dividerPos = 0;
        while ((dividerPos = attr.find(ATTR_DIVIDER, startPos)) != std::string::npos) {
            YEXPORT_VERIFY(startPos != dividerPos, "Attribute contains empty part: " << attribute);
            Dividers_.push_back(dividerPos);
            startPos = dividerPos + 1;
        }
        YEXPORT_VERIFY(startPos != attribute.size(), "Attribute contains empty part: " << attribute);
    }


    bool TAttribute::IsItem() const {
        return Size() > 1 && GetPart(Size() - 1) == ITEM_TYPE;
    }

    size_t TAttribute::Size() const {
        return Dividers_.size() + 1;
    }

    std::string_view TAttribute::GetPart(size_t i) const {
        YEXPORT_VERIFY(i < Size(), "Cannot get attribute part " << i << " because size is " << Size());
        size_t startPos = (i == 0 ? 0 : Dividers_[i - 1] + 1);
        size_t endPos = (i + 1 == Size() ? std::string::npos : Dividers_[i]);
        return GetPart(startPos, endPos);
    }

    std::string_view TAttribute::GetFirstParts(size_t i) const {
        YEXPORT_VERIFY(i < Size(), "Cannot get attribute part " << i << " because size is " << Size());
        size_t endPos = (i + 1 == Size() ? std::string::npos : Dividers_[i]);
        return GetPart(0, endPos);
    }

    std::string_view TAttribute::GetUpperPart() const {
        return GetPart(0);
    }

    std::string_view TAttribute::GetBottomPart() const {
        return GetPart(Size() - 1);
    }


    TAttribute::operator std::string() const {
        return Attribute_;
    }

    const std::string& TAttribute::str() const {
        return Attribute_;
    }

    std::string_view TAttribute::GetPart(size_t from, size_t to) const {
        std::string_view s = Attribute_;
        if (from == std::string::npos) {
            from = 0;
        }
        if (to == std::string::npos) {
            to = s.size();
        }
        Y_ASSERT(from <= to);
        Y_ASSERT(to <= s.size());

        s.remove_prefix(from);
        s.remove_suffix(Attribute_.size() - to);
        return s;
    }

}
