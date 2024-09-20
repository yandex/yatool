#pragma once

#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/folder/dirut.h>
#include <util/folder/path.h>
#include <util/system/fstat.h>
#include <util/string/builder.h>

namespace NUniversalFetcher {

    class TDstPath {
    public:
        static TDstPath FromFilePath(const TString& path) {
            TDstPath ret(GetDirName(path), GetBaseName(path));
            Y_ENSURE(ret.HasFileName());
            return ret;
        }

        static TDstPath FromDirPath(const TString& path) {
            return TDstPath(path, {});
        }

        const TString& DirPath() const {
            return Dir_;
        }

        const TString& FilePath() const {
            CheckHasFileName();
            return FullPath_;
        }

        TString MakeFilePath(const TString& fileName) const {
            return Dir_ + "/" + fileName;
        }

        const TString& FileName() const {
            CheckHasFileName();
            return FileName_;
        }

        TFileStat Stat() const {
            return TFileStat(FilePath());
        }

        ui64 GetFileSize() const {
            return Stat().Size;
        }
        void MkDirs(int mode = MODE0777) const {
            TFsPath(DirPath()).MkDirs(mode);
        }

        void SetFileNameIfEmpty(const TString& fileName) {
            if (!HasFileName()) {
                SetFileName(fileName);
            }
        }

        void ClearFileName() {
            FileName_ = {};
            FullPath_ = {};
        }

        void SetFileName(const TString& fileName) {
            Y_ENSURE(fileName, "Can't set empty file name");
            FileName_ = fileName;
            RebuildFullPathIfNeed();
        }

        bool HasFileName() const {
            return !FileName_.empty();
        }

        void CheckHasFileName() const {
            Y_ENSURE(HasFileName());
        }

        TString MakeRepr() const {
            return TStringBuilder() << (HasFileName() ? "file:" : "dir:") << Dir_ << '/' << FileName_;
        }

    private:
        TDstPath(const TString& dir, const TString& fileName)
            : Dir_(dir)
            , FileName_(fileName)
        {
            RebuildFullPathIfNeed();
        }

        void RebuildFullPathIfNeed() {
            if (HasFileName()) {
                FullPath_ = Dir_ + "/" + FileName_;
            }
        }

    private:
        TString Dir_;
        TString FileName_;
        TString FullPath_;
    };

    class TExistingTempFile {
    public:
        TExistingTempFile(const TString& path)
            : Path_(path)
        {
        }

        ~TExistingTempFile() {
            if (!Removed_) {
                try {
                    Remove();
                } catch (...) {
                    Cerr << "Failed to remove path " << Path_ << ": " << CurrentExceptionMessage();
                }
            }
        }

        const TString& Path() const {
            return Path_;
        }

        operator const TString& () const {
            return Path();
        }

        void DoNotRemove() {
            Removed_ = true;
        }

        void Remove() {
            if (NFs::Exists(Path_)) {
                RemoveDirWithContents(Path_);
            }
            Removed_ = true;
        }

    private:
        TString Path_;
        bool Removed_ = false;
    };


}
