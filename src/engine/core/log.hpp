#ifndef HADES_ENGINE_CORE_LOG_HPP
#define HADES_ENGINE_CORE_LOG_HPP

#include <cstdarg>

#if defined(__GNUC__) || defined(__clang__)
#define HADES_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define HADES_PRINTF_FORMAT(fmt, args)
#endif

namespace hades
{
  namespace Log
  {
    enum class Level
    {
      Debug,
      Info,
      Warning,
      Error,
    };

    void debug(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void info(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void warn(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void error(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);

    void debug(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void info(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void warn(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void error(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
  }
}

#endif
