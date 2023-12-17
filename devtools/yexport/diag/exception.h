#pragma once

#include <util/generic/yexception.h>
#include <util/generic/va_args.h>

namespace NYexport {

class TYExportException: public yexception {
public:
    TYExportException();
    TYExportException(const std::string& what);
    const TBackTrace* BackTrace() const noexcept override;

    TVector<TString> GetCallStack() const;

private:
    TBackTrace BackTrace_;
};

#define YEXPORT_THROW(...) Y_MACRO_IMPL_DISPATCHER_2(__VA_ARGS__, YEXPORT_THROW_IMPL_2, YEXPORT_THROW_IMPL_1)(__VA_ARGS__)
#define YEXPORT_THROW_IMPL_1(WHAT) throw ::NYexport::TYExportException() << WHAT
#define YEXPORT_THROW_IMPL_2(EX, WHAT) throw EX << WHAT

#define YEXPORT_VERIFY(...) Y_MACRO_IMPL_DISPATCHER_3(__VA_ARGS__, YEXPORT_VERIFY_IMPL_3, YEXPORT_VERIFY_IMPL_2)(__VA_ARGS__)
#define YEXPORT_VERIFY_IMPL_3(CONDITION, EXCEPTION, MESSAGE) \
    do {                                                     \
        if (!(CONDITION)) {                                  \
            YEXPORT_THROW(EXCEPTION, MESSAGE);               \
        }                                                    \
    } while (false)
#define YEXPORT_VERIFY_IMPL_2(CONDITION, MESSAGE) \
    do {                                          \
        if (!(CONDITION)) {                       \
            YEXPORT_THROW(MESSAGE);               \
        }                                         \
    } while (false)

}
