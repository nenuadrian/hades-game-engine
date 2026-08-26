#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_ASSET_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_ASSET_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "blueprint_compiler.hpp"
#include "blueprint_graph.hpp"

namespace hades
{
  /// File extension for Blueprint assets.
  inline constexpr const char *kBlueprintFileExtension = ".hbp";

  /// Directory new Blueprints are created in by default: `<workspace>/Blueprints`.
  std::filesystem::path blueprint_assets_directory(const std::filesystem::path &workspaceRoot);

  bool load_blueprint(
      const std::filesystem::path &file,
      Blueprint &out,
      std::string *errorMessage = nullptr);

  bool save_blueprint(
      const std::filesystem::path &file,
      const Blueprint &blueprint,
      std::string *errorMessage = nullptr);

  /// Every `.hbp` under the workspace, as forward-slash relative paths, sorted.
  std::vector<std::string> list_blueprint_assets(const std::filesystem::path &workspaceRoot);

  /// A fresh graph with an `Event BeginPlay -> Print String` chain already
  /// wired, so a new asset does something the moment it is created.
  Blueprint make_starter_blueprint(const std::string &name);

  /// Loads and compiles assets on demand, keyed by workspace-relative path.
  ///
  /// The runtime shares one cache across every entity so a Blueprint used by
  /// a hundred entities is parsed and compiled once. Compiled results are
  /// stable in memory (held by unique_ptr) because live instances point at
  /// them.
  class BlueprintAssetCache
  {
  public:
    /// Returns the compiled asset, or nullptr when it could not be read.
    /// A compiled-but-invalid graph is still returned: inspect
    /// `CompiledBlueprint::succeeded`.
    const CompiledBlueprint *acquire(
        const std::filesystem::path &workspaceRoot,
        const std::string &relativePath,
        std::string *errorMessage = nullptr);

    /// Compile an in-memory Blueprint under a synthetic key. Used by the
    /// editor to preview an unsaved graph and by tests.
    const CompiledBlueprint *acquire_from_memory(
        const std::string &key,
        const Blueprint &blueprint);

    void clear();
    std::size_t size() const { return entries_.size(); }

  private:
    std::unordered_map<std::string, std::unique_ptr<CompiledBlueprint>> entries_;
  };
}

#endif
