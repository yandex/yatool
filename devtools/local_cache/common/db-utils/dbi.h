#pragma once

#include <library/cpp/containers/stack_vector/stack_vec.h>
#include <library/cpp/resource/resource.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/generic/function.h>
#include <util/generic/list.h>
#include <util/generic/maybe.h>
#include <util/string/cast.h>
#include <util/system/rwlock.h>

/// Helper functions to implement internal checks and simplify boiler-plate code.
namespace NCachesPrivate {
    /// One shot execution of SQLite statements.
    class TStmtSeq {
    public:
        TStmtSeq(NSQLite::TSQLiteDB& db, const TVector<TStringBuf>& resources);

    private:
        TList<NSQLite::TSQLiteStatement> Stmts_;
    };

    enum ELockKind {
        EXCLUSIVE,
        IMMEDIATE,
        DEFERRED
    };

    template <typename SqlSeq>
    class Wrappers {
    protected:
        template <typename Enum>
        NSQLite::TSQLiteStatement& Get(Enum e) {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            return static_cast<SqlSeq*>(this)->GetImpl(e);
        }

        template <typename Enum>
        const NSQLite::TSQLiteStatement& Get(Enum e) const {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            return static_cast<const SqlSeq*>(this)->GetImpl(e);
        }

        template <typename Enum>
        TStringBuf GetColumnBlob(Enum e, size_t idx, TStringBuf name) {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            auto& stmt = Get(e);
            Y_ENSURE(stmt.ColumnName(idx) == name);
            return stmt.ColumnBlob(idx);
        }

        template <typename Enum>
        i64 GetColumnInt64(Enum e, size_t idx, TStringBuf name) {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            auto& stmt = Get(e);
            Y_ENSURE(stmt.ColumnName(idx) == name);
            return stmt.ColumnInt64(idx);
        }

        template <typename Enum>
        TStringBuf GetColumnText(Enum e, size_t idx, TStringBuf name) {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            auto& stmt = Get(e);
            Y_ENSURE(stmt.ColumnName(idx) == name);
            return stmt.ColumnText(idx);
        }

        template <typename Enum>
        void ColumnAccept(Enum e, size_t idx, TStringBuf name, NSQLite::ISQLiteColumnVisitor& c) {
            static_assert(std::is_same<typename SqlSeq::TEnum, Enum>::value);
            auto& stmt = Get(e);
            Y_ENSURE(stmt.ColumnName(idx) == name);
            stmt.ColumnAccept(idx, c);
        }

        template <typename Enum>
        // Exactly one row.
        i64 GetRowid(Enum e) {
            static_assert(
                std::is_same<typename SqlSeq::TEnum, Enum>::value);
            Y_ABORT_UNLESS(Get(e).Step() && Get(e).ColumnCount() == 1);
            i64 rowid = GetColumnInt64(e, 0, "Rowid");
            Y_ABORT_UNLESS(!Get(e).Step());
            return rowid;
        }

        template <typename Enum>
        // Exactly one row.
        TMaybe<i64> GetMaybeRowid(Enum e) {
            static_assert(
                std::is_same<typename SqlSeq::TEnum, Enum>::value);
            if (!Get(e).Step()) {
                return TMaybe<i64>();
            }
            Y_ABORT_UNLESS(Get(e).ColumnCount() == 1);
            i64 rowid = GetColumnInt64(e, 0, "Rowid");
            Y_ABORT_UNLESS(!Get(e).Step());
            return MakeMaybe(rowid);
        }
    };

    template <typename SqlSeq, ELockKind Kind>
    class Utilities: public Wrappers<SqlSeq> {
    protected:
        Utilities(NSQLite::TSQLiteDB& db, size_t retries, TRWMutex& dbMutex)
            : Begin_(db, "BEGIN " + ToString(Kind) + ";")
            , Commit_(db, "COMMIT;")
            , Rollback_(db, "ROLLBACK;")
            , BigDBMutex_(dbMutex)
            , Retries_(retries)
        {
        }

        void BeginTransaction() {
            Begin_.ResetHard();
            Begin_.Step();
        }

        void CommitTransaction() {
            Commit_.ResetHard();
            Commit_.Step();
        }

        void RollbackTransaction() {
            Rollback_.ResetHard();
            Rollback_.Step();
        }

        template <typename Func>
        TFunctionResult<Func> WrapInTransaction(Func&& func) {
            TWriteGuard lock(BigDBMutex_);

            EnforceBeginTransaction();

            try {
                auto res(func());
                EnforceCommitTransaction();
                return res;
            } catch (...) {
                RollbackTransaction();
                throw;
            }
        }

        template <typename Func>
        void WrapInTransactionVoid(Func&& func) {
            TWriteGuard lock(BigDBMutex_);

            EnforceBeginTransaction();
            try {
                func();
                EnforceCommitTransaction();
            } catch (...) {
                RollbackTransaction();
                throw;
            }
        }

