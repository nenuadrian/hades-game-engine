#ifndef HADES_ENGINE_COMPONENTS_SCRIPT_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_SCRIPT_COMPONENT_HPP

#include <map>
#include <string>
#include <vector>

namespace hades
{
  struct ScriptAttachment
  {
    std::string scriptPath;
    std::string className;
    bool enabled = true;
    std::map<std::string, std::string> publicFieldValues;

    // Relative path (from the workspace root) to a trained `.pt` policy that
    // drives this attachment at play time, e.g. `.hades/policies/pole_v1/policy.pt`.
    // Empty = legacy `onUpdate` behaviour (or training-owned when the script
    // is the training subject).
    std::string modelPath;
  };

  struct ScriptComponent
  {
    std::vector<ScriptAttachment> attachments;
  };
}

#endif
