#pragma once

#if defined(__linux__)
#define LINUX_PLATFORM
#endif
#if defined(__FreeBSD__)
#define FREEBSD_PLATFORM
#endif
#if defined(LINUX_PLATFORM) || defined(FREEBSD_PLATFORM)
#if !defined(__GNUC__)
#error GCC compiler required.
#else
#define GCC_COMPILER //check version?
#endif
#endif
#if defined(_darwin_)
#define DARWIN_PLATFORM
#endif
#if defined(_WIN64)
#define WIN64_PLATFORM
#endif
#if defined(_WIN32) && !defined(_WIN64)
#define WIN32_PLATFORM
#endif
#if defined(WIN64_PLATFORM) || defined(WIN32_PLATFORM)
#if !defined(_MSC_VER)
#error MSVC compiler required.
#else
#if (_MSVC_VER >= 1400)
#define MSVC_COMPILER
#else
#define OLD_MSVC_COMPILER //older then 8.0 (2005)
#endif
#endif
#endif
#if defined(__CYGWIN__)
#define CYGWIN_PLATFORM
#if defined(__GNUC__)
#define CYGWIN_GCC_COMPILER
#else
#if defined(_MSC_VER)
#if (_MSVC_VER >= 1400)
#define CYGWIN_MSVC_COMPILER
#else
#define CYGWIN_OLD_MSVC_COMPILER //older then 8.0 (2005)
#endif
#else
#error C++ compiler required.
#endif
#endif
#endif
#if !defined(LINUX_PLATFORM) && !defined(FREEBSD_PLATFORM) && !defined(CYGWIN_PLATFORM) && !defined(WIN32_PLATFORM) && !defined(WIN64_PLATFORM) && !defined(DARWIN_PLATFORM)
#error "This platform is not supported (Can't find any suitable platform- or compiler-specific preprocessor macro)."
#endif
