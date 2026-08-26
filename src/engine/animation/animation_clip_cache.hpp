#ifndef HADES_ENGINE_ANIMATION_ANIMATION_CLIP_CACHE_HPP
#define HADES_ENGINE_ANIMATION_ANIMATION_CLIP_CACHE_HPP

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "animation_clip.hpp"
#include "animator_graph.hpp"

namespace hades
{
  /// Process-wide cache of authored animation assets: clips
  /// (`<assets>/.hades/animations/<name>.json`) and animator graphs
  /// (`<assets>/.hades/animators/<name>.json`).
  ///
  /// Mirrors ModelAssetCache: lazily loaded, failures cached so a broken file
  /// is reported once, one coarse mutex, and an asset root that the editor
  /// points at the workspace and the runtime at the project directory.
  ///
  /// Unlike models, these assets are written by the editor while the engine
  /// is running, so this cache also owns saving and per-path invalidation.
  class AnimationClipCache
  {
  public:
    static AnimationClipCache &instance();

    void setAssetRoot(const std::filesystem::path &root);
    std::filesystem::path assetRoot() const;

    static std::filesystem::path clips_directory(const std::filesystem::path &assetRoot);
    static std::filesystem::path graphs_directory(const std::filesystem::path &assetRoot);

    /// Resolve a clip reference. Accepts a bare name ("run"), a
    /// workspace-relative path, or an absolute path.
    std::filesystem::path resolveClipPath(const std::string &reference) const;
    std::filesystem::path resolveGraphPath(const std::string &reference) const;

    /// Load-on-demand accessors. Return nullptr when the reference is empty
    /// or the file failed to parse — see errorFor().
    const AnimationClipAsset *clip(const std::string &reference);
    const AnimatorGraph *graph(const std::string &reference);

    std::string errorFor(const std::string &reference) const;

    /// Write an asset to disk and refresh the cached copy. Returns false and
    /// fills `errorMessage` on failure.
    bool saveClip(const std::string &reference, const AnimationClipAsset &clip,
                  std::string *errorMessage = nullptr);
    bool saveGraph(const std::string &reference, const AnimatorGraph &graph,
                   std::string *errorMessage = nullptr);

    /// Delete the file and drop it from the cache.
    bool deleteClip(const std::string &reference, std::string *errorMessage = nullptr);
    bool deleteGraph(const std::string &reference, std::string *errorMessage = nullptr);

    /// Names (file stems) of every asset on disk, sorted. Cheap directory
    /// scans intended for editor combos, not per-frame use.
    std::vector<std::string> listClips() const;
    std::vector<std::string> listGraphs() const;

    /// Forget one entry so the next access re-reads it from disk.
    void invalidate(const std::string &reference);
    void clear();

  private:
    AnimationClipCache() = default;

    struct ClipEntry
    {
      std::unique_ptr<AnimationClipAsset> asset;
      std::string error;
    };

    struct GraphEntry
    {
      std::unique_ptr<AnimatorGraph> asset;
      std::string error;
    };

    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::unordered_map<std::string, ClipEntry> clips_;
    std::unordered_map<std::string, GraphEntry> graphs_;
    /// reference -> resolved key, so a cache hit costs one hash lookup rather
    /// than rebuilding a filesystem path. AnimatorInstance asks for the same
    /// handful of references 4-9 times per entity per frame, and the answer
    /// cannot change until `root_` does — which is the only place these are
    /// cleared. Entries are small and bounded by the set of references ever
    /// passed, misses included, so anything user-typed per keystroke should
    /// go through resolveClipPath() instead of clip().
    std::unordered_map<std::string, std::string> clipKeys_;
    std::unordered_map<std::string, std::string> graphKeys_;
  };
}

#endif
