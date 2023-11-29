#include "run_python.h"

#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/array_ref.h>

#include <Python.h>
#include <stdlib.h>

#ifdef _win32_
#define PUTENV _putenv
#else
#define PUTENV putenv
#endif

struct TPy3StrDeleter {
    static void Destroy(wchar_t* str) noexcept {
        PyMem_RawFree(str);
    }
};
using TPy3Str = THolder<wchar_t, TPy3StrDeleter>;

TVector<TPy3Str> Py3StringConvert(TArrayRef<char*> strings) {
    TVector<TPy3Str> res;
    res.reserve(strings.size());
    for (char* str: strings) {
        res.push_back(TPy3Str{Py_DecodeLocale(str, nullptr)});
    }
    return res;
}

int NYMake::RunPython(int argc, char** argv, const char* progname) {
    TString prognamestr(progname);
    TVector<char*> args;

    args.push_back(prognamestr.begin());

    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    TString env("PYTHONDONTWRITEBYTECODE=x");
    PUTENV(env.begin());

    auto py3args = Py3StringConvert(args);
    TVector<wchar_t*> wargs;
    wargs.reserve(args.size());
    for (const auto& arg: py3args) {
        wargs.push_back(arg.Get());
    }
    return Py_Main(wargs.size(), wargs.data());
}
