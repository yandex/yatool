#pragma once

#include <util/generic/yexception.h>
#include <util/generic/fwd.h>
#include <util/system/fstat.h>

#include <sys/stat.h>

#ifndef _win_
    #include <dirent.h>
#else // !_win_
    #include <util/folder/dirent_win.h>
#endif // #else !_win_

class TReadDir {
public:
    class TError: public yexception {
    };

    struct TOptions {
        union {
            ui32 All;
            struct {
                ui32 ForceStat    : 1; ///< Force call stat on each directory items
                ui32 SkipOnlyDots : 1; ///< Skip only . and .. items
            };
        };

        TOptions()
            : All(0)
        {}

        TOptions& SetForceStat(bool forceStat = true) {
            ForceStat = forceStat;
            return *this;
        }

        TOptions& SetSkipOnlyDots(bool skipOnlyDots = true) {
            SkipOnlyDots = skipOnlyDots;
            return *this;
        }
    };

    TReadDir() = delete;
    explicit TReadDir(const TString& dirName, TOptions options = {});

private:
    class TIterator {
    public:
        TIterator() noexcept; ///< End of directory data
        ~TIterator() noexcept;

        /// Return one item of directory, return <filename, IsDir flag, pointer to stat data>
        ///
        /// ATTN!!! Filename and pointer invalidate on increment!!!
        /// Filename use C function readdir() buffer
        /// IsDir flag filled by struct dirent d_type (or emulating it from stat)
        /// Pointer to stat data use internal buffer and returned only if lstat() was called
        /// for directory item, else returned nullptr
        std::tuple<TStringBuf, bool, const TFileStat*> operator*();

        /// Goto next directory item
        ///
        /// ATTN!!! Filename and pointer to stat data invalidate on increment!!!
        TIterator& operator++();

        bool operator!=(const TIterator& it) const noexcept;

    private:
        DIR* Dir_; ///< Opened directory
        const struct dirent* Dirent_; ///< Returned from readdir() (nullptr after ++)
    // Used only when d_type is DT_UNKNOWN
        const TFileStat* Stat_; ///< Pointer to StatBuf_ if lstat() called, else nullptr (nullptr after ++)
        TFileStat StatBuf_; ///< Buffer for lstat()
        const char* DirName_; ///< Directory name form TReaddir
        TOptions Options_; ///< Options from TReaddir
#ifdef _win_
        struct _stat64 Stat64_; ///< Windows buffer for stat
#else // _win_
        int DirFd_; ///< Directory descriptor for use in fstatat() for emulate lstat()
#endif // #else _win_

        TIterator(const TReadDir* readdir); ///< Start read directory data

        /// Goto next directory item
        void Next();

        /// Close all opened
        void CloseAll() noexcept;

        friend class TReadDir;
    };

public:
    TIterator begin() const {
        return TIterator(this);
    }

    TIterator end() const noexcept {
        return TIterator();
    }

private:
    const char* DirName_; ///< Directory name
    TOptions Options_;
};
