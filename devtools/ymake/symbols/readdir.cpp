#include "readdir.h"

#include "devtools/ymake/common/npath.h"

#include <fcntl.h>


/// Select only files and directories (but skip . and ..)
static bool Selector(const struct dirent* dirent, const TFileStat* stat, const TReadDir::TOptions options) {
    unsigned char d_type = dirent->d_type;
    if (d_type == DT_UNKNOWN && stat) {
        d_type = stat->IsFile() ? DT_REG : stat->IsDir() ? DT_DIR : DT_UNKNOWN;
    }
    if (d_type == DT_REG) { // regular file
        return true;
    } else if (d_type == DT_DIR) { // directory
        auto* d_name = dirent->d_name;
        if ('.' != *d_name) {
            return true;
        }
        ++d_name;
        if ('\0' == *d_name) { // skip .
            return false;
        }
        if (('.' == *d_name) && ('\0' == *(d_name + 1))) { // skip ..
            return false;
        }
        return true;
    } else { // not file and not directory
        return (bool)options.SkipOnlyDots;
    }
}

TReadDir::TReadDir(const TString& dirName, TOptions options)
    : DirName_(dirName.c_str())
    , Options_(options)
{}

TReadDir::TIterator::TIterator() noexcept
    : Dir_(nullptr)
    , Dirent_(nullptr)
    , Stat_(nullptr)
    , DirName_(nullptr)
{
#ifndef _win_
    DirFd_ = 0;
#endif // !_win_
}

TReadDir::TIterator::TIterator(const TReadDir* readdir)
    : Dirent_(nullptr)
    , Stat_(nullptr)
    , DirName_(readdir->DirName_)
    , Options_(readdir->Options_)
{
#ifndef _win_
    DirFd_ = 0;
#endif // !_win_
    Dir_ = opendir(DirName_);
    if (!Dir_) { // fail open directory
        ythrow TFileError() << "Can't open directory '" << DirName_ << "' by opendir()";
    }
    Next(); // get first directory item to iterator
}

TReadDir::TIterator::~TIterator() noexcept {
    CloseAll();
}

/// Return one item of directory, return <filename, IsDir flag, pointer to stat data>
std::tuple<TStringBuf, bool, const TFileStat*> TReadDir::TIterator::operator*() {
    if (!Dir_ || !Dirent_) {
        return { TStringBuf(), false, nullptr };
    }
    return {
        TStringBuf(Dirent_->d_name),
        Dirent_->d_type == DT_DIR || (Stat_ && Stat_->IsDir()),
        Stat_
    };
}

/// Goto next directory item
TReadDir::TIterator& TReadDir::TIterator::operator++() {
    Next();
    return *this;
}

bool TReadDir::TIterator::operator!=(const TIterator& it) const noexcept {
    return (Dir_ != it.Dir_) || (Dirent_ != it.Dirent_);
}

/// Goto next directory item
void TReadDir::TIterator::Next() {
    do {
        if (!Dir_) {
            ythrow TError() << "Try get next item of closed directory '" << DirName_;
            break;
        }
        if (!(Dirent_ = readdir(Dir_))) { // End of directory entries
            CloseAll();
            break;
        };
        if (Dirent_->d_type == DT_LNK && !Options_.SkipOnlyDots) {
            Stat_ = nullptr;
            continue;
        }
        if ((Options_.ForceStat) || (Dirent_->d_type == DT_UNKNOWN)) {
#ifdef _win_
            Stat_ = nullptr;
            auto fullname = NPath::Join(TStringBuf(DirName_), TStringBuf(Dirent_->d_name));
            StatBuf_ = TFileStat(fullname, true);

            Stat_ = &StatBuf_; // success lstat() of file
#else // _win_
            if (!DirFd_) { // must open directory descriptor
                if ((DirFd_ = open(DirName_, O_RDONLY)) < 0) {
                    ythrow TFileError() << "Can't open directory '" << DirName_ << "' by open()";
                }
            }
            // Use fstatat with relative filename instead lstat with full filename
            struct stat st;
            if (auto err = fstatat(DirFd_, Dirent_->d_name, &st, AT_SYMLINK_NOFOLLOW /* lstat() mode */); err == 0) {
                StatBuf_ = TFileStat(st);
            } else {
                ythrow TFileError() << "Can't lstat() file '" << DirName_ << '/' << Dirent_->d_name << "' by fstatat() (" << err << " - " << strerror(err) << ")";
            }
#endif // #else _win_
            Stat_ = !Options_.SkipOnlyDots && StatBuf_.IsSymlink() ? nullptr : &StatBuf_;
        } else {
            Stat_ = nullptr; // lstat() not used for current item
        }
    } while (!Selector(Dirent_, Stat_, Options_));
}

/// Close all opened
void TReadDir::TIterator::CloseAll() noexcept {
    if (Dir_) {
        closedir(Dir_);
        Dir_ = nullptr;
    }
#ifndef _win_
    if (DirFd_ > 0) {
        close(DirFd_);
        DirFd_ = 0;
    }
#endif // !_win_
    Dirent_ = nullptr;
    Stat_ = nullptr;
}
