#include "ac.h"

#include <devtools/local_cache/ac/proto/ac.grpc.pb.h>
#include <devtools/local_cache/ac/proto/ac.pb.h>
#include <devtools/local_cache/psingleton/systemptr.h>

#include <library/cpp/pybind/ptr.h>
#include <library/cpp/pybind/cast.h>

#include <contrib/libs/grpc/include/grpcpp/grpcpp.h>

#include <util/generic/ptr.h>
#include <util/generic/scope.h>
#include <util/generic/yexception.h>
#include <util/system/guard.h>
#include <util/system/spinlock.h>

using NPyBind::TPyObjectPtr;

namespace NAcClient {
    namespace {
        constexpr bool BORROW = true;

        struct TGlobals {
            TPyObjectPtr AcPb2Module;
            TPyObjectPtr Logger;
            TPyObjectPtr GrpcErrorCls;
        };
        TGlobals globals{};

        inline PyObject* CheckNewObject(PyObject* object) {
            if (!object) {
                // User should never see the exception message because a Python error must be also triggered
                ythrow yexception() << "Internal error. Cannot get Python object but Python error is not occurred";
            }
            return object;
        }

        TPyObjectPtr GetPtrWithCheck(PyObject* object, bool borrow=false) {
            return TPyObjectPtr{CheckNewObject(object), borrow};
        }

        struct TGrpcConnection;
        using TGrpcConnectionPtr = TAtomicSharedPtr<TGrpcConnection>;

        struct TGrpcConnection {
            static inline TAdaptiveLock GrpcConnectionLock;
            static inline TGrpcConnectionPtr ConnectionPtr;

            static TGrpcConnectionPtr Get(const TAddress& paddress) {
                auto lockGuard = Guard(GrpcConnectionLock);
                if (!ConnectionPtr || paddress != ConnectionPtr->LastAddress) {
                    ConnectionPtr = MakeAtomicShared<TGrpcConnection>(paddress);
                }
                return ConnectionPtr;
            }

            TGrpcConnection(const TAddress& paddress) {
                LastAddress = paddress;
                grpc::ChannelArguments args{};
                args.SetMaxReceiveMessageSize(Max<int>());
                args.SetMaxSendMessageSize(Max<int>());
                Channel = grpc::CreateCustomChannel(std::get<2>(paddress), grpc::InsecureChannelCredentials(), args);
                Stub = NACCache::TACCache::NewStub(Channel);

                TProcessUID procName = TProcessUID::GetMyName();
                Peer.MutableProc()->SetPid(procName.GetPid());
                Peer.MutableProc()->SetStartTime(procName.GetStartTime());
            }

            TAddress LastAddress;
            std::shared_ptr<grpc::Channel> Channel{};
            std::unique_ptr<NACCache::TACCache::Stub> Stub{};
            NUserService::TPeer Peer{};
        };

        THolder<grpc::ClientContext> MakeContext(double timeout, bool waitForReady) {
            THolder<grpc::ClientContext> context = MakeHolder<grpc::ClientContext>();
            if (timeout) {
                std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now();
                deadline += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>(timeout));
                context->set_wait_for_ready(waitForReady);
                context->set_deadline(deadline);
            }
            return std::move(context);
        }

        void SetGrpcError(const grpc::Status& status) {
            const auto& error_details = status.error_details();
            TPyObjectPtr code = GetPtrWithCheck(PyLong_FromLong(status.error_code()), BORROW);
            TPyObjectPtr details = GetPtrWithCheck(PyUnicode_DecodeUTF8(error_details.data(), error_details.size(), "replace"), BORROW);
            TPyObjectPtr error = GetPtrWithCheck(PyObject_CallFunction(globals.GrpcErrorCls.Get(), "OO", code.Get(), details.Get()), BORROW);
            PyErr_SetObject(globals.GrpcErrorCls.Get(), error.Get());
        }

        void ThrowOnError(const grpc::Status& status) {
            if (status.ok()) {
                return;
            }
            SetGrpcError(status);
            ythrow yexception() << "Grpc error";
        }

