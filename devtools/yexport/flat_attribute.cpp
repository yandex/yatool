#include "flat_attribute.h"

namespace NYexport {

    bool TFlatAttribute::TBottomUpIterator::operator==(const TFlatAttribute::TBottomUpIterator& other) {
        return Attribute_ == other.Attribute_ && Value_ == other.Value_;
    }

    bool TFlatAttribute::TBottomUpIterator::operator!=(const TFlatAttribute::TBottomUpIterator& other) {
        return !(*this == other);
    }

    const std::string_view& TFlatAttribute::TBottomUpIterator::operator*() const {
        return Value_;
    }

    const std::string_view* TFlatAttribute::TBottomUpIterator::operator->() const {
        return &Value_;
    }

    TFlatAttribute::TBottomUpIterator& TFlatAttribute::TBottomUpIterator::operator++() {
        auto divPos = Value_.rfind(Attribute_->Divider_);
        if (divPos == std::string::npos) {
            Value_ = {};
        } else {
            Value_.remove_suffix(Value_.size() - divPos);
        }
        return *this;
    }

    TFlatAttribute::TBottomUpIterator::TBottomUpIterator(const TFlatAttribute* attr, std::string_view state) {
        Attribute_ = attr;
        Value_ = state;
    }

    TFlatAttribute::TFlatAttribute(const std::string& attribute, char divider)
        : Attribute_(attribute)
        , Divider_(divider)
    {
    }

    TFlatAttribute::TBottomUpRange TFlatAttribute::BottomUpRange() const {
        return TBottomUpRange(BottomUpBegin(), BottomUpEnd());
    }

    TFlatAttribute::TBottomUpIterator TFlatAttribute::BottomUpBegin() const {
        return TBottomUpIterator(this, Attribute_);
    }

    TFlatAttribute::TBottomUpIterator TFlatAttribute::BottomUpEnd() const {
        return TBottomUpIterator(this, {});
    }

    TFlatAttribute::operator const std::string&() const {
        return Attribute_;
    }

}
