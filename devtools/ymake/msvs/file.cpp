#include "file.h"

#include <util/generic/array_size.h>
#include <util/generic/is_in.h>
#include <util/string/subst.h>

namespace NYMake {
    namespace NMsvs {
        namespace {
            const TStringBuf OBJ_EXTS[] = {TStringBuf("o"), TStringBuf("obj")};
            const TStringBuf REGULAR_SOURCE_EXTS[] = {TStringBuf("c"), TStringBuf("cpp"), TStringBuf("cxx"), TStringBuf("cc"), TStringBuf("C"), TStringBuf("auxcpp")};
            const TStringBuf ASM_EXTS[] = {TStringBuf("masm"), TStringBuf("asm"), TStringBuf("rodata"), TStringBuf("yasm")};
            const TStringBuf C_EXTS[] = {TStringBuf("c")};
            const TStringBuf O_EXTS[] = {TStringBuf("o")};
            const TStringBuf CU_EXTS[] = {TStringBuf("cu")};
            const TStringBuf COMPILE_CUDA_PY = "compile_cuda.py";

            bool IsObjPathByExt(TStringBuf ext) {
                return ::IsIn(OBJ_EXTS, OBJ_EXTS + Y_ARRAY_SIZE(OBJ_EXTS), ext);
            }

            bool IsRegularSourcePathByExt(TStringBuf ext) {
                return ::IsIn(REGULAR_SOURCE_EXTS, REGULAR_SOURCE_EXTS + Y_ARRAY_SIZE(REGULAR_SOURCE_EXTS), ext);
            }
        }

        TString WindowsPath(TString unixPath) {
            ::SubstGlobal(unixPath, '/', '\\');
            return unixPath;
        }

        TString WindowsPathWithPrefix(const TString& prefix, TString unixPath) {
            ::SubstGlobal(unixPath, '/', '\\');
            return prefix + unixPath;
        }

        bool IsObjPath(TStringBuf str) {
            return IsObjPathByExt(NPath::Extension(str));
        }

        bool IsObjPath(TFileView path) {
            return IsObjPathByExt(path.Extension());
        }

        // XXX: detect regular sources from config data or graph, not by extensions
        bool IsRegularSourcePath(TStringBuf str) {
            return IsRegularSourcePathByExt(NPath::Extension(str));
        }

        // XXX: detect regular sources from config data or graph, not by extensions
        bool IsRegularSourcePath(TFileView path) {
            return IsRegularSourcePathByExt(path.Extension());
        }

        bool IsAsmSourcePath(const TStringBuf& str) {
            return IsIn(ASM_EXTS, ASM_EXTS + Y_ARRAY_SIZE(ASM_EXTS), NPath::Extension(str));
        }

        bool IsCSourcePath(const TStringBuf& str) {
            return IsIn(C_EXTS, C_EXTS + Y_ARRAY_SIZE(C_EXTS), NPath::Extension(str));
        }

        bool IsOObjPath(const TStringBuf& str) {
            return IsIn(O_EXTS, O_EXTS + Y_ARRAY_SIZE(O_EXTS), NPath::Extension(str));
        }

        bool IsCuSourcePath(const TStringBuf& str) {
            return IsIn(CU_EXTS, CU_EXTS + Y_ARRAY_SIZE(CU_EXTS), NPath::Extension(str)) || NPath::AnyBasename(str) == COMPILE_CUDA_PY;
        }
    }
}
