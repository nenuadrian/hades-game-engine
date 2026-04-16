#include "log.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <system_error>

namespace hades
{
  namespace Log
  {
    namespace
    {
      std::mutex &file_mutex()
      {
        static std::mutex m;
        return m;
      }

      FILE *&file_handle()
      {
        static FILE *handle = nullptr;
        return handle;
      }

      const char *level_label(Level level)
      {
        switch (level)
        {
        case Level::Debug:
          return "DEBUG";
        case Level::Info:
          return "INFO";
        case Level::Warning:
          return "WARNING";
        case Level::Error:
          return "ERROR";
        }
        return "INFO";
      }

      // Format (fmt, args) into a std::string at the call site. Doing this at
      // the public API entry point keeps printf-style varargs from flowing
      // through any helper's format-string parameter downstream — the internal
      // emitters only see an already-rendered message. This also sidesteps
      // CodeQL's cpp/tainted-format-string false positives on variadic
      // forwarding chains.
      std::string format_message(const char *fmt, std::va_list args)
      {
        std::va_list probe;
        va_copy(probe, args);
        const int needed = std::vsnprintf(nullptr, 0, fmt, probe);
        va_end(probe);
        if (needed <= 0)
        {
          return {};
        }
        std::string result(static_cast<std::size_t>(needed), '\0');
        std::vsnprintf(result.data(), static_cast<std::size_t>(needed) + 1, fmt, args);
        return result;
      }

      std::string timestamp_prefix()
      {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::array<char, 32> buffer{};
        const int written = std::snprintf(
            buffer.data(), buffer.size(),
            "[%04d-%02d-%02d %02d:%02d:%02d] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        if (written <= 0)
        {
          return {};
        }
        return std::string(buffer.data(), static_cast<std::size_t>(written));
      }

      // Emit an already-formatted message. No format-string machinery from
      // here on: every write is a plain fputs of a constructed std::string.
      void emit(Level level, const char *tag, const std::string &message)
      {
        std::string line;
        line.reserve(message.size() + 32);
        line.push_back('[');
        line.append(level_label(level));
        line.push_back(']');
        if (tag != nullptr)
        {
          line.push_back('[');
          line.append(tag);
          line.push_back(']');
        }
        line.push_back(' ');
        line.append(message);
        line.push_back('\n');

        std::fputs(line.c_str(), stderr);

        std::lock_guard<std::mutex> lock(file_mutex());
        FILE *fileStream = file_handle();
        if (fileStream != nullptr)
        {
          const std::string prefixed = timestamp_prefix() + line;
          std::fputs(prefixed.c_str(), fileStream);
          std::fflush(fileStream);
        }
      }
    }

    bool enable_file_logging(const std::filesystem::path &logPath)
    {
      std::lock_guard<std::mutex> lock(file_mutex());
      FILE *&handle = file_handle();
      if (handle != nullptr)
      {
        std::fclose(handle);
        handle = nullptr;
      }
      std::error_code ec;
      std::filesystem::create_directories(logPath.parent_path(), ec);
      handle = std::fopen(logPath.string().c_str(), "w");
      if (handle == nullptr)
      {
        const std::string warning =
            "[WARNING] Failed to open log file: " + logPath.string() + "\n";
        std::fputs(warning.c_str(), stderr);
        return false;
      }
      const std::string header =
          timestamp_prefix() + "[INFO] hades log opened at " + logPath.string() + "\n";
      std::fputs(header.c_str(), handle);
      std::fflush(handle);
      return true;
    }

    void debug(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Debug, nullptr, message);
    }

    void info(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Info, nullptr, message);
    }

    void warn(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Warning, nullptr, message);
    }

    void error(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Error, nullptr, message);
    }

    void debug(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Debug, tag, message);
    }

    void info(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Info, tag, message);
    }

    void warn(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Warning, tag, message);
    }

    void error(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      const std::string message = format_message(fmt, args);
      va_end(args);
      emit(Level::Error, tag, message);
    }
  }
}
