#pragma once

// HDPHANTASOS_TRACE=1 prints what the delegate syncs and draws to stderr.

#include "pxr/base/tf/getenv.h"

#include <cstdarg>
#include <cstdio>

PXR_NAMESPACE_OPEN_SCOPE

inline bool HdPhantasosTraceEnabled()
{
    static const bool enabled = TfGetenvBool("HDPHANTASOS_TRACE", false);
    return enabled;
}

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline void HdPhantasosTrace(const char* format, ...)
{
    if (!HdPhantasosTraceEnabled()) return;
    // One write per line: meshes sync on several threads at once, and three
    // separate writes interleave.
    char line[1024];
    std::va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(line, sizeof line - 1, format, arguments);
    va_end(arguments);
    if (length < 0) return;
    std::fprintf(stderr, "[hdPhantasos] %s\n", line);
}

PXR_NAMESPACE_CLOSE_SCOPE
