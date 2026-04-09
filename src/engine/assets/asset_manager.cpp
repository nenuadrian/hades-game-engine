#include "asset_manager.hpp"
#include "model_importer.hpp"

#include "../core/log.hpp"

namespace hades
{
  AssetManager &AssetManager::instance()
  {
    static AssetManager manager;
    return manager;
  }

  AssetManager::AssetManager()
  {
    start_worker();
  }

  AssetManager::~AssetManager()
  {
    shutdown();
  }

  void AssetManager::start_worker()
  {
    if (workerRunning_)
    {
      return;
    }

    shutdownRequested_ = false;
    workerRunning_ = true;
    workerThread_ = std::thread(&AssetManager::worker_loop, this);
  }

  void AssetManager::shutdown()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!workerRunning_)
      {
        return;
      }
      shutdownRequested_ = true;
    }
    condVar_.notify_one();

    if (workerThread_.joinable())
    {
      workerThread_.join();
    }
    workerRunning_ = false;
  }

  void AssetManager::clear_cache()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    modelCache_.clear();
  }

  std::string AssetManager::canonicalize(const std::filesystem::path &path) const
  {
    if (path.empty())
    {
      return {};
    }
    return std::filesystem::absolute(path).lexically_normal().string();
  }

  AssetHandle<ImportedModel> AssetManager::load_model(const std::filesystem::path &sourcePath)
  {
    const std::string key = canonicalize(sourcePath);
    if (key.empty())
    {
      AssetHandle<ImportedModel> handle;
      handle.set_failed("Empty asset path.");
      return handle;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = modelCache_.find(key);
    if (it != modelCache_.end())
    {
      return it->second;
    }

    AssetHandle<ImportedModel> handle;
    handle.set_loading();
    modelCache_[key] = handle;

    requestQueue_.push({key, handle});
    condVar_.notify_one();

    return handle;
  }

  AssetHandle<ImportedModel> AssetManager::load_model_sync(const std::filesystem::path &sourcePath)
  {
    const std::string key = canonicalize(sourcePath);
    if (key.empty())
    {
      AssetHandle<ImportedModel> handle;
      handle.set_failed("Empty asset path.");
      return handle;
    }

    // Check cache first.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = modelCache_.find(key);
      if (it != modelCache_.end())
      {
        auto &handle = it->second;
        // If already ready or failed, return immediately.
        if (handle.is_ready() || handle.has_failed())
        {
          return handle;
        }

        // If loading async, wait for it.
        if (handle.is_loading())
        {
          // Release lock, spin-wait for completion.
          auto handleCopy = handle;
          // We need to release the lock to allow the worker to complete.
          mutex_.unlock();
          while (handleCopy.is_loading())
          {
            std::this_thread::yield();
          }
          mutex_.lock();
          return handleCopy;
        }
      }
    }

    // Not in cache — load synchronously on this thread.
    AssetHandle<ImportedModel> handle;

    std::string errorMessage;
    auto imported = ModelImporter::importFromFile(sourcePath, &errorMessage);
    if (imported.has_value())
    {
      handle.set_data(std::move(*imported));
    }
    else
    {
      handle.set_failed(errorMessage.empty() ? "Failed to import model." : errorMessage);
      hades::Log::warn("failed to load model from '%s': %s",
                   sourcePath.string().c_str(), handle.error().c_str());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    modelCache_[key] = handle;
    return handle;
  }

  void AssetManager::worker_loop()
  {
    while (true)
    {
      LoadRequest request;

      {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this]
                      { return shutdownRequested_ || !requestQueue_.empty(); });

        if (shutdownRequested_ && requestQueue_.empty())
        {
          return;
        }

        if (requestQueue_.empty())
        {
          continue;
        }

        request = std::move(requestQueue_.front());
        requestQueue_.pop();
      }

      // Load the model outside the lock (Assimp::Importer is per-instance thread-safe).
      std::string errorMessage;
      auto imported = ModelImporter::importFromFile(request.canonicalPath, &errorMessage);

      if (imported.has_value())
      {
        request.handle.set_data(std::move(*imported));
      }
      else
      {
        request.handle.set_failed(errorMessage.empty() ? "Failed to import model." : errorMessage);
        hades::Log::warn("async model load failed for '%s': %s",
                     request.canonicalPath.c_str(), request.handle.error().c_str());
      }
    }
  }
}
