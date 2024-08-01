#include <util/datetime/base.h>
#include <util/string/cast.h>


void __attribute__((constructor)) premain() {
    TString since_epoch = ToString(TInstant::Now().MicroSeconds());

    #ifdef _WIN32
        _putenv_s("_BINARY_START_TIMESTAMP", since_epoch.c_str());
    #else
        setenv("_BINARY_START_TIMESTAMP", since_epoch.c_str(), 1);
    #endif // ifdef _WIN32
}
