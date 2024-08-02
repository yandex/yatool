#pragma once

#include "evaluation_common.h"

#include <span>

namespace NCommands {

    TTermValue RenderArgs(std::span<const TTermValue> args);
    TTermValue RenderTerms(std::span<const TTermValue> args);
    TTermValue RenderCat(std::span<const TTermValue> args);
    TTermValue RenderClear(std::span<const TTermValue> args);
    TTermValue RenderPre(std::span<const TTermValue> args);
    TTermValue RenderSuf(std::span<const TTermValue> args);
    TTermValue RenderJoin(std::span<const TTermValue> args);
    TTermValue RenderQuo(std::span<const TTermValue> args);
    TTermValue RenderQuoteEach(std::span<const TTermValue> args);
    TTermValue RenderToUpper(std::span<const TTermValue> args);
    void RenderCwd(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderStdout(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderLateOut(const TEvalCtx& ctx, std::span<const TTermValue> args);
    TTermValue RenderRootRel(std::span<const TTermValue> args);
    TTermValue RenderCutPath(std::span<const TTermValue> args);
    TTermValue RenderCutExt(std::span<const TTermValue> args);
    TTermValue RenderLastExt(std::span<const TTermValue> args);
    TTermValue RenderExtFilter(std::span<const TTermValue> args);
    TTermValue RenderTODO1(std::span<const TTermValue> args);
    TTermValue RenderTODO2(std::span<const TTermValue> args);

}
