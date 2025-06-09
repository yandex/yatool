#pragma once

#include <yt/cpp/mapreduce/interface/logging/logger.h>

namespace NPrivate {
    NYT::ILoggerPtr GetLogger();

    inline void LogMessage(NYT::ILogger::ELevel level, const ::TSourceLocation& sourceLocation, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        GetLogger()->Log(level, sourceLocation, format, args);
        va_end(args);
    }
}

#define LDEBUG(...) { NPrivate::LogMessage(NYT::ILogger::DEBUG, __LOCATION__, __VA_ARGS__); }
#define LINFO(...)  { NPrivate::LogMessage(NYT::ILogger::INFO, __LOCATION__, __VA_ARGS__); }
#define LERROR(...) { NPrivate::LogMessage(NYT::ILogger::ERROR, __LOCATION__, __VA_ARGS__); }
