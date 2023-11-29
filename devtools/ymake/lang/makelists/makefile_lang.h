#pragma once

#include "statement_location.h"

#include <devtools/ymake/yndex/yndex.h>

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

struct IMemoryPool;

using NYndex::TSourceRange;

class TVisitorCtx {
private:
    TStatementLocation Location;
    TStringBuf Content;

public:
    TVisitorCtx(const TStatementLocation& location, TStringBuf content);
    const TStatementLocation& GetLocation() const;

    TString Here(size_t context) const;
};

class ISimpleMakeListVisitor {
public:
    virtual ~ISimpleMakeListVisitor();

    virtual void Statement(const TStringBuf& command, TVector<TStringBuf>& args, const TVisitorCtx& ctx, const TSourceRange& range) = 0;

    virtual void Error(const TStringBuf& message, const TVisitorCtx& ctx) = 0;
};

void ReadMakeList(const TStringBuf& path, const TStringBuf& content, ISimpleMakeListVisitor* reader, IMemoryPool* pool);
