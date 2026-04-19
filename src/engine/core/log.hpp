#ifndef HADES_ENGINE_CORE_LOG_HPP
#define HADES_ENGINE_CORE_LOG_HPP

#include <cstdarg>
#include <filesystem>

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

    // Mirror all subsequent log output to the given file path (in addition to
    // stderr). Intended for exported runtimes launched without a terminal
    // attached — the file gives users a way to see why startup failed.
    // Returns true if the file was opened successfully.
    bool enable_file_logging(const std::filesystem::path &logPath);

    void debug(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void info(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void warn(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);
    void error(const char *fmt, ...) HADES_PRINTF_FORMAT(1, 2);

    // Tagged logging uses explicit names to avoid ambiguous overloads like
    // warn("message: %s", value) resolving as warn(tag, fmt, ...).
    void debug_tagged(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void info_tagged(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void warn_tagged(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
    void error_tagged(const char *tag, const char *fmt, ...) HADES_PRINTF_FORMAT(2, 3);
  }
}

#endif
