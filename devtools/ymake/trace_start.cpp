#include "trace_start.h"

#include <util/generic/buffer.h>
#include <util/generic/fwd.h>

#include <util/system/env.h>
#include <util/system/pipe.h>
#include <util/system/platform.h>
#include <util/system/shellcommand.h>
#include <util/system/tempfile.h>
#include <util/system/thread.h>

class TStdinTrace {
public:
    static void Start(TString stdinTracePath) {
        StdinTraceInstance_.Reset(new TStdinTrace{stdinTracePath});
        TThread traceThread{TThread::TParams{&TStdinTrace::Thread, nullptr}.SetName("Stdin trace")};
        traceThread.Start();
        traceThread.Detach();
    }

private:
    class TWriteBuffer {
    public:
        TWriteBuffer(const TBuffer& buffer)
            : Data_(buffer.Data())
            , Size_(buffer.Size())
        {
        }

        template<typename THandle>
        void Write(THandle& out) {
            while (Size_) {
                ProcessWriteResult(out.Write(Data_, Size_));
            }
        }

        void ProcessWriteResult(ssize_t written) {
            if (written < 0) {
                if (errno == EINTR) {
                    return;
                }

                ythrow TSystemError();
            }

            Data_ += written;
            Size_ -= written;
        }

    private:
        const char* Data_;
        size_t Size_;
    };

    static void* Thread(void*) {
        try {
            TAtomicSharedPtr<TStdinTrace> traceInstance = StdinTraceInstance_;
            traceInstance->Communicate();
        } catch (const std::exception& e) {
            Cerr << "Failed to communicate stdin trace: " << e.what() << Endl;
        }

        return nullptr;
    }

    TStdinTrace(TString path) {
        RealStdin_ = Duplicate(0);

#if !defined(_win_)
        TPipeHandle stdinReader;
        TPipeHandle::Pipe(stdinReader, StdinWriter_);
        TFileHandle readerFile{stdinReader.Release()};
#else
        TFileHandle readerFile{MakeTempName(), OpenAlways | RdWr | ARW | Temp | Transient};
        StdinWriter_ = readerFile.Duplicate();
#endif
        readerFile.Duplicate2Posix(0);
        readerFile.Close();

        StdinTrace_ = TFileHandle{path, OpenAlways | WrOnly | ARW};
    }

    void Communicate() {
        TBuffer buffer{1024};

        while (ReadRealStdin(buffer)) {
            TWriteBuffer{buffer}.Write(StdinTrace_);
            StdinTrace_.Flush();

            TWriteBuffer{buffer}.Write(StdinWriter_);
        }

        StdinTrace_.Close();
        StdinWriter_.Close();
    }

    bool ReadRealStdin(TBuffer& buffer) {
        ssize_t readBytes = RealStdin_.RawRead(buffer.Data(), buffer.Capacity());

        if (readBytes < 0) {
            ythrow TSystemError() << "failed to read real stdin";
        }

        if (readBytes > 0) {
            buffer.Resize(readBytes);
            return true;
        }

        return false;
    }

private:
#if !defined(_win_)
    TPipeHandle StdinWriter_;
#else
    TFileHandle StdinWriter_;
#endif

    TFile RealStdin_;
    TFileHandle StdinTrace_;

    // We are using a detached thread to communicate stdin traces.
    // Static destructors (including this shared pointer destructor)
    // will be executed when main thread exists and the communicating thread
    // is yet alive. If we had a static TStdinTrace instance instead,
    // we would close all it's handles prematurely
    // and get spurious errors in the communicating thread.
    // Use a shared pointer and hold another instance of it
    // in the communicating thread to avoid this.
    //
    // We are still not protected from abrupt termination
    // of the communicating thread. But of that we do not care.
    // We just need to live up to the end of the main thread:
    // all real communication through stdin will be already done
    // and trace file will be flushed even before ymake sees stdin data.
    // We want only to avoid spurious errors and do not miss
    // any of the real ones, so we rather fail than silently
    // get incomplete traces.
    static TAtomicSharedPtr<TStdinTrace> StdinTraceInstance_;
};

TAtomicSharedPtr<TStdinTrace> TStdinTrace::StdinTraceInstance_;

void TraceYmakeStart(int argc, char** argv) {
    TString traceProgram = GetEnv("YMAKE_START_TRACE_PROGRAM");

    if (traceProgram.empty())
        return;

    TShellCommandOptions opts;
    opts.SetUseShell(false);
    opts.SetAsync(false);

    TList<TString> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);

    TShellCommand cmd{traceProgram, args, opts};
    cmd.Run();

    TString output = cmd.GetOutput();
    TString inputTracePath = output.substr(0, output.find_first_of("\r\n"sv));
    if (!inputTracePath.Empty()) {
        TStdinTrace::Start(inputTracePath);
    }
}
