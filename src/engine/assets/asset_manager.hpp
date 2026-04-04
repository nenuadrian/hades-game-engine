#ifndef HADES_ENGINE_ASSETS_ASSET_MANAGER_HPP
#define HADES_ENGINE_ASSETS_ASSET_MANAGER_HPP

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include "asset_handle.hpp"
#include "imported_model.hpp"

namespace hades
{
  class AssetManager
  {
  public:
    static AssetManager &instance();

    /// Load a model asynchronously. Returns a handle immediately.
    /// The model will be loaded on a background thread.
    AssetHandle<ImportedModel> load_model(const std::filesystem::path &sourcePath);

    /// Load a model synchronously (blocking). Returns a handle with data already ready.
    /// Uses the cache — if an async load is in-flight, waits for it.
    AssetHandle<ImportedModel> load_model_sync(const std::filesystem::path &sourcePath);

    /// Shut down the background worker thread. Call before application exit.
    void shutdown();

    /// Clear the asset cache.
    void clear_cache();

  private:
    AssetManager();
    ~AssetManager();

    AssetManager(const AssetManager &) = delete;
    AssetManager &operator=(const AssetManager &) = delete;

    void start_worker();
    void worker_loop();

    std::string canonicalize(const std::filesystem::path &path) const;

    struct LoadRequest
    {
      std::string canonicalPath;
      AssetHandle<ImportedModel> handle;
    };

    std::mutex mutex_;
    std::condition_variable condVar_;
    std::queue<LoadRequest> requestQueue_;
    std::unordered_map<std::string, AssetHandle<ImportedModel>> modelCache_;

    std::thread workerThread_;
    bool workerRunning_ = false;
    bool shutdownRequested_ = false;
  };
}

#endif
