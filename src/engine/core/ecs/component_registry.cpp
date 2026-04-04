#include "component_registry.hpp"

namespace hades
{
  ComponentRegistry &ComponentRegistry::instance()
  {
    static ComponentRegistry registry;
    return registry;
  }
}
