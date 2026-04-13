#ifndef HADES_ENGINE_RUNTIME_HADES_SCRIPT_REGISTRATION_HPP
#define HADES_ENGINE_RUNTIME_HADES_SCRIPT_REGISTRATION_HPP

#include "hades_script.hpp"

namespace hades
{
  struct ScriptFactoryEntry
  {
    const char *className;
    HadesScript *(*createFn)();
  };
}

#define HADES_REGISTER_SCRIPT(ClassName)                                    \
  extern "C" hades::HadesScript *hades_create_##ClassName()                \
  {                                                                        \
    return new ClassName();                                                 \
  }

#endif
