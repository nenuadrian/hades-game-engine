#ifndef HADES_ENGINE_CORE_LOG_HPP
#define HADES_ENGINE_CORE_LOG_HPP

#include <cstdarg>

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

    void debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

    void debug(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void info(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void warn(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void error(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  }
}

#endif
