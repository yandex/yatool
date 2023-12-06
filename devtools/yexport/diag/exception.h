#pragma once

#include <util/generic/yexception.h>
#include <util/generic/va_args.h>

namespace NYexport {

class TYExportException: public yexception {
public:
    TYExportException();
    const TBackTrace* BackTrace() const noexcept override;

    TVector<TString> GetCallStack() const;

private:
    TBackTrace BackTrace_;
};

#define YEXPORT_THROW(...) Y_MACRO_IMPL_DISPATCHER_2(__VA_ARGS__, YEXPORT_THROW_IMPL_2, YEXPORT_THROW_IMPL_1)(__VA_ARGS__)
#define YEXPORT_THROW_IMPL_1(WHAT) ythrow TYExportException() << WHAT
#define YEXPORTYEXPORT_THROW_IMPL_2 (EX, WHAT) ythrow EX << WHAT

}