        void LogWarning(PyObject* logger, TStringBuf message) {
            GetPtrWithCheck(PyObject_CallMethod(logger, "warn", "s#", message.data(), message.length()), BORROW);
        }

        void AnalyzeMetadata(const std::multimap<grpc::string_ref, grpc::string_ref>& metadata) {
            static bool recreateCheckReported{false};

            for (const auto& [k, v] : metadata) {
                if (!recreateCheckReported && k == "ac-db-recreated" && v == "true") {
                    recreateCheckReported = true;
                    LogWarning(globals.Logger.Get(), "AC cache was recreated");
                }
                if (k == "io-exception") {
                    TStringStream message{};
                    message << "IO error in local cache: " << v;
                    LogWarning(globals.Logger.Get(), message.Str());
                }
            }
        }

        // Note: steals a reference to value
        void AddToDict(TPyObjectPtr dict, const char* key, PyObject* value) {
            TPyObjectPtr valueRef = GetPtrWithCheck(value, BORROW);
            Y_ENSURE(PyDict_SetItemString(dict.Get(), key, value) == 0);
        }

        PyObject* ToPyObject(const NACCache::TStatus& status) {
            TPyObjectPtr cls = GetPtrWithCheck(PyObject_GetAttrString(globals.AcPb2Module.Get(), "TStatus"), BORROW);

            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            TPyObjectPtr kwargs = GetPtrWithCheck(PyDict_New(), BORROW);
            AddToDict(kwargs, "TotalFSSize", CheckNewObject(PyLong_FromUnsignedLongLong(status.GetTotalFSSize())));
            AddToDict(kwargs, "TotalSize", CheckNewObject(PyLong_FromUnsignedLongLong(status.GetTotalSize())));
            AddToDict(kwargs, "Master", CheckNewObject(NPyBind::BuildPyObject(status.GetMaster())));
            AddToDict(kwargs, "TotalDBSize", CheckNewObject(PyLong_FromUnsignedLong(status.GetTotalDBSize())));
            AddToDict(kwargs, "BlobCount", CheckNewObject(PyLong_FromUnsignedLong(status.GetBlobCount())));
            AddToDict(kwargs, "UidCount", CheckNewObject(PyLong_FromUnsignedLong(status.GetUidCount())));
            AddToDict(kwargs, "ProcessesCount", CheckNewObject(PyLong_FromUnsignedLong(status.GetProcessesCount())));

            return PyObject_Call(cls.Get(), args.Get(), kwargs.Get());
        }

        PyObject* ToPyObject(const NACCache::TACStatus& acStatus) {
            TPyObjectPtr cls = GetPtrWithCheck(PyObject_GetAttrString(globals.AcPb2Module.Get(), "TACStatus"), BORROW);

            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            TPyObjectPtr kwargs = GetPtrWithCheck(PyDict_New(), BORROW);
            AddToDict(kwargs, "Stats", CheckNewObject(ToPyObject(acStatus.GetStats())));
            AddToDict(kwargs, "Success", CheckNewObject(NPyBind::BuildPyObject(acStatus.GetSuccess())));
            AddToDict(kwargs, "Optim", CheckNewObject(PyLong_FromLong(acStatus.GetOptim())));

            return PyObject_Call(cls.Get(), args.Get(), kwargs.Get());
        }

        PyObject* ToPyObject(const NACCache::TTaskStatus& taskStatus) {
            TPyObjectPtr cls = GetPtrWithCheck(PyObject_GetAttrString(globals.AcPb2Module.Get(), "TTaskStatus"), BORROW);

            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            TPyObjectPtr kwargs = GetPtrWithCheck(PyDict_New(), BORROW);
            AddToDict(kwargs, "TotalFSSize", CheckNewObject(PyLong_FromUnsignedLongLong(taskStatus.GetTotalFSSize())));
            AddToDict(kwargs, "TotalSize", CheckNewObject(PyLong_FromUnsignedLongLong(taskStatus.GetTotalSize())));

            return PyObject_Call(cls.Get(), args.Get(), kwargs.Get());
        }

