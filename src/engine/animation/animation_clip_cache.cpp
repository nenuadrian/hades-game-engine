#include "animation_clip_cache.hpp"

#include <algorithm>
#include <fstream>
#include <memory>

#include "../assets/model_asset.hpp"
#include "../assets/model_asset_cache.hpp"
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    constexpr const char *kLogTag = "anim_cache";
    constexpr const char *kAssetExtension = ".json";

    /// Turn an asset reference into a file path. Three shapes are accepted so
    /// the editor can hand us whatever the user typed: an absolute path is
    /// taken as-is, anything that already looks like a path (a slash, or a
    /// .json suffix) is anchored at the asset root, and a bare name lands in
    /// the asset's own directory.
    std::filesystem::path resolve_reference(const std::filesystem::path &root,
                                            const std::filesystem::path &directory,
                                            const std::string &reference)
    {
      if (reference.empty())
      {
        return {};
      }

      std::filesystem::path path(reference);
      if (path.is_absolute())
      {
        return path;
      }

      const bool looksLikePath = reference.find('/') != std::string::npos ||
                                 reference.find('\\') != std::string::npos ||
                                 path.extension() == kAssetExtension;
      if (looksLikePath)
      {
        return root.empty() ? path : root / path;
      }

      return directory / (reference + kAssetExtension);
    }

    /// Memoised resolve_reference. The caller must already hold the cache
    /// mutex; the returned reference stays valid until `memo` is cleared.
    ///
    /// `directory` is a function rather than a path so the asset directory is
    /// only built on a miss — composing `<root>/.hades/animations` is itself
    /// three allocations, which is most of what this memo exists to avoid.
    using DirectoryFn = std::filesystem::path (*)(const std::filesystem::path &);

    const std::string &resolved_key(std::unordered_map<std::string, std::string> &memo,
                                    const std::filesystem::path &root, DirectoryFn directory,
                                    const std::string &reference)
    {
      auto it = memo.find(reference);
      if (it == memo.end())
      {
        it = memo.emplace(reference, resolve_reference(root, directory(root), reference).string()).first;
      }
      return it->second;
    }

    /// Read one JSON asset. `Asset::from_json` does the tolerant field
    /// reading; this only has to turn "no file" and "not JSON" into errors.
    template <typename Asset>
    bool load_json_asset(const std::filesystem::path &file, Asset &out, std::string *errorMessage)
    {
      std::ifstream input(file);
      if (!input.is_open())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "could not open " + file.string();
        }
        return false;
      }

      // Parsing without exceptions: a malformed file yields a discarded
      // value instead of unwinding through the render loop.
      const nlohmann::json document = nlohmann::json::parse(input, nullptr, false);
      if (document.is_discarded())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "invalid JSON in " + file.string();
        }
        return false;
      }

      return Asset::from_json(document, out, errorMessage);
    }

    bool write_json_asset(const std::filesystem::path &file, const nlohmann::json &document,
                          std::string *errorMessage)
    {
      const std::filesystem::path parent = file.parent_path();
      if (!parent.empty())
      {
        std::error_code errorCode;
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode)
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "could not create " + parent.string() + ": " + errorCode.message();
          }
          return false;
        }
      }

      std::ofstream stream(file, std::ios::out | std::ios::trunc);
      if (!stream.is_open())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "could not write " + file.string();
        }
        return false;
      }

      // error_handler_t::replace: dump()'s default handler throws
      // type_error.316 on a string that is not valid UTF-8, and strings here
      // come from user input and from filesystem paths. Saving is contracted
      // to report failure through the return value, so it must not throw.
      stream << document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';

      // Close explicitly: the insertion above only fills the stream buffer, so
      // a full disk usually surfaces at flush time. Letting the destructor
      // close would swallow that and report a successful save of a truncated
      // file.
      stream.close();
      if (!stream)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "failed while writing " + file.string();
        }
        return false;
      }

      return true;
    }

    /// The cached copy of a just-saved asset has to be what a *reload* would
    /// produce, not the caller's object: from_json normalises and repairs
    /// (AnimationClipAsset sorts its keys; AnimatorGraph clamps defaultState
    /// and drops transitions whose toState no longer exists). Caching the
    /// caller's copy would leave the asset behaving one way for the rest of
    /// the session and another way after the next invalidate() or restart.
    template <typename Asset>
    std::unique_ptr<Asset> reloaded_copy(const nlohmann::json &document, const Asset &fallback)
    {
      auto asset = std::make_unique<Asset>();
      if (!Asset::from_json(document, *asset, nullptr))
      {
        *asset = fallback;
      }
      return asset;
    }

    /// File stems of every .json in `directory`, sorted. A missing directory
    /// is not an error — it just means nothing has been authored yet — so
    /// every filesystem call here uses the error_code overload.
    std::vector<std::string> list_json_stems(const std::filesystem::path &directory)
    {
      std::vector<std::string> names;
      if (directory.empty())
      {
        return names;
      }

      std::error_code errorCode;
      if (!std::filesystem::is_directory(directory, errorCode) || errorCode)
      {
        return names;
      }

      std::filesystem::directory_iterator iterator(directory, errorCode);
      if (errorCode)
      {
        return names;
      }

      const std::filesystem::directory_iterator end;
      for (auto cursor = iterator; cursor != end; cursor.increment(errorCode))
      {
        if (errorCode)
        {
          break;
        }

        const std::filesystem::directory_entry &entry = *cursor;
        std::error_code statusCode;
        if (!entry.is_regular_file(statusCode) || statusCode)
        {
          continue;
        }

        if (entry.path().extension() != kAssetExtension)
        {
          continue;
        }

        names.push_back(entry.path().stem().string());
      }

      std::sort(names.begin(), names.end());
      return names;
    }
  }

  AnimationClipCache &AnimationClipCache::instance()
  {
    static AnimationClipCache cache;
    return cache;
  }

  void AnimationClipCache::setAssetRoot(const std::filesystem::path &root)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root_ = root;
    // Entries are keyed by resolved path, so a new root makes every one of
    // them stale — including the cached failures and the resolved-key memos,
    // which is the only way a memoised key can go stale at all.
    clips_.clear();
    graphs_.clear();
    clipKeys_.clear();
    graphKeys_.clear();
  }

  std::filesystem::path AnimationClipCache::assetRoot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_;
  }

  std::filesystem::path AnimationClipCache::clips_directory(const std::filesystem::path &assetRoot)
  {
    return assetRoot / ".hades" / "animations";
  }

  std::filesystem::path AnimationClipCache::graphs_directory(const std::filesystem::path &assetRoot)
  {
    return assetRoot / ".hades" / "animators";
  }

  bool AnimationClipCache::split_imported_reference(
      const std::string &reference, std::string &outModel, std::string &outClip)
  {
    const std::size_t separator = reference.find(kImportedClipSeparator);
    // Both halves have to be non-empty: "#Walk" names no model and "model#"
    // names no clip, and either would otherwise resolve to a path with a '#'
    // in it and fail much further away from the mistake.
    if (separator == std::string::npos || separator == 0 || separator + 1 >= reference.size())
    {
      return false;
    }

    outModel = reference.substr(0, separator);
    outClip = reference.substr(separator + 1);
    return true;
  }

  std::filesystem::path AnimationClipCache::resolveClipPath(const std::string &reference) const
  {
    std::string modelReference;
    std::string clipName;
    if (split_imported_reference(reference, modelReference, clipName))
    {
      // An imported clip has no file of its own; the model that carries it is
      // the honest answer to "where does this live".
      return ModelAssetCache::instance().resolvePath(modelReference);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return resolve_reference(root_, clips_directory(root_), reference);
  }

  std::filesystem::path AnimationClipCache::resolveGraphPath(const std::string &reference) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolve_reference(root_, graphs_directory(root_), reference);
  }

  const AnimationClipAsset *AnimationClipCache::imported_clip(
      const std::string &reference, const std::string &modelReference, const std::string &clipName)
  {
    // Resolved before this cache's lock is taken. Holding two asset-cache
    // mutexes at once is how a lock order gets established by accident, and
    // ModelAssetCache::get() does real work (an assimp import, on a miss).
    const ModelAsset *asset = ModelAssetCache::instance().get(modelReference);

    std::lock_guard<std::mutex> lock(mutex_);

    ClipEntry &entry = clips_[reference];
    const std::size_t nodeCount = asset != nullptr ? asset->nodes.size() : 0;
    if (entry.sourceAsset == asset && entry.sourceNodeCount == nodeCount &&
        (entry.asset != nullptr || !entry.error.empty()))
    {
      return entry.asset.get();
    }

    entry.asset.reset();
    entry.error.clear();
    entry.sourceAsset = asset;
    entry.sourceNodeCount = nodeCount;

    if (asset == nullptr)
    {
      entry.error = "could not load model '" + modelReference + "'";
      return nullptr;
    }

    // First name wins, the same rule the importer applies when it binds two
    // identically named nodes.
    int clipIndex = -1;
    for (std::size_t i = 0; i < asset->clips.size(); ++i)
    {
      if (asset->clips[i].name == clipName)
      {
        clipIndex = static_cast<int>(i);
        break;
      }
    }

    if (clipIndex < 0)
    {
      entry.error = "'" + modelReference + "' has no animation named '" + clipName + "'";
      return nullptr;
    }

    auto baked = std::make_unique<AnimationClipAsset>();
    if (!AnimationClipAsset::bake_from_model(*asset, clipIndex, *baked))
    {
      entry.error = "could not bake '" + clipName + "' from '" + modelReference + "'";
      return nullptr;
    }

    // bake_from_model leaves these to the caller: only this layer knows the
    // reference the clip was asked for by.
    baked->sourceModel = modelReference;
    baked->looping = true;

    entry.asset = std::move(baked);
    return entry.asset.get();
  }

  const AnimationClipAsset *AnimationClipCache::clip(const std::string &reference)
  {
    if (reference.empty())
    {
      return nullptr;
    }

    std::string modelReference;
    std::string clipName;
    if (split_imported_reference(reference, modelReference, clipName))
    {
      return imported_clip(reference, modelReference, clipName);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // resolve_reference is a free function, so the key is derived under the
    // one lock this call needs rather than through resolveClipPath(), which
    // would re-enter the same non-recursive mutex. Bound by reference: a copy
    // would put back the allocation the memo exists to remove, and
    // unordered_map values keep their addresses across the inserts below.
    const std::string &key = resolved_key(clipKeys_, root_, &AnimationClipCache::clips_directory, reference);

    auto it = clips_.find(key);
    if (it != clips_.end())
    {
      return it->second.asset.get();
    }

    ClipEntry entry;
    auto asset = std::make_unique<AnimationClipAsset>();
    if (load_json_asset(key, *asset, &entry.error))
    {
      Log::info_tagged(kLogTag, "loaded clip '%s' (%zu tracks, %.2fs)",
                       key.c_str(), asset->tracks.size(), static_cast<double>(asset->duration));
      entry.asset = std::move(asset);
    }
    else
    {
      // The failure is cached with the entry, so a broken file is reported
      // once instead of on every frame that asks for it.
      Log::warn_tagged(kLogTag, "failed to load clip '%s': %s", key.c_str(), entry.error.c_str());
    }

    auto [inserted, ok] = clips_.emplace(key, std::move(entry));
    (void)ok;
    return inserted->second.asset.get();
  }

  const AnimatorGraph *AnimationClipCache::graph(const std::string &reference)
  {
    if (reference.empty())
    {
      return nullptr;
    }

    const AnimatorGraph *result = nullptr;
    std::vector<std::string> warm;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const std::string &key = resolved_key(graphKeys_, root_, &AnimationClipCache::graphs_directory, reference);

      auto it = graphs_.find(key);
      if (it != graphs_.end())
      {
        return it->second.asset.get();
      }

      GraphEntry entry;
      auto asset = std::make_unique<AnimatorGraph>();
      if (load_json_asset(key, *asset, &entry.error))
      {
        Log::info_tagged(kLogTag, "loaded graph '%s' (%zu layers, %zu parameters)",
                         key.c_str(), asset->layers.size(), asset->parameters.size());
        warm = asset->referenced_clips();
        entry.asset = std::move(asset);
      }
      else
      {
        Log::warn_tagged(kLogTag, "failed to load graph '%s': %s", key.c_str(), entry.error.c_str());
      }

      auto [inserted, ok] = graphs_.emplace(key, std::move(entry));
      (void)ok;
      result = inserted->second.asset.get();
    }

    // Warm the clips the graph plays, so the first transition into a state
    // does not stall on file IO. Deliberately outside the lock: clip() takes
    // the same non-recursive mutex. The graph itself is held by a
    // unique_ptr, so `result` survives the clip inserts.
    for (const auto &clipReference : warm)
    {
      clip(clipReference);
    }

    return result;
  }

  std::string AnimationClipCache::errorFor(const std::string &reference) const
  {
    if (reference.empty())
    {
      return {};
    }

    // An imported bake is keyed by the reference itself: resolveClipPath
    // answers with the model file that carries it, which is a different
    // entry — and the one place a failed bake would never be found.
    std::string importedModel;
    std::string importedClip;
    if (split_imported_reference(reference, importedModel, importedClip))
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = clips_.find(reference);
      return it != clips_.end() ? it->second.error : std::string();
    }

    // A reference does not say which kind of asset it names, so both maps
    // are consulted; only one of the two keys can ever be populated.
    const std::string clipKey = resolveClipPath(reference).string();
    const std::string graphKey = resolveGraphPath(reference).string();

    std::lock_guard<std::mutex> lock(mutex_);
    auto clipIt = clips_.find(clipKey);
    if (clipIt != clips_.end() && !clipIt->second.error.empty())
    {
      return clipIt->second.error;
    }

    auto graphIt = graphs_.find(graphKey);
    if (graphIt != graphs_.end())
    {
      return graphIt->second.error;
    }

    return {};
  }

  bool AnimationClipCache::saveClip(const std::string &reference, const AnimationClipAsset &clip,
                                    std::string *errorMessage)
  {
    if (reference.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "no clip name given";
      }
      return false;
    }

    std::string importedModel;
    std::string importedClip;
    if (split_imported_reference(reference, importedModel, importedClip))
    {
      // An imported clip is a view onto the model file. Writing one would
      // have to either edit the model — which nothing in the engine does —
      // or silently write a JSON clip under a name that still reads back as
      // the model's own. Bake it to an authored clip and save that.
      if (errorMessage != nullptr)
      {
        *errorMessage = "'" + reference +
                        "' is animation inside a model file and cannot be written; bake it to an "
                        "editable clip first";
      }
      return false;
    }

    const std::filesystem::path file = resolveClipPath(reference);
    const nlohmann::json document = clip.to_json();
    if (!write_json_asset(file, document, errorMessage))
    {
      return false;
    }

    const std::string key = file.string();
    // Cache what was just written rather than re-reading the file, but run it
    // back through from_json so the entry matches what the next load would
    // yield. This also clears any failure cached from an earlier, broken
    // version of the file.
    auto asset = reloaded_copy(document, clip);

    std::lock_guard<std::mutex> lock(mutex_);
    ClipEntry entry;
    entry.asset = std::move(asset);
    clips_[key] = std::move(entry);

    Log::info_tagged(kLogTag, "saved clip '%s'", key.c_str());
    return true;
  }

  bool AnimationClipCache::saveGraph(const std::string &reference, const AnimatorGraph &graph,
                                     std::string *errorMessage)
  {
    if (reference.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "no animator name given";
      }
      return false;
    }

    const std::filesystem::path file = resolveGraphPath(reference);
    const nlohmann::json document = graph.to_json();
    if (!write_json_asset(file, document, errorMessage))
    {
      return false;
    }

    const std::string key = file.string();
    auto asset = reloaded_copy(document, graph);

    std::lock_guard<std::mutex> lock(mutex_);
    GraphEntry entry;
    entry.asset = std::move(asset);
    graphs_[key] = std::move(entry);

    Log::info_tagged(kLogTag, "saved graph '%s'", key.c_str());
    return true;
  }

  bool AnimationClipCache::deleteClip(const std::string &reference, std::string *errorMessage)
  {
    std::string importedModel;
    std::string importedClip;
    if (split_imported_reference(reference, importedModel, importedClip))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "'" + reference + "' lives inside a model file and cannot be deleted here";
      }
      return false;
    }

    if (reference.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "no clip name given";
      }
      return false;
    }

    const std::filesystem::path file = resolveClipPath(reference);

    // remove() returning false only means the file was already gone, which
    // is the outcome the caller asked for.
    std::error_code errorCode;
    std::filesystem::remove(file, errorCode);
    if (errorCode)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "could not delete " + file.string() + ": " + errorCode.message();
      }
      return false;
    }

    const std::string key = file.string();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      clips_.erase(key);
    }

    Log::info_tagged(kLogTag, "deleted clip '%s'", key.c_str());
    return true;
  }

  bool AnimationClipCache::deleteGraph(const std::string &reference, std::string *errorMessage)
  {
    if (reference.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "no animator name given";
      }
      return false;
    }

    const std::filesystem::path file = resolveGraphPath(reference);

    std::error_code errorCode;
    std::filesystem::remove(file, errorCode);
    if (errorCode)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "could not delete " + file.string() + ": " + errorCode.message();
      }
      return false;
    }

    const std::string key = file.string();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      graphs_.erase(key);
    }

    Log::info_tagged(kLogTag, "deleted graph '%s'", key.c_str());
    return true;
  }

  std::vector<std::string> AnimationClipCache::listClips() const
  {
    std::filesystem::path directory;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      directory = clips_directory(root_);
    }

    return list_json_stems(directory);
  }

  std::vector<std::string> AnimationClipCache::listImportedClips(const std::string &modelReference)
  {
    std::vector<std::string> references;
    if (modelReference.empty())
    {
      return references;
    }

    const ModelAsset *asset = ModelAssetCache::instance().get(modelReference);
    if (asset == nullptr)
    {
      return references;
    }

    references.reserve(asset->clips.size());
    for (const auto &clip : asset->clips)
    {
      // An unnamed clip cannot be addressed by reference at all, so listing
      // one would offer a pick that resolves to nothing.
      if (clip.name.empty())
      {
        continue;
      }
      references.push_back(modelReference + kImportedClipSeparator + clip.name);
    }

    return references;
  }

  std::vector<std::string> AnimationClipCache::listGraphs() const
  {
    std::filesystem::path directory;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      directory = graphs_directory(root_);
    }

    return list_json_stems(directory);
  }

  void AnimationClipCache::stageGraph(const std::string &reference, const AnimatorGraph &graph)
  {
    if (reference.empty())
    {
      return;
    }

    const std::string key = resolveGraphPath(reference).string();

    std::lock_guard<std::mutex> lock(mutex_);
    GraphEntry &entry = graphs_[key];
    entry.asset = std::make_unique<AnimatorGraph>(graph);
    // A staged graph is by definition loadable, so any error recorded from an
    // earlier read of the same key has to go with it — otherwise errorFor()
    // keeps reporting a parse failure for a graph that is now in hand.
    entry.error.clear();
  }

  void AnimationClipCache::invalidate(const std::string &reference)
  {
    if (reference.empty())
    {
      return;
    }

    const std::string clipKey = resolveClipPath(reference).string();
    const std::string graphKey = resolveGraphPath(reference).string();

    std::lock_guard<std::mutex> lock(mutex_);
    clips_.erase(clipKey);
    graphs_.erase(graphKey);
    // An imported bake is keyed by the reference itself, not by a path: it
    // has no file of its own for resolveClipPath to have produced.
    clips_.erase(reference);
  }

  void AnimationClipCache::clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clips_.clear();
    graphs_.clear();
  }
}
