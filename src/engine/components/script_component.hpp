#ifndef HADES_ENGINE_COMPONENTS_SCRIPT_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_SCRIPT_COMPONENT_HPP

#include <string>
#include <vector>

namespace hades
{
  struct ScriptAttachment
  {
    std::string scriptPath;
    std::string className;
    bool enabled = true;
  };

  struct ScriptComponent
  {
    std::vector<ScriptAttachment> attachments;
  };
}

#endif
