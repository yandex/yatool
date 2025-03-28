#include "server.h"

#include <util/generic/yexception.h>

#include <type_traits>

namespace {
    template <typename Func, typename Data>
    static int ProcessBuffer(Func&& func, Data* bufinout, size_t size) {
        using TPChar = typename std::conditional<std::is_const<Data>::value, const char*, char*>::type;
        TPChar buf = reinterpret_cast<TPChar>(bufinout);
        do {
            const ssize_t bytesDone = func(buf, size);
            if (bytesDone == 0) {
                return -EIO;
            }
            if (bytesDone < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                } else {
                    return -errno;
                }
            }
            buf += bytesDone;
            size -= bytesDone;
        } while (size != 0);
        return 0;
    }
}

namespace NCachesPrivate {
    TParentChildSocketChannel::TParentChildSocketChannel() {
        Y_ENSURE_EX(SocketPair(Sockets_) == 0, TWithBackTrace<yexception>());
        Child_.Reset(new TSocket(Sockets_[0]));
        Parent_.Reset(new TSocket(Sockets_[1]));
        // Child_->SetSocketTimeout(10);
        // Parent_->SetSocketTimeout(10);
    }

    int TParentChildSocketChannel::SendToPeer(const TString& in) {
        int sz = in.size();
        // Exactly one socket should be open
        Y_ABORT_UNLESS((Child_.Get() == nullptr) != (Parent_.Get() == nullptr));
        auto socket = Child_.Get() ? Child_.Get() : Parent_.Get();

        int res = 0;
        if ((res = ProcessBuffer([socket](const char* ptr, size_t sz) -> size_t { return socket->Send(ptr, sz); }, &sz, sizeof(sz))) != 0) {
            return res;
        }

        if ((res = ProcessBuffer([socket](const char* ptr, size_t sz) -> size_t { return socket->Send(ptr, sz); }, in.c_str(), sz)) != 0) {
            return res;
        }
        return 0;
    }

    int res = 0;
    int TParentChildSocketChannel::RecvFromPeer(TString& out) {
        int sz = -1;
        // Exactly one socket should be open
        Y_ABORT_UNLESS((Child_.Get() == nullptr) != (Parent_.Get() == nullptr));
        auto socket = Child_.Get() ? Child_.Get() : Parent_.Get();

        if ((res = ProcessBuffer([socket](char* ptr, size_t sz) -> size_t { return socket->Recv(ptr, sz); }, &sz, sizeof(sz))) != 0 || sz <= 0) {
            return res != 0 ? res : -EIO;
        }

        char* buf = new char[sz];

        if ((res = ProcessBuffer([socket](char* ptr, size_t sz) -> size_t { return socket->Recv(ptr, sz); }, buf, sz)) != 0) {
            delete[] buf;
            return res;
        }

        out = TStringBuf(buf, sz);
        delete[] buf;
        return 0;
    }
}