        PyObject* ToPyObject(PyObject* diskUsageCls, const NACCache::TDiskUsageSummary::TFileStat& fileStat) {
            TPyObjectPtr cls = GetPtrWithCheck(PyObject_GetAttrString(diskUsageCls, "TFileStat"), BORROW);
            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            TPyObjectPtr kwargs = GetPtrWithCheck(PyDict_New(), BORROW);
            AddToDict(kwargs, "Path", CheckNewObject(NPyBind::BuildPyObject(fileStat.GetPath())));
            AddToDict(kwargs, "FSSize", CheckNewObject(PyLong_FromLongLong(fileStat.GetFSSize())));
            AddToDict(kwargs, "Size", CheckNewObject(PyLong_FromLongLong(fileStat.GetSize())));
            AddToDict(kwargs, "Freq", CheckNewObject(PyLong_FromLong(fileStat.GetFreq())));

            return PyObject_Call(cls.Get(), args.Get(), kwargs.Get());
        }

        PyObject* ToPyObject(const NACCache::TDiskUsageSummary& diskUsage) {
            TPyObjectPtr cls = GetPtrWithCheck(PyObject_GetAttrString(globals.AcPb2Module.Get(), "TDiskUsageSummary"), BORROW);

            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            TPyObjectPtr kwargs = GetPtrWithCheck(PyDict_New(), BORROW);
            TPyObjectPtr fileStats = GetPtrWithCheck(PyList_New(0), BORROW);
            for (const auto& fileStat : diskUsage.GetFileStats()) {
                PyList_Append(fileStats.Get(), CheckNewObject(ToPyObject(cls.Get(), fileStat)));
            }
            AddToDict(kwargs, "Stats", CheckNewObject(ToPyObject(diskUsage.GetStats())));
            AddToDict(kwargs, "FileStats", fileStats.RefGet());
            return PyObject_Call(cls.Get(), args.Get(), kwargs.Get());
        }

        template <class T>
        PyObject* ProcessResult(const grpc::ClientContext& context, const grpc::Status& status, const T& response) {
            ThrowOnError(status);
            AnalyzeMetadata(context.GetServerTrailingMetadata());
            return ToPyObject(response);
        }

        class TGILUnlocker {
        public:
            TGILUnlocker() {
                ThreadState_ = PyEval_SaveThread();
            }

            ~TGILUnlocker() {
                RestoreGil();
            }

            void RestoreGil() {
                if (ThreadState_) {
                    PyEval_RestoreThread(ThreadState_);
                    ThreadState_ = nullptr;
                }
            }

