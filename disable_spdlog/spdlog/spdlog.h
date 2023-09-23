// This header is a replacement for the real spdlog.h.
// In the case that we don't link with spdlog, we use this header instead
// allowing us to compile anyway

#include <cstdint>
#ifdef SPDLOG_INFO
    #undef SPDLOG_INFO
#endif
#define SPDLOG_INFO(...)

#ifdef SPDLOG_DEBUG
    #undef SPDLOG_DEBUG
#endif
#define SPDLOG_DEBUG(...)

#ifdef SPDLOG_ERROR
    #undef SPDLOG_ERROR
#endif
#define SPDLOG_ERROR(...)

#ifdef SPDLOG_WARN
    #undef SPDLOG_WARN
#endif
#define SPDLOG_WARN(...)

#ifdef SPDLOG_TRACE
    #undef SPDLOG_TRACE
#endif
#define SPDLOG_TRACE(...)

#define SPDLOG_OFF