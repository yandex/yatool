#include "ymake.h"
#include <devtools/ymake/diag/trace.h>

#include <devtools/ymake/msvs/vcxproj.h>
#include <devtools/ymake/diag/display.h>

#include <util/folder/path.h>
#include <util/generic/strbuf.h>

using namespace NYMake;

namespace {

const TStringBuf DEFAULT_SOLUTION_NAME = "arcadia";

}

void TYMake::RenderMsvsSolution(size_t vsVersion, const TStringBuf& name, const TStringBuf& dir) {
    FORCE_TRACE(U, NEvent::TStageStarted("Export MSVS solution"));
    YInfo() << "Exporting Solution" << Endl;
    TFsPath solutionRoot = dir ? dir : Conf.BuildRoot;
    Conf.EnableRealPathCache(&Names.FileConf);
    NMsvs::TVcRender renderer(*this, solutionRoot, name ? name : DEFAULT_SOLUTION_NAME, vsVersion);
    TStringBuf msvsStr = "MSVS";
    TScopedContext context(0, msvsStr); //XXX: 0 is also used for TOP_LEVEL
    renderer.Render();
    Conf.EnableRealPathCache(nullptr);
    FORCE_TRACE(U, NEvent::TStageFinished("Export MSVS solution"));
}
