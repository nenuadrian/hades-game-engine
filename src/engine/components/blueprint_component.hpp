#ifndef HADES_ENGINE_COMPONENTS_BLUEPRINT_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_BLUEPRINT_COMPONENT_HPP

#include <map>
#include <string>
#include <vector>

namespace hades
{
  /// One Blueprint asset attached to an entity.
  ///
  /// Mirrors `ScriptAttachment`: a workspace-relative asset path, an enable
  /// flag, and a string-keyed override map so the inspector can retune an
  /// exposed variable per entity without touching the shared asset.
  struct BlueprintAttachment
  {
    std::string assetPath;
    bool enabled = true;
    std::map<std::string, std::string> variableOverrides;
  };

  struct BlueprintComponent
  {
    std::vector<BlueprintAttachment> attachments;
  };
}

#endif
