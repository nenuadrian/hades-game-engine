#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
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

      void write_timestamp(FILE *stream)
      {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::fprintf(stream, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
      }

      void write_va(Level level, const char *tag, const char *fmt, std::va_list args)
      {
        std::va_list argsCopy;
        va_copy(argsCopy, args);

        if (tag)
        {
          std::fprintf(stderr, "[%s][%s] ", level_label(level), tag);
        }
        else
        {
          std::fprintf(stderr, "[%s] ", level_label(level));
        }
        std::vfprintf(stderr, fmt, args);
        std::fprintf(stderr, "\n");

        std::lock_guard<std::mutex> lock(file_mutex());
        FILE *fileStream = file_handle();
        if (fileStream != nullptr)
        {
          write_timestamp(fileStream);
          if (tag)
          {
            std::fprintf(fileStream, "[%s][%s] ", level_label(level), tag);
          }
          else
          {
            std::fprintf(fileStream, "[%s] ", level_label(level));
          }
          std::vfprintf(fileStream, fmt, argsCopy);
          std::fprintf(fileStream, "\n");
          std::fflush(fileStream);
        }

        va_end(argsCopy);
      }

      void write_no_tag(Level level, const char *fmt, std::va_list args)
      {
        write_va(level, nullptr, fmt, args);
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
        std::fprintf(stderr, "[WARNING] Failed to open log file: %s\n", logPath.string().c_str());
        return false;
      }
      write_timestamp(handle);
      std::fprintf(handle, "[INFO] hades log opened at %s\n", logPath.string().c_str());
      std::fflush(handle);
      return true;
    }

    void debug(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_no_tag(Level::Debug, fmt, args);
      va_end(args);
    }

    void info(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_no_tag(Level::Info, fmt, args);
      va_end(args);
    }

    void warn(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_no_tag(Level::Warning, fmt, args);
      va_end(args);
    }

    void error(const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_no_tag(Level::Error, fmt, args);
      va_end(args);
    }

    void debug(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_va(Level::Debug, tag, fmt, args);
      va_end(args);
    }

    void info(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_va(Level::Info, tag, fmt, args);
      va_end(args);
    }

    void warn(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_va(Level::Warning, tag, fmt, args);
      va_end(args);
    }

    void error(const char *tag, const char *fmt, ...)
    {
      std::va_list args;
      va_start(args, fmt);
      write_va(Level::Error, tag, fmt, args);
      va_end(args);
    }
  }
}
