#include "module_wrapper.h"

#include "ymake.h"
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/include_processors/include.h>

TString TModuleWrapper::ResolveToAbsPath(TStringBuf path) {
    if (!NPath::IsTypedPath(path)) {
        return TString{path};
    }
    return Conf.RealPathEx(path);
}

TString TModuleWrapper::ResolveToArcPath(TStringBuf path, bool force) {
    if (NPath::IsTypedPath(path)) {
        return TString{path};
    }
    auto resolveFile = ResolveSourcePath(path, Module.GetDir(), force ? TModuleResolver::LastTry : TModuleResolver::Default);
    return GetStr(resolveFile);
}

void TModuleWrapper::ResolveInclude(TStringBuf src, const TVector<TStringBuf>& includes, TVector<TString>& result) {
    TVector<TResolveFile> resultViews;
    ResolveLocalIncludes(AddSrcToSymbols(src), includes, resultViews);
    if (!resultViews.empty()) {
        result.reserve(resultViews.size());
        for (auto& resultView: resultViews) {
            result.emplace_back(GetStr(resultView));
        }
    }
}
