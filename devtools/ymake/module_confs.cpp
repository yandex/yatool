#include "module_confs.h"

#include "module_state.h"

#include <devtools/ymake/options/static_options.h>

#include <devtools/ymake/diag/manager.h>

#include <util/string/util.h>

void ParseModuleArgsBase(TModule* mod, TArrayRef<const TStringBuf> args) {
    for (size_t n = 0; n < args.size(); n++) {
        if (args[n] == "PREFIX") {
            if (args.size() > n + 1) {
                mod->Vars.SetValue("MODULE_PREFIX", EvalExpr(mod->Vars, args[++n]));
            } else {
                YWarn() << "in " << mod->GetMakefile() << ", prefix not specified after PREFIX keyword" << Endl;
            }
            continue;
        }
        if (!mod->Vars.Contains("REALPRJNAME")) {
            mod->Vars.SetValue("REALPRJNAME", args[n]);
            continue;
        }
    }
}

void SetDirNameBasenameImpl(TModule* mod) {
    TFsPath path(mod->GetDir().CutType());
    TString target = path.Basename();
    mod->Vars.SetValue("REALPRJNAME", target);
}

void SetDirNameBasename(TModule* mod) {
    if (!mod->Vars.contains("REALPRJNAME")) {
        SetDirNameBasenameImpl(mod);
    }
}

void SetDirNameBasenameOrGoPackage(TModule* mod) {
    if (mod->Vars.contains("REALPRJNAME")) {
        mod->Vars.SetValue("GO_PACKAGE_VALUE", mod->Vars.EvalValue("REALPRJNAME")); // TODO: move the handling of this case to args parser
    } else {
        SetDirNameBasenameImpl(mod);
    }
}

void SetDefaultRealprjnameImpl(TModule* mod, size_t depth) {
    TFsPath path(mod->GetDir().CutType());
    TString target = path.Basename();
    TFsPath parent = path.Parent();
    for (; parent.IsDefined() && parent.Basename() != "." && depth > 0; --depth) {
        target = TString::Join(parent.Basename(), "-", target);
        parent = parent.Parent();
    }
    mod->Vars.SetValue("REALPRJNAME", target);
}

void SetTwoDirNamesBasename(TModule* mod) {
    if (!mod->Vars.contains("REALPRJNAME")) {
        SetDefaultRealprjnameImpl(mod, 1);
    }
}

void SetThreeDirNamesBasename(TModule* mod) {
    if (!mod->Vars.contains("REALPRJNAME")) {
        SetDefaultRealprjnameImpl(mod, 2);
    }
}

// Trim a/b/c/d to b/c/d, or c/d, or d, such that it is under limit or has no /.
void ShortenPath(TString& path, size_t limit) {
    if (path.length() > limit) {
        size_t pos = path.find('/', path.length() - limit - 1);
        if (pos == path.npos) {
            pos = path.rfind('/');
        }
        if (pos != path.npos) {
            path.erase(0, pos + 1);
        }
    }
}

void SetFullPathBasenameImpl(TModule* mod) {
    TFsPath dir(mod->GetDir().CutType());
    TString target = dir.c_str();
    // link.exe fails under wine when resolved absolute output path length >= 258.
    // (This is the documented 260 character limit after prepending a drive letter.)
    // distbuild wastes 154 chars for the build root:
    // /place/db-0/key_0/srvs/worker_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/srvdata/build/1/11111111-22222222-33333333-44444-5/66/test-7777777777777777777-8-BBBB/9999/
    // This leaves 103 chars for the root relative path: dir + "/" + target + ".exe".
    // dir + target without "/" and ".exe" should fit in 98 chars.
    ShortenPath(target, 98 > target.length() ? 98 - target.length() : 0);
    // TODO: Use NPath::PATH_SEP_S() here
    static Tr tr("/", "-");
    tr.Do(target);
    mod->Vars.SetValue("REALPRJNAME", target);
}

void SetFullPathBasename(TModule* mod) {
    if (!mod->Vars.contains("REALPRJNAME")) {
        SetFullPathBasenameImpl(mod);
    }
}


void ParseDllModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args) {
    bool versionDefined = false;
    for (size_t n = 0; n < args.size(); n++) {
        if (args[n] == "EXPORTS") {
            YConfErr(UserErr) << "EXPORTS keyword in module header is deprecated and will be ignored. Use EXPORTS_SCRIPT macro instead." << Endl;
            n++;
            continue;
        }
        if (args[n] == "PREFIX") {
            if (args.size() > n + 1) {
                mod->Vars.SetValue("MODULE_PREFIX", EvalExpr(mod->Vars, args[++n]));
            } else {
                YWarn() << "in " << mod->GetMakefile() << ", prefix not specified after PREFIX keyword" << Endl;
            }
            continue;
        }
        if (!mod->Vars.Contains("REALPRJNAME")) {
            mod->Vars.SetValue("REALPRJNAME", args[n]);
            continue;
        }
        // Used only for DLL-based modules
        if (!versionDefined) {
            mod->Vars.SetValue("MODULE_VERSION", TString::Join(".", args[n]));
            versionDefined = true;
            continue;
        }
    }
}

void ParseBaseModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args) {
    ParseModuleArgsBase(mod, args);
}

void ParseRawModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args) {
    mod->Vars.SetValue("MODULE_ARGS_RAW", JoinStrings(args.cbegin(), args.cend(), " "));
}
