#include "log.hpp"

#include <cstdarg>
#include <cstdio>

namespace hades
{
  namespace Log
  {
    namespace
    {
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

      void write_va(Level level, const char *tag, const char *fmt, std::va_list args)
      {
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
      }

      void write_no_tag(Level level, const char *fmt, std::va_list args)
      {
        write_va(level, nullptr, fmt, args);
      }
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
