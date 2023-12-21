#pragma once

#include "evaluation_common.h"

#include <span>

namespace NCommands {

    TTermValue RenderClear(std::span<const TTermValue> args);
    TTermValue RenderPre(std::span<const TTermValue> args);
    TTermValue RenderSuf(std::span<const TTermValue> args);
    TTermValue RenderQuo(std::span<const TTermValue> args);
    void RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args);
    TTermValue RenderCutExt(std::span<const TTermValue> args);
    TTermValue RenderLastExt(std::span<const TTermValue> args);
    TTermValue RenderExtFilter(std::span<const TTermValue> args);
    TTermValue RenderTODO1(std::span<const TTermValue> args);
    TTermValue RenderTODO2(std::span<const TTermValue> args);
    TTermValue RenderMsvsSource(ICommandSequenceWriter* writer, std::span<const TTermValue> args);

}
