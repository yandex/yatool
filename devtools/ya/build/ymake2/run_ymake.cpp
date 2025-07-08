#include "run_ymake.h"

#include <library/cpp/blockcodecs/codecs.h>
#include <library/cpp/iterator/concatenate.h>
#include <library/cpp/iterator/enumerate.h>
#include <library/cpp/pybind/ptr.h>
#include <library/cpp/ucompress/writer.h>

#include <util/datetime/base.h>
#include <util/generic/scope.h>
#include <util/stream/file.h>
#include <util/stream/str.h>
#include <util/stream/walk.h>
#include <util/string/cast.h>
#include <util/system/pipe.h>
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

    TString ReadLine(TPipe& input) {
        TString result;
        char buf{};
        while (buf != '\n') {
            auto res = input.Read(&buf, 1);
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

TRunYmakeMulticonfigResultPtr RunYMakeMulticonfig(const TList<TRunYmakeParams>& params, size_t threads) {
    THashMap<int, TRunYMakeResultPtr> results;
    TString Binary;
    TList<TString> Args;
    THashMap<TString, TString> Env;
    THashMap<int, TStringStream> outStreams, errStreams;
    TList<TThread> pollThreads;
    THashMap<int, TPipe> stdinr, stdinw, stdoutr, stdoutw, stderrr, stderrw;

    if (threads > 0) {
        Args.push_back("--threads");
        Args.push_back(ToString(threads));
    }

    for (const auto& [i, param]: Enumerate(params)) {
        // TODO: check binary is the same for all params
        Binary = param.Binary;
        // TODO: check env is used at all
        Env = param.Env;
        Args.push_back("--conf-id");
        Args.push_back(ToString(i));
        if (param.StdinLineProvider != Py_None) {
            TPipe::Pipe(stdinr[i], stdinw[i]);
            Args.push_back("--fd-in");
            Args.push_back(ToString(stdinr[i].GetHandle()));
            pollThreads.emplace_back([&stdinw, i, &param]() {
                TInputStreamFromCallback input(param.StdinLineProvider);
                TString line;
                while (input.ReadLine(line)) {
                    stdinw[i].Write(line.c_str(), line.size());
                    stdinw[i].Write("\n", 1);
                }
            });
        }
        TPipe::Pipe(stdoutr[i], stdoutw[i]);
        Args.push_back("--fd-out");
        Args.push_back(ToString(stdoutw[i].GetHandle()));
        pollThreads.emplace_back([&stdoutr, &outStreams, i]() {
            char buf[1 << 10];
            while (size_t len = stdoutr[i].Read(buf, 1024)) {
                outStreams[i].Write(buf, len);
            }
        });
        TPipe::Pipe(stderrr[i], stderrw[i]);
        Args.push_back("--fd-err");
        Args.push_back(ToString(stderrw[i].GetHandle()));
        pollThreads.emplace_back([&stderrr, &errStreams, i, &param]() {
            while (TString line = ReadLine(stderrr[i])) {
                CallReader(param.StderrLineReader, line);
                errStreams[i] << line;
            }
        });
        Args.insert(Args.end(), param.Args.begin(), param.Args.end());
    }

    for (auto& thread: pollThreads) {
        thread.Start();
    }

#ifdef  _win_
    // disable message box
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif

    TShellCommandOptions options = TShellCommandOptions()
        .SetUseShell(false)
        .SetDetachSession(false);
    options.Environment = Env;
    // SetUseShell disables quoting if it's enabled. Call SetQuoteArguments after SetUseShell.
    // It's safe to set quote on the windows, because args will be quoted only if it's required.
    options.SetQuoteArguments(true);

    TStringStream errStream{};
    options.SetErrorStream(&errStream);

    TShellCommand shCmd{Binary, Args, options};
    shCmd.Run();

    for (auto& s: Concatenate(stdoutw, stderrw, stdinw, stdinr)) {
        s.second.Close();
    }

    if (!shCmd.GetExitCode().Defined()) {
        throw yexception() << "Internal error: no exit code";
    }

    int exitCode = shCmd.GetExitCode().GetRef();
    if (exitCode == 255) {
        throw yexception() << "Cannot start process: " << errStream.Str();
    }

    if (exitCode != 0) {
        Cerr << errStream.Str() << Endl;
    }

    for (const auto& [i, param]: Enumerate(params)) {
        results[i] = MakeAtomicShared<TRunYMakeResult>(exitCode, std::move(outStreams[i].Str()), std::move(errStreams[i].Str()));
    }

    for (auto& thread: pollThreads) {
        thread.Join();
    }

    return MakeAtomicShared<THashMap<int, TRunYMakeResultPtr>>(results);
}
