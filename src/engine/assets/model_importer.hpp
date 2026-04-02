#ifndef HADES_ENGINE_ASSETS_MODEL_IMPORTER_HPP
#define HADES_ENGINE_ASSETS_MODEL_IMPORTER_HPP

#include <filesystem>
#include <optional>
#include <string>

#include "imported_model.hpp"

namespace hades
{
  class ModelImporter
  {
  public:
    static std::optional<ImportedModel> importFromFile(
        const std::filesystem::path &sourcePath,
        std::string *errorMessage = nullptr);
  };
}

#endif
