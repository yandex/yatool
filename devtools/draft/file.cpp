#include "file.h"
#include <library/cpp/digest/md5/md5.h>
#include <util/stream/file.h>
#include <util/stream/output.h>
#include <util/generic/buffer.h>
#include <util/folder/dirut.h>
#include <util/folder/path.h>

namespace NDev {

TModifiedFile::TModifiedFile(const TString& path, bool keepBackup)
    : Name(path)
    , Modified(-1)
    , KeepBackup(keepBackup)
{
    if (NFs::Exists(path)) {
        Digest = MD5::File(path);
    }
    else {
        TFsPath(path).Parent().MkDirs();
        TOFStream out(path);
        KeepBackup = false;
    }
}

TModifiedFile::~TModifiedFile() {
    DoFinish();
}

void TModifiedFile::DoFinish() {
    if (Modified == -1) {
        Modified = false;
        if (MD5::Data(TStringBuf(Buffer().Data(), Buffer().Size())) != Digest) {
            if (KeepBackup) {
                rename(Name.data(), (Name + ".bak").data());
            }
            TOFStream out(Name);
            out.Write(Buffer().Data(), Buffer().Size());
            // Cdbg << "written modified: " << Name << Endl;
            Modified = true;
        }
        Buffer().Clear();
    }
}


}
