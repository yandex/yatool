#include "logger.h"

#include <util/system/env.h>
#include <util/string/cast.h>


namespace NPrivate {
    NYT::ILoggerPtr CreateLogger() {
        NYT::ILogger::ELevel logLevel = FromStringWithDefault<NYT::ILogger::ELevel>(GetEnv("LOG_LEVEL"), NYT::ILogger::INFO);
        return NYT::CreateStdErrLogger(logLevel);
    }

    static NYT::ILoggerPtr Logger = CreateLogger();

    NYT::ILoggerPtr GetLogger() {
        return Logger;
    }

}
