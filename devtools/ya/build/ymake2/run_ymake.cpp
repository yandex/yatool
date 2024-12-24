#include "run_ymake.h"

#include <library/cpp/blockcodecs/codecs.h>
#include <library/cpp/pybind/ptr.h>
#include <library/cpp/ucompress/writer.h>

#include <util/generic/scope.h>
#include <util/stream/file.h>
#include <util/stream/str.h>
#include <util/stream/walk.h>
#include <util/system/platform.h>

#ifdef  _win_
    #include <Windows.h>
    #include <util/system/win_undef.h>
#endif

constexpr bool BORROW = true;
using NPyBind::TPyObjectPtr;

namespace {
    void CallReader(PyObject* stderrLineReader, const TString& line) {
        PyGILState_STATE gilState = PyGILState_Ensure();
        Y_DEFER {PyGILState_Release(gilState);};

        TPyObjectPtr s{PyUnicode_FromStringAndSize(line.c_str(), line.size()), BORROW};
        if (!s.Get()) {
            throw yexception() << "Cannot create string object from " << line;
        }
        TPyObjectPtr result{PyObject_CallFunctionObjArgs(stderrLineReader, s.Get(), nullptr), BORROW};
        if (!result.Get()) {
            throw yexception() << "stderrLineReader(" << line << ") failed";
        }
    }

    TString ReadLine(TFile& input) {
        TString result;
        char buf{};
        while (buf != '\n') {
            auto res = input.RawRead(&buf, 1);
#ifdef _win_
            if (res < 0 && LastSystemError() != ERROR_BROKEN_PIPE) {
#else
            if (res < 0) {
#endif
                ythrow TFileError() << "Cannot read from command error pipe";
            }
            if (res <= 0) {
                break;
            }
            result += buf;
        }
        return result;
    }

    TString CallProvider(PyObject* lineProvider) {
        PyGILState_STATE gilState = PyGILState_Ensure();
        Y_DEFER {PyGILState_Release(gilState);};
        TPyObjectPtr result{PyObject_CallFunctionObjArgs(lineProvider, nullptr)};
        if (!result.Get()) {
            throw yexception() << "lineProvider() failed";
        }
        return PyUnicode_AsUTF8(result.Get());
    }

    class TInputStreamFromCallback : public IWalkInput {
    public:
        TInputStreamFromCallback(PyObject* providerCallback) : ProviderCallback_{providerCallback} {};
    protected:
        size_t DoUnboundedNext(const void** ptr) override {
            CurrentStr_ = CallProvider(ProviderCallback_);
            *ptr = CurrentStr_.c_str();
            return CurrentStr_.size();
        }
    private:
        PyObject* ProviderCallback_;
        TString CurrentStr_;
    };
}

TRunYMakeResultPtr RunYMake(
    TStringBuf binary,
    const TList<TString>& args,
    const THashMap<TString, TString>& env,
    PyObject* stderrLineReader,
    PyObject* stdinLineProvider
) {
#ifdef  _win_
    // disable message box
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif

    TShellCommandOptions options = TShellCommandOptions()
        .SetUseShell(false)
        .SetAsync(true)
        .PipeError();
    options.Environment = env;
    // With async=true always use non zero latency if you don't want to have problems with high CPU load.
    options.SetLatency(10);
    // SetUseShell disables quoting if it's enabled. Call SetQuoteArguments after SetUseShell.
    // It's safe to set quote on the windows, because args will be quoted only if it's required.
    options.SetQuoteArguments(true);

    TStringStream output{};
    options.SetOutputStream(&output);
    THolder<TInputStreamFromCallback> input;
    if (stdinLineProvider != Py_None) {
        input = MakeHolder<TInputStreamFromCallback>(stdinLineProvider);
        options.SetInputStream(input.Get());
    }

    TShellCommand shCmd{binary, args, options};
    shCmd.Run();
    TFile cmdErrors{shCmd.GetErrorHandle().Release()};

    TStringStream errStream{};
    while (TString line = ReadLine(cmdErrors)) {
        CallReader(stderrLineReader, line);
        errStream << line;
    }
    shCmd.Wait();

    if (!shCmd.GetExitCode().Defined()) {
        throw yexception() << "Internal error: no exit code";
    }

    int exitCode = shCmd.GetExitCode().GetRef();
    if (exitCode == 255) {
        throw yexception() << "Cannot start process: " << errStream.Str();
    }

    return MakeAtomicShared<TRunYMakeResult>(exitCode, std::move(output.Str()), std::move(errStream.Str()));
}
