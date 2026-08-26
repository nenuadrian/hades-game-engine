#include "blueprint_asset.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    std::string to_forward_slashes(std::filesystem::path path)
    {
      std::string text = path.generic_string();
      return text;
    }
  }

  std::filesystem::path blueprint_assets_directory(const std::filesystem::path &workspaceRoot)
  {
    return workspaceRoot / "Blueprints";
  }

  bool load_blueprint(
      const std::filesystem::path &file,
      Blueprint &out,
      std::string *errorMessage)
  {
    std::ifstream stream(file);
    if (!stream.is_open())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "could not open " + file.string();
      }
      return false;
    }

    nlohmann::json document;
    try
    {
      stream >> document;
    }
    catch (const std::exception &exception)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::string("invalid Blueprint JSON in ") + file.string() + ": " + exception.what();
      }
      return false;
    }

    if (!Blueprint::from_json(document, out, errorMessage))
    {
      return false;
    }

    if (out.name.empty())
    {
      out.name = file.stem().string();
    }

    return true;
  }

  bool save_blueprint(
      const std::filesystem::path &file,
      const Blueprint &blueprint,
      std::string *errorMessage)
  {
    std::error_code errorCode;
    if (file.has_parent_path())
    {
      std::filesystem::create_directories(file.parent_path(), errorCode);
      if (errorCode)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "could not create " + file.parent_path().string() + ": " + errorCode.message();
        }
        return false;
      }
    }

    std::ofstream stream(file, std::ios::trunc);
    if (!stream.is_open())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "could not write " + file.string();
      }
      return false;
    }

    stream << blueprint.to_json().dump(2) << '\n';
    if (!stream.good())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "failed while writing " + file.string();
      }
      return false;
    }

    return true;
  }

  std::vector<std::string> list_blueprint_assets(const std::filesystem::path &workspaceRoot)
  {
    std::vector<std::string> assets;

    std::error_code errorCode;
    if (workspaceRoot.empty() || !std::filesystem::is_directory(workspaceRoot, errorCode))
    {
      return assets;
    }

    auto iterator = std::filesystem::recursive_directory_iterator(
        workspaceRoot,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode);
    if (errorCode)
    {
      return assets;
    }

    for (auto it = iterator; it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode))
    {
      if (errorCode)
      {
        break;
      }

      const auto &entry = *it;
      // `.hades` holds editor state and saved worlds, never source assets.
      if (entry.is_directory(errorCode) && entry.path().filename() == ".hades")
      {
        it.disable_recursion_pending();
        continue;
      }

      if (!entry.is_regular_file(errorCode))
      {
        continue;
      }

      if (entry.path().extension() != kBlueprintFileExtension)
      {
        continue;
      }

      assets.push_back(to_forward_slashes(
          std::filesystem::relative(entry.path(), workspaceRoot, errorCode)));
    }

    std::sort(assets.begin(), assets.end());
    return assets;
  }

  Blueprint make_starter_blueprint(const std::string &name)
  {
    Blueprint blueprint;
    blueprint.name = name;

    BlueprintNode beginPlay;
    beginPlay.id = blueprint.allocate_node_id();
    beginPlay.type = "event.begin_play";
    beginPlay.x = -220.0f;
    beginPlay.y = 0.0f;

    BlueprintNode print;
    print.id = blueprint.allocate_node_id();
    print.type = "debug.print";
    print.x = 60.0f;
    print.y = 0.0f;
    print.pinDefaults["text"] = BlueprintValue::from_string(name + " started");

    BlueprintLink link;
    link.kind = BlueprintLinkKind::Exec;
    link.from = {beginPlay.id, "exec"};
    link.to = {print.id, "exec"};

    blueprint.eventGraph.nodes.push_back(std::move(beginPlay));
    blueprint.eventGraph.nodes.push_back(std::move(print));
    blueprint.eventGraph.links.push_back(link);

    return blueprint;
  }

  const CompiledBlueprint *BlueprintAssetCache::acquire(
      const std::filesystem::path &workspaceRoot,
      const std::string &relativePath,
      std::string *errorMessage)
  {
    if (relativePath.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "no Blueprint asset selected";
      }
      return nullptr;
    }

    const auto cached = entries_.find(relativePath);
    if (cached != entries_.end())
    {
      return cached->second.get();
    }

    Blueprint blueprint;
    std::string loadError;
    if (!load_blueprint(workspaceRoot / relativePath, blueprint, &loadError))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = loadError;
      }
      return nullptr;
    }

    auto compiled = std::make_unique<CompiledBlueprint>(compile_blueprint(blueprint));
    const CompiledBlueprint *raw = compiled.get();
    entries_.emplace(relativePath, std::move(compiled));
    return raw;
  }

  const CompiledBlueprint *BlueprintAssetCache::acquire_from_memory(
      const std::string &key,
      const Blueprint &blueprint)
  {
    auto compiled = std::make_unique<CompiledBlueprint>(compile_blueprint(blueprint));
    const CompiledBlueprint *raw = compiled.get();
    entries_[key] = std::move(compiled);
    return raw;
  }

  void BlueprintAssetCache::clear()
  {
    entries_.clear();
  }
}
