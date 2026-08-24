#ifndef HADES_ENGINE_ASSETS_MODEL_ASSET_CACHE_HPP
#define HADES_ENGINE_ASSETS_MODEL_ASSET_CACHE_HPP

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "model_asset.hpp"

namespace hades
{
  /// Process-wide cache of imported model assets, keyed by resolved file
  /// path. Loads happen lazily on first get(); failures are cached so a
  /// broken path is reported once instead of re-imported every frame.
  ///
  /// The asset root anchors workspace-relative component paths: the editor
  /// sets it to the active workspace, the runtime to the project directory.
  class ModelAssetCache
  {
  public:
    static ModelAssetCache &instance();

    void setAssetRoot(const std::filesystem::path &root);
    std::filesystem::path assetRoot() const;

    /// Resolve a component asset path against the asset root.
    std::filesystem::path resolvePath(const std::string &assetPath) const;

    /// Get (loading on demand) the model at `assetPath`. Returns nullptr if
    /// the path is empty or the import failed — see errorFor().
    const ModelAsset *get(const std::string &assetPath);

    /// Last import error for `assetPath`, or empty when it loaded fine.
    std::string errorFor(const std::string &assetPath) const;

    /// Drop all cached assets (and cached failures).
    void clear();

  private:
    ModelAssetCache() = default;

    struct Entry
    {
      std::unique_ptr<ModelAsset> asset;
      std::string error;
    };

    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::unordered_map<std::string, Entry> entries_;
  };
}

#endif
