#include "editor.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#include "imgui.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_2d.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char IMPORT_MODEL_POPUP_TITLE[] = "Import Model";

    std::string entity_display_label(Entity::EntityId entity, ComponentManager &componentManager)
    {
      std::string name = "Entity";
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        name = componentManager.getComponent<NameComponent>(entity).value;
      }

      if (componentManager.hasComponent<CameraComponent>(entity) &&
          componentManager.getComponent<CameraComponent>(entity).isMainCamera)
      {
        name += " [Main]";
      }

      if (componentManager.hasComponent<WorldComponent>(entity))
      {
        name += componentManager.getComponent<WorldComponent>(entity).isDefault
                    ? " [World, Default]"
                    : " [World]";
      }

      return name + " (" + std::to_string(entity) + ")";
    }

    template <typename T>
    void remove_component_if_present(ComponentManager &componentManager, Entity::EntityId entity)
    {
      if (componentManager.hasComponent<T>(entity))
      {
        componentManager.removeComponent<T>(entity);
      }
    }

    bool hierarchy_contains_entity(
        Entity::EntityId root,
        Entity::EntityId target,
        ComponentManager &componentManager)
    {
      if (root == target)
      {
        return true;
      }

      if (!componentManager.hasComponent<TransformHierarchyComponent>(root))
      {
        return false;
      }

      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(root);
      for (const Entity::EntityId child : hierarchy.children)
      {
        if (hierarchy_contains_entity(child, target, componentManager))
        {
          return true;
        }
      }

      return false;
    }

    void destroy_entity_subtree(
        Entity::EntityId entity,
        EntityManager &entityManager,
        ComponentManager &componentManager)
    {
      if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        return;
      }

      const auto hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      for (const Entity::EntityId child : hierarchy.children)
      {
        destroy_entity_subtree(child, entityManager, componentManager);
      }

      if (hierarchy.parent.has_value() &&
          componentManager.hasComponent<TransformHierarchyComponent>(*hierarchy.parent))
      {
        auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(*hierarchy.parent);
        parentHierarchy.removeChild(entity);
      }

      remove_component_if_present<NameComponent>(componentManager, entity);
      remove_component_if_present<WorldComponent>(componentManager, entity);
      remove_component_if_present<TransformHierarchyComponent>(componentManager, entity);
      remove_component_if_present<PositionComponent2D>(componentManager, entity);
      remove_component_if_present<PositionComponent3D>(componentManager, entity);
      remove_component_if_present<CameraComponent>(componentManager, entity);
      remove_component_if_present<AudioListenerComponent>(componentManager, entity);
      remove_component_if_present<PrimitiveComponent>(componentManager, entity);
      remove_component_if_present<TextComponent>(componentManager, entity);
      remove_component_if_present<AudioSourceComponent>(componentManager, entity);
      remove_component_if_present<ModelComponent>(componentManager, entity);
      remove_component_if_present<RenderComponent>(componentManager, entity);
      remove_component_if_present<ScriptComponent>(componentManager, entity);

      entityManager.destroyEntity(entity);
    }
  }

  void Editor::ensure_world_state(EntityManager &entityManager, ComponentManager &componentManager)
  {
    const auto defaultWorld = normalize_default_world(entityManager, componentManager);
    if (!defaultWorld.has_value())
    {
      const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
      load_world(world, componentManager);
      state.playModeMessage.clear();
      return;
    }

    if (!state.loadedWorld.has_value() || !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      load_world(*defaultWorld, componentManager);
      return;
    }

    if (state.selectedEntity.has_value())
    {
      const auto selectedWorld = world_for_entity(*state.selectedEntity, componentManager);
      if (!selectedWorld.has_value() || *selectedWorld != *state.loadedWorld)
      {
        state.selectedEntity = *state.loadedWorld;
      }
    }
  }

  Entity::EntityId Editor::create_world(EntityManager &entityManager, ComponentManager &componentManager)
  {
    const std::size_t worldIndex = find_world_entities(entityManager, componentManager).size() + 1U;
    const bool shouldBeDefault = !find_default_world(entityManager, componentManager).has_value();
    const auto world = EntityFactory::createWorld(
        entityManager,
        componentManager,
        "World" + std::to_string(worldIndex),
        shouldBeDefault);
    if (shouldBeDefault)
    {
      set_default_world(world, entityManager, componentManager);
    }

    load_world(world, componentManager);
    state.playModeMessage.clear();
    return world;
  }

  void Editor::load_world(Entity::EntityId world, ComponentManager &componentManager)
  {
    (void)componentManager;
    state.loadedWorld = world;
    state.selectedEntity = world;
  }

  void Editor::set_default_world(
      Entity::EntityId world,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    hades::set_default_world(entityManager, componentManager, world);
    state.playModeMessage.clear();
  }

  void Editor::request_entity_creation(EditorEntityPreset preset, Entity::EntityId parent)
  {
    state.selectedEntity = parent;
    state.pendingEntityPreset = preset;
  }

  void Editor::request_model_import(Entity::EntityId parent)
  {
    state.selectedEntity = parent;
    if (importModelPathBuffer[0] == '\0')
    {
      std::snprintf(
          importModelPathBuffer.data(),
          importModelPathBuffer.size(),
          "%s",
          "src/tests/backpack/12305_backpack_v2_l3.obj");
    }

    importModelError.clear();
    openImportModelDialog = true;
  }

  void Editor::request_entity_deletion(Entity::EntityId entity)
  {
    pendingEntityDeletion_ = entity;
  }

  void Editor::handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (state.pendingEntityPreset == EditorEntityPreset::None)
    {
      return;
    }

    const auto parent = get_selected_parent(entityManager, componentManager);
    Entity::EntityId createdEntity = Entity::INVALID;

    switch (state.pendingEntityPreset)
    {
    case EditorEntityPreset::Camera:
      createdEntity = EntityFactory::createCamera(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::Cube:
      createdEntity = EntityFactory::createCube(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::Text:
      createdEntity = EntityFactory::createText(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::AudioEmitter:
      createdEntity = EntityFactory::createAudioEmitter(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::None:
      break;
    }

    if (createdEntity != Entity::INVALID)
    {
      state.selectedEntity = createdEntity;
      state.playModeMessage.clear();
    }

    state.pendingEntityPreset = EditorEntityPreset::None;
  }

  void Editor::handle_entity_deletion_requests(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    if (!pendingEntityDeletion_.has_value())
    {
      return;
    }

    const Entity::EntityId entity = *pendingEntityDeletion_;
    pendingEntityDeletion_.reset();

    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    if (state.isPlaying)
    {
      stop_play_mode(scriptRuntime);
      state.playModeMessage = "Play mode stopped because an entity hierarchy was deleted.";
      log_warning(state.playModeMessage);
    }

    const auto hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    const std::optional<Entity::EntityId> fallbackParent =
        hierarchy.parent.has_value() &&
                componentManager.hasComponent<TransformHierarchyComponent>(*hierarchy.parent)
            ? hierarchy.parent
            : std::nullopt;
    const bool deletedSelectedEntity =
        state.selectedEntity.has_value() &&
        hierarchy_contains_entity(entity, *state.selectedEntity, componentManager);
    const bool deletedLoadedWorld =
        state.loadedWorld.has_value() &&
        hierarchy_contains_entity(entity, *state.loadedWorld, componentManager);

    destroy_entity_subtree(entity, entityManager, componentManager);

    const auto defaultWorld = normalize_default_world(entityManager, componentManager);
    if (deletedLoadedWorld || !state.loadedWorld.has_value() ||
        !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      if (defaultWorld.has_value())
      {
        load_world(*defaultWorld, componentManager);
      }
      else
      {
        state.loadedWorld.reset();
        state.selectedEntity.reset();
      }
    }
    else if (deletedSelectedEntity)
    {
      if (fallbackParent.has_value())
      {
        state.selectedEntity = fallbackParent;
      }
      else
      {
        state.selectedEntity = state.loadedWorld;
      }
    }
  }

  void Editor::import_model(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (openImportModelDialog)
    {
      ImGui::OpenPopup(IMPORT_MODEL_POPUP_TITLE);
      openImportModelDialog = false;
    }

    if (!ImGui::BeginPopupModal(IMPORT_MODEL_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    ImGui::TextWrapped("Import Model");
    ImGui::InputText("Path", importModelPathBuffer.data(), importModelPathBuffer.size());

    if (!importModelError.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", importModelError.c_str());
    }

    if (ImGui::Button("Import"))
    {
      std::string errorMessage;
      const auto parent = get_selected_parent(entityManager, componentManager);
      const auto createdEntity = EntityFactory::createImportedModel(
          entityManager,
          componentManager,
          std::filesystem::path(importModelPathBuffer.data()),
          parent,
          &errorMessage);

      if (createdEntity.has_value())
      {
        state.selectedEntity = *createdEntity;
        importModelError.clear();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        importModelError = errorMessage.empty() ? "Model import failed." : errorMessage;
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      importModelError.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::handle_play_mode_requests(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    switch (state.pendingPlayAction)
    {
    case EditorPlayAction::None:
      return;
    case EditorPlayAction::Start:
      openDebugConsoleWindow_ = true;
      focusDebugConsoleWindow_ = true;
      start_play_mode(entityManager, componentManager, scriptRuntime);
      break;
    case EditorPlayAction::Stop:
      stop_play_mode(scriptRuntime);
      break;
    }

    state.pendingPlayAction = EditorPlayAction::None;
  }

  void Editor::start_play_mode(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    if (!activeWorkspacePath_.empty() && !workspaceScriptFiles_.empty())
    {
      queue_workspace_script_compile();
    }

    const auto startupWorld = normalize_default_world(entityManager, componentManager);
    if (!startupWorld.has_value())
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      state.playModeMessage = "No world available for play mode.";
      log_error("Play mode failed: " + state.playModeMessage);
      return;
    }

    const auto selection = select_main_camera(entityManager, componentManager, startupWorld);
    if (selection.status != MainCameraSelectionStatus::Ready || !selection.entity.has_value())
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      state.playModeMessage = main_camera_selection_message(selection.status);
      log_error("Play mode failed: " + state.playModeMessage);
      return;
    }

    std::string scriptError;
    if (!scriptRuntime.start(componentManager, entityManager, activeWorkspacePath_, startupWorld, &scriptError))
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      state.playModeMessage = scriptError;
      log_error("Play mode failed: " + scriptError);
      return;
    }

    state.isPlaying = true;
    state.activeWorld = startupWorld;
    state.activeCamera = selection.entity;
    state.playModeMessage.clear();
  }

  void Editor::stop_play_mode(ScriptRuntime &scriptRuntime)
  {
    scriptRuntime.stop();
    state.isPlaying = false;
    state.activeWorld.reset();
    state.activeCamera.reset();
    state.playModeMessage.clear();
  }

  void Editor::set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager)
  {
    const auto targetWorld = world_for_entity(entity, componentManager);
    for (Entity::EntityId current : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<CameraComponent>(current))
      {
        continue;
      }

      if (targetWorld.has_value() && world_for_entity(current, componentManager) != targetWorld)
      {
        continue;
      }

      auto &camera = componentManager.getComponent<CameraComponent>(current);
      camera.isMainCamera = (current == entity);
    }
  }

  std::optional<Entity::EntityId> Editor::get_selected_parent(
      EntityManager &entityManager,
      ComponentManager &componentManager) const
  {
    if (state.selectedEntity.has_value() &&
        componentManager.hasComponent<TransformHierarchyComponent>(*state.selectedEntity))
    {
      const auto selectedWorld = world_for_entity(*state.selectedEntity, componentManager);
      if (selectedWorld.has_value() &&
          state.loadedWorld.has_value() &&
          *selectedWorld == *state.loadedWorld)
      {
        return state.selectedEntity;
      }
    }

    if (state.loadedWorld.has_value())
    {
      return state.loadedWorld;
    }

    return find_default_world(entityManager, componentManager);
  }

  std::string Editor::entity_label(Entity::EntityId entity, ComponentManager &componentManager) const
  {
    return entity_display_label(entity, componentManager);
  }

  void Editor::entities(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(ENTITY_WINDOW_TITLE);
    render_hierarchies(entityManager, componentManager);
    ImGui::End();
  }

  void Editor::render_hierarchy(
      Entity::EntityId entity,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (state.selectedEntity.has_value() && *state.selectedEntity == entity)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (hierarchy.children.empty())
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const std::string label = entity_label(entity, componentManager);
    ImGui::PushID(static_cast<int>(entity));
    if (componentManager.hasComponent<WorldComponent>(entity))
    {
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    const bool open = ImGui::TreeNodeEx("entity", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked())
    {
      state.selectedEntity = entity;
    }

    if (ImGui::BeginPopupContextItem())
    {
      if (ImGui::BeginMenu("Add Child"))
      {
        if (ImGui::MenuItem("Camera"))
        {
          request_entity_creation(EditorEntityPreset::Camera, entity);
        }
        if (ImGui::MenuItem("Cube"))
        {
          request_entity_creation(EditorEntityPreset::Cube, entity);
        }
        if (ImGui::MenuItem("Text"))
        {
          request_entity_creation(EditorEntityPreset::Text, entity);
        }
        if (ImGui::MenuItem("Audio Emitter"))
        {
          request_entity_creation(EditorEntityPreset::AudioEmitter, entity);
        }
        if (ImGui::MenuItem("Import Model..."))
        {
          request_model_import(entity);
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Delete Entity and Children"))
      {
        request_entity_deletion(entity);
      }

      ImGui::EndPopup();
    }

    if (open && !hierarchy.children.empty())
    {
      for (const auto &child : hierarchy.children)
      {
        render_hierarchy(child, entityManager, componentManager);
      }
      ImGui::TreePop();
    }

    ImGui::PopID();
  }

  void Editor::render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;

    if (!state.loadedWorld.has_value() || !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      ImGui::TextDisabled("No world.");
      return;
    }

    render_hierarchy(*state.loadedWorld, entityManager, componentManager);
  }
}
