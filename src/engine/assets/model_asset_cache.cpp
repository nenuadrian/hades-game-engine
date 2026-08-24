#include "model_asset_cache.hpp"

#include "../core/log.hpp"
#include "model_loader.hpp"

namespace hades
{
  ModelAssetCache &ModelAssetCache::instance()
  {
    static ModelAssetCache cache;
    return cache;
  }

  void ModelAssetCache::setAssetRoot(const std::filesystem::path &root)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root_ = root;
  }

  std::filesystem::path ModelAssetCache::assetRoot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_;
  }

  std::filesystem::path ModelAssetCache::resolvePath(const std::string &assetPath) const
  {
    std::filesystem::path path(assetPath);
    if (path.is_absolute())
    {
      return path;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return root_.empty() ? path : root_ / path;
  }

  const ModelAsset *ModelAssetCache::get(const std::string &assetPath)
  {
    if (assetPath.empty())
    {
      return nullptr;
    }

    const std::string key = resolvePath(assetPath).string();

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end())
    {
      return it->second.asset.get();
    }

    Entry entry;
    auto asset = std::make_unique<ModelAsset>();
    if (load_model_asset(key, *asset, &entry.error))
    {
      Log::info_tagged(
          "model_cache",
          "loaded '%s' (%zu meshes, %zu bones, %zu clips)",
          key.c_str(), asset->meshes.size(), asset->bones.size(), asset->clips.size());
      entry.asset = std::move(asset);
    }
    else
    {
      Log::error_tagged("model_cache", "failed to load '%s': %s", key.c_str(), entry.error.c_str());
    }

    auto [inserted, ok] = entries_.emplace(key, std::move(entry));
    (void)ok;
    return inserted->second.asset.get();
  }

  std::string ModelAssetCache::errorFor(const std::string &assetPath) const
  {
    if (assetPath.empty())
    {
      return {};
    }

    const std::string key = resolvePath(assetPath).string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    return it != entries_.end() ? it->second.error : std::string{};
  }

  void ModelAssetCache::clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
  }
}
