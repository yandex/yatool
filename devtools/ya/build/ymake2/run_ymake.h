#pragma once

#include <Python.h>

#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/shellcommand.h>


struct TRunYMakeResult {
    int ExitCode;
    TString Stdout;
    TString Stderr;
};

using TRunYMakeResultPtr = TAtomicSharedPtr<TRunYMakeResult>;

TRunYMakeResultPtr RunYMake(
    TStringBuf binary,
    const TList<TString>& args,
    const THashMap<TString, TString>& env,
    PyObject* stderrLineReader,
    PyObject* stdinLineProvider
);

struct TRunYmakeParams {
    TString Binary;
    TList<TString> Args;
    THashMap<TString, TString> Env;
    PyObject* StderrLineReader;
    PyObject* StdinLineProvider;
};

using TRunYmakeMulticonfigResultPtr = TAtomicSharedPtr<THashMap<int, TRunYMakeResultPtr>>;

TRunYmakeMulticonfigResultPtr RunYMakeMulticonfig(const TList<TRunYmakeParams>& params, size_t threads);
