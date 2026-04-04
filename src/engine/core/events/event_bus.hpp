#ifndef HADES_ENGINE_CORE_EVENTS_EVENT_BUS_HPP
#define HADES_ENGINE_CORE_EVENTS_EVENT_BUS_HPP

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace hades
{
  class EventBus
  {
  public:
    /// Publish an event. It will be delivered to subscribers on the next dispatch().
    template <typename T>
    void publish(const T &event)
    {
      pendingEvents_[std::type_index(typeid(T))].push_back(event);
    }

    /// Subscribe to an event type. The callback is invoked during dispatch()
    /// for each event of type T that was published since the last dispatch().
    template <typename T>
    void subscribe(std::function<void(const T &)> callback)
    {
      auto wrapper = [cb = std::move(callback)](const std::any &event)
      {
        cb(std::any_cast<const T &>(event));
      };
      subscribers_[std::type_index(typeid(T))].push_back(std::move(wrapper));
    }

    /// Deliver all pending events to their subscribers, then clear the pending queue.
    /// Call this once per frame, before system updates.
    void dispatch()
    {
      // Swap so that events published during dispatch don't cause infinite loops.
      auto events = std::move(pendingEvents_);
      pendingEvents_.clear();

      for (auto &[typeId, eventList] : events)
      {
        auto it = subscribers_.find(typeId);
        if (it == subscribers_.end())
        {
          continue;
        }

        for (const auto &event : eventList)
        {
          for (const auto &subscriber : it->second)
          {
            subscriber(event);
          }
        }
      }
    }

    /// Clear all pending events and all subscriptions.
    void clear()
    {
      pendingEvents_.clear();
      subscribers_.clear();
    }

    /// Clear only pending events (keep subscriptions).
    void clear_pending()
    {
      pendingEvents_.clear();
    }

  private:
    std::unordered_map<std::type_index, std::vector<std::any>> pendingEvents_;
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any &)>>> subscribers_;
  };
}

#endif
