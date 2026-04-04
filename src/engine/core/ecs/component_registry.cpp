#include "component_registry.hpp"

namespace hades
{
  ComponentRegistry &ComponentRegistry::instance()
  {
    static ComponentRegistry registry;
    // Ensure built-in component serializers are registered on first access.
    register_builtin_components();
    return registry;
  }
}
