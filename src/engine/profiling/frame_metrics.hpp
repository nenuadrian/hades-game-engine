#ifndef HADES_ENGINE_PROFILING_FRAME_METRICS_HPP
#define HADES_ENGINE_PROFILING_FRAME_METRICS_HPP

#ifdef HADES_ENABLE_FRAME_METRICS

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace hades
{
  class FrameMetrics
  {
  public:
    struct Entry
    {
      std::string name;
      double lastMs = 0.0;
      double totalMs = 0.0;
      std::uint32_t count = 0;
      std::uint32_t frames = 0;

      // Accumulator for current frame (not exposed).
      double pendingMs = 0.0;
      std::uint32_t pendingCount = 0;
    };

    static FrameMetrics &instance()
    {
      static FrameMetrics s;
      return s;
    }

    void begin(const char *name)
    {
      (void)name;
      pending_.push_back(std::chrono::steady_clock::now());
    }

    void end(const char *name)
    {
      const auto now = std::chrono::steady_clock::now();
      if (pending_.empty())
      {
        return;
      }
      const auto start = pending_.back();
      pending_.pop_back();
      const double ms = std::chrono::duration<double, std::milli>(now - start).count();

      Entry *entry = find_entry(name);
      if (entry == nullptr)
      {
        entries_.push_back(Entry{name, 0.0, 0.0, 0, 0, 0.0, 0});
        entry = &entries_.back();
      }
      entry->pendingMs += ms;
      entry->pendingCount += 1;
    }

    void end_frame()
    {
      for (auto &entry : entries_)
      {
        entry.lastMs = entry.pendingMs;
        entry.totalMs += entry.pendingMs;
        entry.count = entry.pendingCount;
        entry.frames += 1;
        entry.pendingMs = 0.0;
        entry.pendingCount = 0;
      }
    }

    const std::vector<Entry> &entries() const
    {
      return entries_;
    }

  private:
    FrameMetrics() = default;

    Entry *find_entry(const char *name)
    {
      for (auto &entry : entries_)
      {
        if (entry.name == name)
        {
          return &entry;
        }
      }
      return nullptr;
    }

    std::vector<Entry> entries_;
    std::vector<std::chrono::steady_clock::time_point> pending_;
  };

  class ScopedFrameMetric
  {
  public:
    explicit ScopedFrameMetric(const char *name) : name_(name)
    {
      FrameMetrics::instance().begin(name_);
    }

    ~ScopedFrameMetric()
    {
      FrameMetrics::instance().end(name_);
    }

    ScopedFrameMetric(const ScopedFrameMetric &) = delete;
    ScopedFrameMetric &operator=(const ScopedFrameMetric &) = delete;

  private:
    const char *name_;
  };
}

#define HADES_FRAME_METRIC_SCOPE(name) \
  ::hades::ScopedFrameMetric _hades_metric_##__LINE__(name)

#define HADES_FRAME_METRIC_BEGIN(name) \
  ::hades::FrameMetrics::instance().begin(name)

#define HADES_FRAME_METRIC_END(name) \
  ::hades::FrameMetrics::instance().end(name)

#define HADES_FRAME_METRIC_END_FRAME() \
  ::hades::FrameMetrics::instance().end_frame()

#else

#define HADES_FRAME_METRIC_SCOPE(name) ((void)0)
#define HADES_FRAME_METRIC_BEGIN(name) ((void)0)
#define HADES_FRAME_METRIC_END(name) ((void)0)
#define HADES_FRAME_METRIC_END_FRAME() ((void)0)

#endif

#endif
