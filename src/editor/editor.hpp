#include "imgui.h"

#include <iostream>
#include <memory>

#include "types.h"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/render_component.hpp"
#include "tiny_obj_loader.h"
#include "../engine/gui/imgui.hpp"
#include "../engine/gui/gui.hpp"

class Editor
{

public:
  EditorState state;
  std::unique_ptr<GUI> gui = std::make_unique<ImGui_GUI>();

  Editor()
  {
    auto file = MenuBarItem{.title = "File"};
    auto exit = MenuBarItem{.title = "Exit"};
    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);
  }

  void render(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager)
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
      bool ret = tinyobj::LoadObj(
          &attrib,
          &shapes,
          &materials,
          &warn, // Add this line
          &err,
          "/Users/adriannenu/Desktop/projects/hades-game-engine/src/tests/backpack/12305_backpack_v2_l3.obj",
          "/Users/adriannenu/Desktop/projects/hades-game-engine/src/tests/backpack/", // Set material directory to NULL if not used
          true,                                                                       // triangulate the mesh
          true                                                                        // default vertex color fallback
      );

      if (!warn.empty())
      {
        std::cout << "WARN: " << warn << std::endl;
      }
      if (!err.empty())
      {
        std::cerr << "ERR: " << err << std::endl;
      }

      // Load vertices and indices from the .obj file
      std::vector<float> vertices;
      std::vector<uint16_t> indices;
      for (const auto &shape : shapes)
      {
        for (const auto &index : shape.mesh.indices)
        {
          // Fill the vertex data (position, texture coords, etc.)
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
            vertices.push_back(0.0f); // No texcoord, default to 0
            vertices.push_back(0.0f);
          }
          indices.push_back(static_cast<uint16_t>(indices.size()));
        }
      }
      /*
            if (!vertices.empty() && !indices.empty())
            {
              // Create vertex buffer
              bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
                  bgfx::makeRef(vertices.data(), sizeof(float) * vertices.size()), vertexLayout);

              // Create index buffer
              bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
                  bgfx::makeRef(indices.data(), sizeof(uint16_t) * indices.size()));

              componentManager.addComponent(id, RenderComponent());
              componentManager.getComponent<RenderComponent>(id).vbh = vbh;
              componentManager.getComponent<RenderComponent>(id).ibh = ibh;
            }*/
    }

    gui.get()->render_frame();
    entities(entityManager, componentManager);
    debug(deltaTime);
  }

private:
  void entities(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin("Entities");
    render_hierarchies(entityManager, componentManager);
    ImGui::End();
  }

  void render_hierarchy(Entity::EntityId entity, ComponentManager &componentManager, int depth = 0)
  {
    // Get the hierarchy component of the entity
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return; // No hierarchy component, so skip this entity
    }

    const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);

    // Print the entity, indented based on its depth in the hierarchy
    // std::cout << std::string(depth * 2, ' ') << "Entity " << entity << std::endl;
    ImGui::Text("%d", entity);

    // Recursively print each child entity
    for (const auto &child : hierarchy.children)
    {
      render_hierarchy(child, componentManager, depth + 1);
    }
  }

  void render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager)
  {
    // Assuming you have a way to iterate over all entities in the scene
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        continue;
      }

      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      // Only print entities that are "roots" (i.e., have no parent)
      if (!hierarchy.hasParent())
      {
        render_hierarchy(entity, componentManager, 0);
      }
    }
  }

  void debug(float deltaTime)
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %f", 1 / deltaTime);
    ImGui::End();
  }
};
