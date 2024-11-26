
#include "attribute.h"

#include <devtools/yexport/diag/exception.h>

namespace NYexport {

    TAttr::TAttr(const char* attrName)
        : TAttr(std::string_view(attrName))
    {}

    TAttr::TAttr(const std::string& attrName)
        : TAttr(std::string_view(attrName))
    {}

    TAttr::TAttr(std::string_view attrName)
        : Attr_(attrName)
    {
        YEXPORT_VERIFY(!attrName.empty(), "Cannot create empty attribute");
        size_t startPos = 0;
        size_t dividerPos = 0;
        while ((dividerPos = attrName.find(ATTR_DIVIDER, startPos)) != std::string::npos) {
            YEXPORT_VERIFY(startPos != dividerPos, "Attribute contains empty part: " << attrName);
            Dividers_.push_back(dividerPos);
            startPos = dividerPos + 1;
        }
        YEXPORT_VERIFY(startPos != attrName.size(), "Attribute contains empty part: " << attrName);
    }


    bool TAttr::IsItem() const {
        return Size() > 1 && GetPart(Size() - 1) == ITEM_TYPE;
    }

    size_t TAttr::Size() const {
        return Dividers_.size() + 1;
    }

    std::string_view TAttr::GetPart(size_t i) const {
        YEXPORT_VERIFY(i < Size(), "Cannot get attribute part " << i << " because size is " << Size());
        size_t startPos = (i == 0 ? 0 : Dividers_[i - 1] + 1);
        size_t endPos = (i + 1 == Size() ? std::string::npos : Dividers_[i]);
        return GetPart(startPos, endPos);
    }

    std::string_view TAttr::GetFirstParts(size_t i) const {
        YEXPORT_VERIFY(i < Size(), "Cannot get attribute part " << i << " because size is " << Size());
        size_t endPos = (i + 1 == Size() ? std::string::npos : Dividers_[i]);
        return GetPart(0, endPos);
    }

    std::string_view TAttr::GetUpperPart() const {
        return GetPart(0);
    }

    std::string_view TAttr::GetBottomPart() const {
        return GetPart(Size() - 1);
    }


    TAttr::operator std::string() const {
        return Attr_;
    }

    const std::string& TAttr::str() const {
        return Attr_;
    }

    std::string_view TAttr::GetPart(size_t from, size_t to) const {
        std::string_view s = Attr_;
        if (from == std::string::npos) {
            from = 0;
        }
        if (to == std::string::npos) {
            to = s.size();
        }
        Y_ASSERT(from <= to);
        Y_ASSERT(to <= s.size());

        s.remove_prefix(from);
        s.remove_suffix(Attr_.size() - to);
        return s;
    }

}
