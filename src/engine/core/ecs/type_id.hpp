#ifndef HADES_ENGINE_CORE_ECS_TYPE_ID_HPP
#define HADES_ENGINE_CORE_ECS_TYPE_ID_HPP

#include <cassert>
#include <cstdint>

#include "constants.h"

namespace hades
{
  using ComponentId = uint32_t;

  class ComponentTypeId
  {
  private:
    static ComponentId nextId;

  public:
    template <typename T>
    static ComponentId get()
    {
      static ComponentId id = assignId();
      return id;
    }

  private:
    static ComponentId assignId()
    {
      ComponentId id = nextId++;
      assert(id < MAX_COMPONENTS && "Component type count exceeds MAX_COMPONENTS");
      return id;
    }
  };
}

#endif
