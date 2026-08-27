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

    /// Separator between a model reference and a clip name in an imported
    /// clip reference: "character.fbx#Walk".
    static constexpr char kImportedClipSeparator = '#';

    /// Split "model#clip" into its halves. False when `reference` names an
    /// authored clip, in which case the outputs are untouched.
    static bool split_imported_reference(const std::string &reference, std::string &outModel,
                                         std::string &outClip);

    /// Resolve a clip reference. Accepts a bare name ("run"), a
    /// workspace-relative path, an absolute path, or an imported reference
    /// ("character.fbx#Walk"), which resolves to the model file that holds
    /// the clip.
    std::filesystem::path resolveClipPath(const std::string &reference) const;
    std::filesystem::path resolveGraphPath(const std::string &reference) const;

    /// Load-on-demand accessors. Return nullptr when the reference is empty
    /// or the file failed to parse — see errorFor().
    ///
    /// A reference of the form "character.fbx#Walk" names an animation that
    /// came *inside* a model file. It is baked through
    /// `AnimationClipAsset::bake_from_model` on first use and memoised like
    /// any other clip, so an animator can crossfade, blend and fire events on
    /// imported animation without it first being copied into
    /// `.hades/animations/`. The bake is redone when the model is re-imported
    /// underneath it. Clip names inside one model are matched first-wins,
    /// the same rule the importer's own channel binding uses.
    const AnimationClipAsset *clip(const std::string &reference);

    /// Imported clip references ("<modelReference>#<name>") for every
    /// animation inside `modelReference`, in file order. Empty when the model
    /// cannot be loaded or carries no animation.
    std::vector<std::string> listImportedClips(const std::string &modelReference);
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

    /// Install an in-memory graph under `reference`, replacing whatever is
    /// cached, without touching disk.
    ///
    /// This is how the Animator panel previews a graph it has not saved:
    /// AnimatorInstance resolves its graph through this cache every frame, so
    /// the only way to run an unsaved edit is to make the cache answer with
    /// it. The panel stages under a reference of its own rather than the
    /// graph's real name, so nothing else in the editor or a running game
    /// ever sees the working copy.
    void stageGraph(const std::string &reference, const AnimatorGraph &graph);

    /// Forget one entry so the next access re-reads it from disk.
    void invalidate(const std::string &reference);
    void clear();

  private:
    AnimationClipCache() = default;

    struct ClipEntry
    {
      std::unique_ptr<AnimationClipAsset> asset;
      std::string error;
      /// Imported entries only: the ModelAsset the bake was taken from, so a
      /// re-import can be noticed. ModelAssetCache *retires* rather than
      /// destroys, so a replacement never lands on this address while the
      /// pointer is still worth comparing — but the node count is compared
      /// too, because "never" resting on an allocator is not a guarantee.
      const ModelAsset *sourceAsset = nullptr;
      std::size_t sourceNodeCount = 0;
    };

    /// Bake — or return the memoised bake of — a clip that lives inside a
    /// model file.
    const AnimationClipAsset *imported_clip(const std::string &reference,
                                            const std::string &modelReference,
                                            const std::string &clipName);

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
