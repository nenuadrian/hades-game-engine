#ifndef HADES_EDITOR_EDITOR_HPP
#define HADES_EDITOR_EDITOR_HPP

#include <memory>

#include "types.h"
#include "../engine/core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class GUI;

  class Editor
  {
  public:
    EditorState state;
    std::unique_ptr<GUI> gui;

    Editor();
    ~Editor();

    void render(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager);

  private:
    void entities(EntityManager &entityManager, ComponentManager &componentManager);
    void render_hierarchy(Entity::EntityId entity, ComponentManager &componentManager, int depth = 0);
    void render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager);
    void debug(float deltaTime);
  };

}

#endif
