#include "model_asset_cache.hpp"

#include <utility>

#include "../animation/rig_asset.hpp"
#include "../core/log.hpp"
#include "model_loader.hpp"

namespace hades
{
  namespace
  {
    /// Purely diagnostic. The generation hold bounds the retirement list
    /// structurally, so a list this long means the cache is being mutated far
    /// more often than the once-or-twice-a-frame it is designed around, and
    /// the "bounded and small" claim in the header no longer holds.
    constexpr std::size_t kRetiredWarnCount = 64;
  }

  ModelAssetCache &ModelAssetCache::instance()
  {
    static ModelAssetCache cache;
    return cache;
  }

  void ModelAssetCache::setAssetRoot(const std::filesystem::path &root)
  {
    // A root change is a drain point: it is the workspace-switch path, which
    // runs before any panel builds a render list, so nothing can be holding a
    // pointer from the previous root.
    std::vector<std::unique_ptr<ModelAsset>> doomed;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      doomed = advance_generation_locked();
      root_ = root;
    }
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
      entry.importedBoneCount = asset->bones.size();
      entry.rigError = apply_rig_overlay(assetPath, *asset);
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

  std::string ModelAssetCache::rigErrorFor(const std::string &assetPath) const
  {
    if (assetPath.empty())
    {
      return {};
    }

    const std::string key = resolvePath(assetPath).string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    return it != entries_.end() ? it->second.rigError : std::string{};
  }

  std::size_t ModelAssetCache::importedBoneCount(const std::string &assetPath) const
  {
    if (assetPath.empty())
    {
      return 0;
    }

    const std::string key = resolvePath(assetPath).string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    return it != entries_.end() ? it->second.importedBoneCount : 0;
  }

  void ModelAssetCache::invalidate(const std::string &assetPath)
  {
    if (assetPath.empty())
    {
      return;
    }

    // resolvePath takes the mutex itself, so it has to run before we do.
    const std::string key = resolvePath(assetPath).string();

    // Declared outside the lock so the expired assets are freed after it is
    // released — see advance_generation_locked().
    std::vector<std::unique_ptr<ModelAsset>> doomed;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      doomed = advance_generation_locked();

      auto it = entries_.find(key);
      if (it != entries_.end())
      {
        // Retired, not destroyed: a render list built earlier this frame may
        // still hold this exact pointer as RenderItem::model.
        retire_locked(std::move(it->second.asset));
        entries_.erase(it);
      }
    }
  }

  void ModelAssetCache::clear()
  {
    std::vector<std::unique_ptr<ModelAsset>> doomed;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      doomed = advance_generation_locked();

      for (auto &entry : entries_)
      {
        retire_locked(std::move(entry.second.asset));
      }
      entries_.clear();
    }
  }

  void ModelAssetCache::collectRetired()
  {
    std::vector<std::unique_ptr<ModelAsset>> doomed;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      doomed = advance_generation_locked();
    }
  }

  std::size_t ModelAssetCache::retiredAssetCount() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return retired_.size();
  }

  void ModelAssetCache::retire_locked(std::unique_ptr<ModelAsset> asset)
  {
    if (asset == nullptr)
    {
      // A cached *failure* owns no asset; there is nothing to keep alive.
      return;
    }

    retired_.push_back(RetiredAsset{std::move(asset), generation_});
    if (retired_.size() == kRetiredWarnCount)
    {
      Log::warn_tagged(
          "model_cache",
          "%zu model assets awaiting retirement — the cache is being invalidated far more often than once a frame",
          retired_.size());
    }
  }

  std::vector<std::unique_ptr<ModelAsset>> ModelAssetCache::advance_generation_locked()
  {
    ++generation_;

    // retired_ is appended in generation order, so everything expired is a
    // prefix. `generation + hold <= generation_` keeps an asset alive for the
    // call that retired it plus `hold - 1` further calls; with hold == 2 it
    // survives the next invalidate()/clear() too, which is what makes a frame
    // that mutates the cache twice safe.
    std::vector<std::unique_ptr<ModelAsset>> doomed;
    std::size_t expired = 0;
    while (expired < retired_.size() &&
           retired_[expired].generation + kRetiredGenerationHold <= generation_)
    {
      doomed.push_back(std::move(retired_[expired].asset));
      ++expired;
    }

    if (expired > 0)
    {
      retired_.erase(retired_.begin(), retired_.begin() + static_cast<std::ptrdiff_t>(expired));
    }
    return doomed;
  }

  std::string ModelAssetCache::apply_rig_overlay(const std::string &assetPath, ModelAsset &asset) const
  {
    // Called with mutex_ held: read root_ directly rather than through
    // assetRoot(), which would deadlock on the non-recursive mutex.
    if (root_.empty())
    {
      return {};
    }

    const std::filesystem::path rigPath = rig_path_for_model(root_, assetPath);
    std::error_code ec;
    if (!std::filesystem::exists(rigPath, ec) || ec)
    {
      return {};
    }

    RigAsset rig;
    std::string error;
    if (!load_rig_asset(rigPath, rig, &error))
    {
      Log::warn_tagged("model_cache", "rig '%s' ignored: %s", rigPath.string().c_str(), error.c_str());
      return error;
    }

    if (!apply_rig(asset, rig, &error))
    {
      Log::warn_tagged("model_cache", "rig '%s' could not be applied: %s", rigPath.string().c_str(), error.c_str());
      return error;
    }

    Log::info_tagged("model_cache", "applied rig '%s' (%zu joints)", rigPath.string().c_str(), rig.joints.size());
    return {};
  }
}
