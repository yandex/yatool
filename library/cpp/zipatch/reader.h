#pragma once

#include <util/folder/path.h>

#include <memory>

namespace NZipatch {

class TReader {
public:
    enum EAction {
        Unknown = 0,
        Copy,
        MkDir,
        Move,
        Remove,
        StoreFile,
    };

    struct TSource {
        TString Path;
        ui64 Revision;
    };

    struct TEvent {
        EAction Action;
        TString Path;
        TStringBuf Data;
        TSource Source;
        bool Executable;
        bool Symlink;
    };

    using TOnEvent = std::function<void(const TEvent&)>;

public:
     TReader(const TFsPath& path);
     TReader(const TStringBuf buf);
    ~TReader();

    void Enumerate(TOnEvent cb) const;

private:
    class TImpl;
    std::unique_ptr<TImpl> Impl_;
};

} // namespace NZipatch

