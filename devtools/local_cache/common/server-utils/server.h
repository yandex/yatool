#pragma once

#include <library/cpp/logger/log.h>

#include <util/generic/fwd.h>
#include <util/generic/ptr.h>
#include <util/generic/yexception.h>
#include <util/network/pair.h>
#include <util/network/socket.h>
#include <util/string/cast.h>

namespace NCachesPrivate {
    class IParentChildChannel {
    public:
        IParentChildChannel() {
        }

        virtual ~IParentChildChannel() {
        }

        virtual void CleanupParent() = 0;
        virtual void CleanupChild() = 0;
        virtual void Cleanup() = 0;
        virtual int SendToPeer(const TString& in) = 0;
        virtual int RecvFromPeer(TString& out) = 0;
    };

    struct TServerStop : yexception {
        TServerStop(int exitCode)
            : ExitCode(exitCode)
        {
        }
        int ExitCode;
    };

    class TParentChildSocketChannel final: public IParentChildChannel {
    public:
        TParentChildSocketChannel();

        void CleanupParent() override {
            CloseChild();
        }

        void CleanupChild() override {
            CloseParent();
        }

        void Cleanup() override {
            CloseChild();
            CloseParent();
        }

        int SendToPeer(const TString& in) override;
        int RecvFromPeer(TString& out) override;

    private:
        void CloseParent() {
            if (Parent_.Get()) {
                Parent_->Close();
            }
            Parent_.Reset(nullptr);
        }

        void CloseChild() {
            if (Child_.Get()) {
                Child_->Close();
            }
            Child_.Reset(nullptr);
        }

    private:
        SOCKET Sockets_[2];
        THolder<TSocket> Parent_;
        THolder<TSocket> Child_;
    };

    template <const char Msg[], int Rc>
    class TNotifyParent {
    public:
        template <typename T>
        static inline void Destroy(T* channel) noexcept {
            channel->SendToPeer(ToString(Msg));
            channel->SendToPeer(ToString(Rc));
        }
    };

#if defined(_unix_)
    /// Throws TServerStop from grandparent.
    TAutoPtr<NCachesPrivate::IParentChildChannel> Daemonize(TLog& log);
#endif
}
