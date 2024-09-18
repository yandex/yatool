#pragma once

#include <devtools/ymake/include_parsers/includes_parser_type.h>

#include <util/generic/bitops.h>
#include <util/system/types.h>

class TParserId {
public:
    using TIdType = ui32;

    explicit constexpr TParserId(TIdType id = 0)
        : Id(id)
    {
        static_assert(0 < CodeBitsSize, "violated: CodeBitsSize > 0");
        static_assert(CodeBitsSize < sizeof(TIdType) * 8, "violated: CodeBitsSize < sizeof(TIdType) * 8");
    }

    auto operator<=>(const TParserId&) const = default;

    TIdType GetId() const {
        return Id;
    }
    void SetType(EIncludesParserType type) {
        SetBits<0, CodeBitsSize, TIdType>(Id, static_cast<ui32>(type));
    }
    EIncludesParserType GetType() const {
        ui32 id = SelectBits<0, CodeBitsSize, TIdType>(Id);
        return static_cast<EIncludesParserType>(id);
    }
    void SetVersion(ui32 version) {
        SetBits<CodeBitsSize, VersionBitsSize, TIdType>(Id, version);
    }
    ui32 GetVersion() const {
        return SelectBits<CodeBitsSize, VersionBitsSize, TIdType>(Id);
    }

private:
    static constexpr size_t CodeBitsSize = 16;
    static constexpr size_t VersionBitsSize = sizeof(TIdType) * 8 - CodeBitsSize;

    ui32 Id;
};
