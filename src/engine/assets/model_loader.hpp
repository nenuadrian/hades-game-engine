#ifndef HADES_ENGINE_ASSETS_MODEL_LOADER_HPP
#define HADES_ENGINE_ASSETS_MODEL_LOADER_HPP

#include <filesystem>
#include <string>

#include "model_asset.hpp"

namespace hades
{
  /// Import a model file (FBX, OBJ, glTF/GLB, COLLADA) into `out` via assimp.
  /// Returns false and fills `error` (if non-null) on failure.
  bool load_model_asset(const std::filesystem::path &file, ModelAsset &out, std::string *error);
}

#endif
