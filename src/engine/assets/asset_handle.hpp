#ifndef HADES_ENGINE_ASSETS_ASSET_HANDLE_HPP
#define HADES_ENGINE_ASSETS_ASSET_HANDLE_HPP

#include <atomic>
#include <memory>
#include <string>

namespace hades
{
  enum class AssetState : int
  {
    Empty,
    Loading,
    Ready,
    Failed
  };

  template <typename T>
  class AssetHandle
  {
  public:
    AssetHandle() : slot_(std::make_shared<Slot>()) {}

    bool is_empty() const { return slot_->state.load(std::memory_order_acquire) == AssetState::Empty; }
    bool is_loading() const { return slot_->state.load(std::memory_order_acquire) == AssetState::Loading; }
    bool is_ready() const { return slot_->state.load(std::memory_order_acquire) == AssetState::Ready; }
    bool has_failed() const { return slot_->state.load(std::memory_order_acquire) == AssetState::Failed; }

    /// Returns a pointer to the loaded data, or nullptr if not ready.
    const T *get() const
    {
      if (slot_->state.load(std::memory_order_acquire) != AssetState::Ready)
      {
        return nullptr;
      }
      return &slot_->data;
    }

    /// Returns a mutable pointer to the loaded data, or nullptr if not ready.
    T *get_mutable()
    {
      if (slot_->state.load(std::memory_order_acquire) != AssetState::Ready)
      {
        return nullptr;
      }
      return &slot_->data;
    }

    /// Returns the error message if loading failed.
    const std::string &error() const { return slot_->error; }

    /// Mark this handle as loading. Called by the AssetManager.
    void set_loading()
    {
      slot_->state.store(AssetState::Loading, std::memory_order_release);
    }

    /// Set the loaded data and mark as ready. Called by the AssetManager worker thread.
    void set_data(T data)
    {
      slot_->data = std::move(data);
      slot_->state.store(AssetState::Ready, std::memory_order_release);
    }

    /// Set the error and mark as failed. Called by the AssetManager worker thread.
    void set_failed(const std::string &errorMessage)
    {
      slot_->error = errorMessage;
      slot_->state.store(AssetState::Failed, std::memory_order_release);
    }

    /// Returns true if the handle points to a valid slot (always true after construction).
    explicit operator bool() const { return slot_ != nullptr; }

  private:
    struct Slot
    {
      std::atomic<AssetState> state{AssetState::Empty};
      T data{};
      std::string error;
    };

    std::shared_ptr<Slot> slot_;
  };
}

#endif
