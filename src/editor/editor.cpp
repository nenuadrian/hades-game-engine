#include "editor.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "tiny_obj_loader.h"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/gui/imgui.hpp"

namespace hades
{
  Editor::Editor() : gui(std::make_unique<ImGui_GUI>())
  {
    MenuBarItem file;
    file.title = "File";

    MenuBarItem exit;
    exit.title = "Exit";
    exit.on_activate = [this]()
    {
      state.events.push(EDITOR_QUIT);
    };

    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);
  }

  Editor::~Editor() = default;

  void Editor::render(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (entityManager.getAllEntities().empty())
    {
      const auto id = entityManager.createEntity();
      componentManager.addComponent(id, TransformHierarchyComponent());

      tinyobj::attrib_t attrib;
      std::vector<tinyobj::shape_t> shapes;
      std::vector<tinyobj::material_t> materials;
      std::string warn;
      std::string err;
      const bool loaded = tinyobj::LoadObj(
          &attrib,
          &shapes,
          &materials,
          &warn,
          &err,
          "../src/tests/backpack/12305_backpack_v2_l3.obj",
          "../src/tests/backpack/",
          true,
          true);
      (void)loaded;

      if (!warn.empty())
      {
        std::cout << "WARN: " << warn << std::endl;
      }
      if (!err.empty())
      {
        std::cerr << "ERR: " << err << std::endl;
      }

      std::vector<float> vertices;
      std::vector<uint16_t> indices;
      for (const auto &shape : shapes)
      {
        for (const auto &index : shape.mesh.indices)
        {
          vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
          vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
          vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
          if (!attrib.texcoords.empty())
          {
            vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
            vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
          }
          else
          {
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
          }
          indices.push_back(static_cast<uint16_t>(indices.size()));
        }
      }
    }

    gui->render_frame();
    entities(entityManager, componentManager);
    debug(deltaTime);
  }

  void Editor::entities(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin("Entities");
    render_hierarchies(entityManager, componentManager);
    ImGui::End();
  }

  void Editor::render_hierarchy(Entity::EntityId entity, ComponentManager &componentManager, int depth)
  {
    (void)depth;
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    ImGui::Text("%u", static_cast<unsigned>(entity));

    for (const auto &child : hierarchy.children)
    {
      render_hierarchy(child, componentManager, depth + 1);
    }
  }

  void Editor::render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager)
  {
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        continue;
      }

      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      if (!hierarchy.hasParent())
      {
        render_hierarchy(entity, componentManager, 0);
      }
    }
  }

  void Editor::debug(float deltaTime)
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %f", 1 / deltaTime);
    ImGui::End();
  }
}
