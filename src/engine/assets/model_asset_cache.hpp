#ifndef HADES_ENGINE_ASSETS_MODEL_ASSET_CACHE_HPP
#define HADES_ENGINE_ASSETS_MODEL_ASSET_CACHE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "model_asset.hpp"

namespace hades
{
  /// Process-wide cache of imported model assets, keyed by resolved file
  /// path. Loads happen lazily on first get(); failures are cached so a
  /// broken path is reported once instead of re-imported every frame.
  ///
  /// The asset root anchors workspace-relative component paths: the editor
  /// sets it to the active workspace, the runtime to the project directory.
  ///
  /// ## Retirement — why invalidate() does not free anything
  ///
  /// `get()` hands out a raw `const ModelAsset *` that callers hold for the
  /// rest of the frame. `RenderItem::model` (render_types.hpp) states the
  /// contract: "owned by ModelAssetCache and stays valid for the frame". The
  /// Vulkan backend leans on it hard — `pending_target_renders_` keeps
  /// by-value copies of whole render lists that are not recorded until
  /// frame_render, and the mesh cache re-uploads vertex buffers straight out
  /// of `*RenderItem::model`.
  ///
  /// Destroying the asset inside invalidate()/clear() would make that promise
  /// false the moment a panel that saves a rig runs *after* a panel that
  /// builds a model render list: the pipeline would read freed memory and
  /// then upload vertex buffers FROM freed memory. Today no editor ordering
  /// lines that up, but nothing enforces it — the promise holds by accident
  /// of plugin order, and reordering two panels would silently break it.
  ///
  /// So invalidate() and clear() *retire* the ModelAsset — they move the
  /// owning unique_ptr out of the map into a retirement list — instead of
  /// destroying it. A retired asset is never destroyed by the call that
  /// retired it. Each of invalidate()/clear()/setAssetRoot() opens a new
  /// retirement generation and frees only what is older than
  /// `kRetiredGenerationHold` generations, so an asset outlives at least the
  /// next such call as well. Cache mutations happen at most once or twice per
  /// frame, so that is at least one full frame, whatever order the panels run
  /// in. (Same shape as `VulkanMeshPipeline`'s `retireFrame_`: hand ownership
  /// to a queue, drain it a generation later.)
  ///
  /// The memory cost is bounded and explicit: at most the assets retired by
  /// the last `kRetiredGenerationHold` mutations. Normally that is a single
  /// re-imported model; the worst case is two clear()s' worth of cache, which
  /// only a workspace switch produces. `retiredAssetCount()` makes the bound
  /// observable instead of a claim.
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

    /// Why the authored rig for `assetPath` was not applied, or empty when
    /// there is no rig or it applied cleanly. Kept apart from errorFor(): the
    /// model itself loaded, so reporting this as a load failure would be a
    /// lie, and a refused rig otherwise leaves no trace outside the log.
    std::string rigErrorFor(const std::string &assetPath) const;

    /// Size of the bone palette as the file was imported, before any rig
    /// overlay was applied. This is the count `apply_rig` retains, so it is
    /// what a bone-budget warning has to measure against — `get()` hands back
    /// an already-overlaid asset whose `bones` include the current rig.
    std::size_t importedBoneCount(const std::string &assetPath) const;

    /// Forget one entry so the next get() re-imports it. Used after the
    /// animation editor writes a rig overlay for that model.
    ///
    /// The previous ModelAsset is retired, not destroyed, so a pointer
    /// already handed out this frame stays readable — see "Retirement".
    void invalidate(const std::string &assetPath);

    /// Drop all cached assets (and cached failures). Live assets are retired,
    /// not destroyed — see "Retirement".
    void clear();

    /// Open a new retirement generation and free whatever has outlived the
    /// hold, without otherwise touching the cache.
    ///
    /// Nothing calls this yet: the mutating calls above already drain, which
    /// is what bounds the memory. It exists for a caller that owns a real
    /// frame boundary (where no render list is outstanding by construction),
    /// because ticking it there is a strictly tighter bound than ticking it
    /// on cache mutations. It applies the same generation hold either way, so
    /// calling it mid-frame costs memory rather than correctness.
    void collectRetired();

    /// Assets retired but not yet destroyed. For tests and diagnostics.
    std::size_t retiredAssetCount() const;

  private:
    ModelAssetCache() = default;

    /// Retirement generations kept alive, counted in cache mutations.
    ///
    /// 1 would already close the reported hole — an asset would survive the
    /// invalidate() that retired it and die at the next one. 2 also survives
    /// a frame that mutates the cache *twice* (a render-list panel sandwiched
    /// between two rig saves), which is exactly the future reordering that
    /// would make the hole live again. Each extra generation costs at most
    /// one more round of retired assets, so the second one is cheap
    /// insurance; a third would not buy a new class of safety.
    static constexpr std::uint64_t kRetiredGenerationHold = 2;

    /// Apply the authored rig stored under `.hades/rigs/`, if any, to a model
    /// that was just imported. Called with the mutex held. Returns the reason
    /// the rig was not applied, or an empty string when there was nothing to
    /// apply or it applied cleanly.
    std::string apply_rig_overlay(const std::string &assetPath, ModelAsset &asset) const;

    /// Take ownership of an asset that is leaving entries_. Called with the
    /// mutex held.
    void retire_locked(std::unique_ptr<ModelAsset> asset);

    /// Open a new retirement generation and hand back everything that has
    /// outlived the hold. Called with the mutex held; the caller destroys the
    /// returned assets *after* releasing it, because freeing a multi-megabyte
    /// mesh should not hold every other thread out of the cache.
    std::vector<std::unique_ptr<ModelAsset>> advance_generation_locked();

    struct Entry
    {
      std::unique_ptr<ModelAsset> asset;
      std::string error;
      std::string rigError;
      std::size_t importedBoneCount = 0;
    };

    struct RetiredAsset
    {
      std::unique_ptr<ModelAsset> asset;
      std::uint64_t generation = 0;
    };

    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::unordered_map<std::string, Entry> entries_;

    /// Assets dropped from entries_ that a caller may still be holding for
    /// this frame. Appended in generation order, so the expired prefix is a
    /// contiguous range.
    std::vector<RetiredAsset> retired_;
    std::uint64_t generation_ = 0;
  };
}

#endif