        private:
            PyThreadState* ThreadState_;
        };

    } // namespace

    void InitGlobals(PyObject* acPb2Module, PyObject* logger, PyObject* grpcErrorCls) {
        globals.AcPb2Module.Reset(acPb2Module);
        globals.Logger.Reset(logger);
        globals.GrpcErrorCls.Reset(grpcErrorCls);
    }

    void ClearGlobals() {
        globals.AcPb2Module.Reset();
        globals.Logger.Reset();
        globals.GrpcErrorCls.Reset();
    }

    void ExceptionHandler() {
        try {
            if (!PyErr_Occurred()) {
                throw;
            }
        } catch (const yexception& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
        } catch (...) {
            PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
        }
    }

    PyObject* PutUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& rootPath,
        const TVector<TBlob>& blobs,
        long long weight,
        bool hardlink,
        bool replace,
        bool isResult,
        const TVector<TString>& fileNames,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TPutUid request{};
        NACCache::TACStatus response{};
        request.MutablePeer()->CopyFrom(connection->Peer);
        request.MutableACHash()->SetUid(uid);
        request.SetRootPath(rootPath);
        request.MutableOrigin()->SetOriginKind(NACCache::EOrigin::User);
        NACCache::EOptim optimization = hardlink ? NACCache::EOptim::Hardlink : NACCache::EOptim::Copy;
        for (const auto& [fpath, fuid] : blobs) {
            auto blobInfo = request.AddBlobInfo();
            blobInfo->SetOptimization(optimization);
            blobInfo->SetPath(fpath);
            if (fuid) {
                blobInfo->MutableCASHash()->SetUid(fuid);
            }
        }
        for (const TString& fileName : fileNames) {
            request.AddDBFileNames(fileName);
        }
        request.SetWeight(weight);
        request.SetReplacementMode(replace ? NACCache::EPutMode::ForceBlobReplacement : NACCache::EPutMode::UseOldBlobs);
        request.SetResult(isResult);
        grpc::Status status = connection->Stub->Put(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* GetUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& destPath,
        bool hardlink,
        bool isResult,
        bool release,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TGetUid request{};
        NACCache::TACStatus response{};
        request.MutablePeer()->CopyFrom(connection->Peer);
        request.MutableACHash()->SetUid(uid);
        request.SetDestPath(destPath);
        request.SetOptimization(hardlink ? NACCache::EOptim::Hardlink : NACCache::EOptim::Copy);
        request.SetResult(isResult);
        request.SetRelease(release);
        grpc::Status status = connection->Stub->Get(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* HasUid(
        const TAddress& paddress,
        const TString& uid,
        bool isResult,
        double timeout,
        bool waitForReady
    ) {
        // return ToPyObject(NACCache::TACStatus{}); // FIXME
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::THasUid request{};
        NACCache::TACStatus response{};
        request.MutablePeer()->CopyFrom(connection->Peer);
        request.MutableACHash()->SetUid(uid);
        request.SetResult(isResult);
        grpc::Status status = connection->Stub->Has(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* RemoveUid(
        const TAddress& paddress,
        const TString& uid,
        bool forcedRemoval,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TRemoveUid request{};
        NACCache::TACStatus response{};
        request.MutableACHash()->SetUid(uid);
        request.SetForcedRemoval(forcedRemoval);
        grpc::Status status = connection->Stub->Remove(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* GetTaskStats(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TTaskStatus response{};
        grpc::Status status = connection->Stub->GetTaskStats(context.Get(), connection->Peer, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* PutDeps(
        const TAddress& paddress,
        const TString& uid,
        const TVector<TString>& deps,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TNodeDependencies request{};
        NACCache::TACStatus response{};
        request.MutableNodeHash()->SetUid(uid);
        for (const TString& dep : deps) {
            NACCache::THash* hash = request.AddRequiredHashes();
            hash->SetUid(dep);

        }
        grpc::Status status = connection->Stub->PutDeps(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* ForceGC(
        const TAddress& paddress,
        uint64_t diskLimit,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TForceGC request{};
        NACCache::TStatus response{};
        request.MutablePeer()->CopyFrom(connection->Peer);
        request.SetTargetSize(diskLimit);
        grpc::Status status = connection->Stub->ForceGC(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    PyObject* ReleaseAll(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TTaskStatus response{};
        grpc::Status status = connection->Stub->ReleaseAll(context.Get(), connection->Peer, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

    struct TCacheStatsGeneratorState {
        THolder<grpc::ClientContext> Context{};
        std::unique_ptr<::grpc::ClientReader< ::NACCache::TStatus>> Reader{};

        ~TCacheStatsGeneratorState() {
            Context->TryCancel();
        }
    };

    struct TCacheStatsGeneratorObject {
        PyObject_HEAD;
        bool Inited;
        TCacheStatsGeneratorState State;

        // C++ initialization is not applicable
        TCacheStatsGeneratorObject() = delete;
        ~TCacheStatsGeneratorObject() = delete;

        static inline PyObject* New(PyTypeObject *type, PyObject *args, PyObject *kwds) {
            TCacheStatsGeneratorObject *self = (TCacheStatsGeneratorObject*) type->tp_alloc(type, 0);
            self->Inited = false;
            return (PyObject*) self;
        }

        static inline int Init(PyObject *self, PyObject *args, PyObject *kwds) {
            TCacheStatsGeneratorObject* m = (TCacheStatsGeneratorObject*) self;
            try {
                new(&m->State) TCacheStatsGeneratorState();
                m->Inited = true;
            } catch (const yexception& e) {
                PyErr_SetString(PyExc_RuntimeError, e.what());
                return -1;
            } catch (...) {
                PyErr_SetString(PyExc_RuntimeError, "Initialization failed");
                return -1;
            }

            return 0;
        }

        static inline void Dealloc(PyObject* self) {
            TCacheStatsGeneratorObject* m = (TCacheStatsGeneratorObject*) self;
            if (m->Inited) {
                m->State.~TCacheStatsGeneratorState();
                m->Inited = false; // Just in case
            }
        }

        static inline PyObject* IterNext(PyObject *self) {
            TCacheStatsGeneratorObject* m = (TCacheStatsGeneratorObject*) self;
            NACCache::TStatus response;
            if (m->State.Reader->Read(&response)) {
                return ToPyObject(response);
            }
            grpc::Status status = m->State.Reader->Finish();
            if (!status.ok()) {
                SetGrpcError(status);
            }
            return nullptr;
        }

        static inline PyTypeObject* CacheStatsGeneratorType = nullptr;

        static inline PyObject* CreateGenerator(
                THolder<grpc::ClientContext> context,
            std::unique_ptr< ::grpc::ClientReader< ::NACCache::TStatus>> reader
        ) {
            if (!CacheStatsGeneratorType) {
                PyType_Slot slots[] = {
                    {Py_tp_new,     (void*)New},
                    {Py_tp_init,    (void*)Init},
                    {Py_tp_dealloc, (void*)Dealloc},
                    {Py_tp_iter, (void*)PyObject_SelfIter},
                    {Py_tp_iternext, (void*)IterNext},
                    {0, nullptr}
                };
                PyType_Spec spec {
                    .name = "ac.CacheStatsGenerator",
                    .basicsize = sizeof(TCacheStatsGeneratorObject),
                    .itemsize = 0,
                    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
                    .slots = slots,
                };
                CacheStatsGeneratorType = (PyTypeObject*) CheckNewObject(PyType_FromSpec(&spec));
            }

            TPyObjectPtr args = GetPtrWithCheck(PyTuple_New(0), BORROW);
            PyObject* generator = CheckNewObject(PyObject_Call((PyObject*) CacheStatsGeneratorType, args.Get(), nullptr));
            TCacheStatsGeneratorState& state = ((TCacheStatsGeneratorObject*) generator)->State;

            state.Context = std::move(context);
            state.Reader = std::move(reader);
            return generator;
        }
    };

    PyObject* GetCacheStats(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TStatus request{};
        auto reader = connection->Stub->GetCacheStats(context.Get(), request);
        gilUnlocker.RestoreGil();

        return TCacheStatsGeneratorObject::CreateGenerator(std::move(context), std::move(reader));
    }

    PyObject* AnalyzeDU(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TStatus request{};
        NACCache::TDiskUsageSummary response{};
        grpc::Status status = connection->Stub->AnalyzeDU(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }

   PyObject* SynchronousGC(
        const TAddress& paddress,
        long long totalSize,
        long long minLastAccess,
        long long maxObjectSize,
        double timeout,
        bool waitForReady
    ) {
        TGILUnlocker gilUnlocker{};

        TGrpcConnectionPtr connection = TGrpcConnection::Get(paddress);
        THolder<grpc::ClientContext> context = MakeContext(timeout, waitForReady);
        NACCache::TSyncronousGC request{};
        if (totalSize >= 0) {
            request.SetTotalSize(totalSize);
        } else if (maxObjectSize >= 0) {
            request.SetBLobSize(maxObjectSize);
        } else if (minLastAccess >= 0) {
            request.SetTimestamp(minLastAccess);
        }
        NACCache::TStatus response{};
        grpc::Status status = connection->Stub->SynchronousGC(context.Get(), request, &response);

        gilUnlocker.RestoreGil();

        return ProcessResult(*context, status, response);
    }
}