    private:
        void EnforceBeginTransaction() {
            for (size_t i = 0; i != Retries_; i++) {
                try {
                    BeginTransaction();
                    return;
                } catch (const NSQLite::TSQLiteError& e) {
                    if (i + 1 == Retries_) {
                        throw;
                    }
                    auto err = e.GetErrorCode();
                    if (err == SQLITE_LOCKED || err == SQLITE_BUSY) {
                        continue;
                    } else {
                        throw;
                    }
                }
            }
        }

        void EnforceCommitTransaction() {
            for (size_t i = 0; i != Retries_; i++) {
                try {
                    CommitTransaction();
                    return;
                } catch (const NSQLite::TSQLiteError& e) {
                    if (i + 1 == Retries_) {
                        throw;
                    }
                    auto err = e.GetErrorCode();
                    if (err == SQLITE_LOCKED || err == SQLITE_BUSY) {
                        continue;
                    } else {
                        throw;
                    }
                }
            }
        }

    private:
        NSQLite::TSQLiteStatement Begin_;
        NSQLite::TSQLiteStatement Commit_;
        NSQLite::TSQLiteStatement Rollback_;

        /// It is faster to access single-threaded compared to processing SQLITE_BUSY.
        TRWMutex& BigDBMutex_;
        size_t Retries_;
    };

#define SQL_STMTS_UTILITIES()                        \
    template <bool ClearBinding>                     \
    friend class ::NCachesPrivate::TStmtResetter;    \
                                                     \
    using TEnum = TOuter::TEnum;                     \
                                                     \
    TSQLiteStatement& GetImpl(TEnum e) {             \
        return *Stmts_[e].Get();                     \
    }                                                \
                                                     \
    const TSQLiteStatement& GetImpl(TEnum e) const { \
        return *Stmts_[e].Get();                     \
    }

#define UTILITY_WRAPPERS(LockKind)            \
    using TSelf = TOuter::TImpl;              \
    using TBase = Utilities<TSelf, LockKind>; \
    SQL_STMTS_UTILITIES()

    // RAII for TSQLiteStatement.
    class TClearBindings {
    public:
        template <typename Stmt>
        static inline void Destroy(Stmt* stmt) noexcept {
            stmt->ClearBindings();
        }
    };

    /// Primitive parser of .sql resource files.
    /// Intended to keep db logic better localized, smaller amount of C++ stuff intermingled.
    ///
    /// _Severely_ restricted syntax, sequence of:
    ///   -- STMT: <name>
    ///   <SINGLE_SQL_STMT>;
    TSmallVec<std::pair<TString, TString>> GetDbSeq(TString f);

    /// Simple sequence w/o names.
    TSmallVec<TString> GetDbAnonymousSeq(TString f);

    /// Reset TSQLiteStatements.
    template <typename Container>
    void ResetStmts(Container& c, bool hard, bool clearBindings) {
        for (auto& v : c) {
            if (clearBindings) {
                v->ClearBindings();
            }
            if (hard) {
                v->ResetHard();
            } else {
                v->Reset();
            }
        }
    }

    /// Initialize TSQLiteStatements and check names of bound parameters.
    template <typename Enum, typename ResourceContainer, typename Container, typename Strings>
    void CheckAndSetSqlStmts(const ResourceContainer& resources, Container& c, const Strings* strs, NSQLite::TSQLiteDB& db) {
        for (auto& resource : resources) {
            for (auto& v : GetDbSeq(NResource::Find(resource))) {
                Enum ename;
                Y_ABORT_UNLESS(TryFromString<Enum>(v.first, ename));

                c[ename].Reset(new NSQLite::TSQLiteStatement(db, v.second));

                if (strs == nullptr) {
                    Y_ABORT_UNLESS(c[ename]->BoundParameterCount() == 0);
                }
                for (size_t i = 1, e = c[ename]->BoundParameterCount(); i <= e; ++i) {
                    TStringBuf pname(c[ename]->BoundParameterName(i));
                    Y_ABORT_UNLESS(std::find_if(std::begin(*strs), std::end(*strs),
                                          [pname](const char* name) -> bool { return TStringBuf(name) == pname; }) != std::end(*strs));
                }
            }
        }
        for (int i = 0; i <= static_cast<int>(Enum::Last); ++i) {
            Y_ABORT_UNLESS(c[i]);
        }
    }

    // Reset TSQLiteStatements
    template <bool ClearBindings>
    class TStmtResetter {
    public:
        template <typename Stmts>
        static inline void Destroy(Stmts* stmts) noexcept {
            // Resets Stmts_ member field.
            ::NCachesPrivate::ResetStmts(stmts->Stmts_, true, ClearBindings);
        }
    };
}
