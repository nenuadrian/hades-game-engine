#ifndef HADES_ENGINE_COMPONENTS_MODEL_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_MODEL_COMPONENT_HPP

#include "../assets/asset_handle.hpp"
#include "../assets/imported_model.hpp"

namespace hades
{
  struct ModelComponent
  {
    AssetHandle<ImportedModel> modelAsset;
  };
}

#endif
