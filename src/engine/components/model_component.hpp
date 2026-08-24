#ifndef HADES_ENGINE_COMPONENTS_MODEL_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_MODEL_COMPONENT_HPP

#include <string>

namespace hades
{
  /// Renders an imported model asset (FBX, OBJ, glTF/GLB, COLLADA).
  /// The path resolves relative to the workspace (editor) or project
  /// directory (runtime). Pair with an AnimationComponent to play the
  /// asset's animation clips, and with a MeshRendererComponent to override
  /// the imported materials.
  struct ModelComponent
  {
    std::string assetPath;
  };
}

#endif
