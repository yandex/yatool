#pragma once
#include <util/stream/output.h>
#include <util/stream/buffer.h>

namespace NDev {

/// drop in replacement for TOFstream that never writes file with same data
class TModifiedFile
    : public TBufferOutput
{
public:
    TModifiedFile(const TString& path, bool keepBackup = false);
    ~TModifiedFile() override;
    // to be called after Finish()
    bool WasModified() const {
        return Modified == 1;
    }

protected:
    void DoFinish() override;
private:
    TString Name;
    TString Digest;
    int Modified;
    bool KeepBackup;
};

}

