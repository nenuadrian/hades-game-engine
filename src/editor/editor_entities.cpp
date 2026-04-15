#include "editor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/collider_component.hpp"
#include "../engine/components/mesh_renderer_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_2d.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/rigid_body_component.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/components/scale_component_3d.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char ADD_ENTITY_POPUP_TITLE[] = "Add Entity";

    struct EntityPickerCategory
    {
      const char *id;
      const char *label;
      const char *icon;
    };

    struct EntityPickerOption
    {
      const char *label;
      const char *description;
      const char *searchTerms;
      const char *icon;
      const char *categoryId;
      EditorEntityPreset preset = EditorEntityPreset::None;
    };

    constexpr std::array<EntityPickerCategory, 4> ENTITY_PICKER_CATEGORIES{{
        {"scene", "Scene", ICON_FA_LAYER_GROUP},
        {"primitives", "Primitives", ICON_FA_SHAPES},
        {"audio", "Audio", ICON_FA_VOLUME_HIGH},
        {"lighting", "Lighting", ICON_FA_LIGHTBULB},
    }};

    constexpr std::array<EntityPickerOption, 9> ENTITY_PICKER_OPTIONS{{
        {"Camera", "Adds a camera and audio listener.", "main view listener", ICON_FA_CAMERA, "scene", EditorEntityPreset::Camera},
        {"Text", "Adds a text entity.", "ui label typography", ICON_FA_FONT, "scene", EditorEntityPreset::Text},
        {"Cube", "Adds a renderable cube.", "box mesh primitive", ICON_FA_CUBE, "primitives", EditorEntityPreset::Cube},
        {"Plane", "Adds a flat plane primitive.", "ground floor quad", ICON_FA_VECTOR_SQUARE, "primitives", EditorEntityPreset::Plane},
        {"Physics Cube", "Adds a cube with rigid body and collider.", "physics rigid body collider", ICON_FA_WAND_MAGIC_SPARKLES, "primitives", EditorEntityPreset::PhysicsCube},
        {"Audio Emitter", "Adds a positional audio source.", "speaker sound music", ICON_FA_VOLUME_HIGH, "audio", EditorEntityPreset::AudioEmitter},
        {"Directional Light", "Adds a sun-style directional light.", "sun light shadow", ICON_FA_SUN, "lighting", EditorEntityPreset::DirectionalLight},
        {"Point Light", "Adds an omni-directional point light.", "bulb omni light", ICON_FA_LIGHTBULB, "lighting", EditorEntityPreset::PointLight},
        {"Spot Light", "Adds a cone-shaped spot light.", "flashlight cone beam", ICON_FA_DRAW_POLYGON, "lighting", EditorEntityPreset::SpotLight},
    }};

    std::string to_lower(std::string_view text)
    {
      std::string result(text);
      std::transform(
          result.begin(),
          result.end(),
          result.begin(),
          [](const unsigned char character)
          {
            return static_cast<char>(std::tolower(character));
          });
      return result;
    }

    bool entity_picker_option_matches(const EntityPickerOption &option, const char *filter)
    {
      if (filter == nullptr || filter[0] == '\0')
      {
        return true;
      }

      const std::string lowerFilter = to_lower(filter);
      const std::string lowerLabel = to_lower(option.label);
      const std::string lowerDescription = to_lower(option.description);
      const std::string lowerSearchTerms = to_lower(option.searchTerms);

      return lowerLabel.find(lowerFilter) != std::string::npos ||
             lowerDescription.find(lowerFilter) != std::string::npos ||
             lowerSearchTerms.find(lowerFilter) != std::string::npos;
    }

    bool category_has_matches(const EntityPickerCategory &category, const char *filter)
    {
      for (const auto &option : ENTITY_PICKER_OPTIONS)
      {
        if (std::string_view(option.categoryId) == category.id &&
            entity_picker_option_matches(option, filter))
        {
          return true;
        }
      }

      return false;
    }

    std::string entity_display_label(Entity::EntityId entity, ComponentManager &componentManager)
    {
      std::string name = "Entity";
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        name = componentManager.getComponent<NameComponent>(entity).value;
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
      remove_component_if_present<RenderComponent>(componentManager, entity);
      remove_component_if_present<LightComponent>(componentManager, entity);
      remove_component_if_present<ScriptComponent>(componentManager, entity);
      remove_component_if_present<MeshRendererComponent>(componentManager, entity);
      remove_component_if_present<ColliderComponent>(componentManager, entity);
      remove_component_if_present<RigidBodyComponent>(componentManager, entity);
      remove_component_if_present<RotationComponent3D>(componentManager, entity);
      remove_component_if_present<ScaleComponent3D>(componentManager, entity);

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

  void Editor::request_add_entity_picker(Entity::EntityId parent)
  {
    state.selectedEntity = parent;
    pendingAddEntityParent_ = parent;
    addEntitySearchBuffer_[0] = '\0';
    focusAddEntitySearch_ = true;
    openAddEntityDialog_ = true;
  }

  void Editor::request_entity_deletion(Entity::EntityId entity)
  {
    pendingEntityDeletion_ = entity;
  }

  void Editor::render_add_entity_dialog(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;

    auto reset_add_entity_dialog = [this]()
    {
      pendingAddEntityParent_.reset();
      focusAddEntitySearch_ = false;
      addEntitySearchBuffer_[0] = '\0';
    };

    if (openAddEntityDialog_)
    {
      ImGui::OpenPopup(ADD_ENTITY_POPUP_TITLE);
      openAddEntityDialog_ = false;
    }
    else if (pendingAddEntityParent_.has_value() && !ImGui::IsPopupOpen(ADD_ENTITY_POPUP_TITLE))
    {
      reset_add_entity_dialog();
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 420.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(ADD_ENTITY_POPUP_TITLE, nullptr, ImGuiWindowFlags_NoResize))
    {
      return;
    }

    if (!pendingAddEntityParent_.has_value() ||
        !componentManager.hasComponent<TransformHierarchyComponent>(*pendingAddEntityParent_))
    {
      reset_add_entity_dialog();
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }

    ImGui::TextWrapped("Choose an entity type to add under:");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.78f, 0.84f, 1.0f));
    const std::string parentLabel = entity_label(*pendingAddEntityParent_, componentManager);
    ImGui::TextWrapped("%s", parentLabel.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (focusAddEntitySearch_)
    {
      ImGui::SetKeyboardFocusHere();
      focusAddEntitySearch_ = false;
    }

    ImGui::InputTextWithHint(
        "##addentityfilter",
        ICON_FA_MAGNIFYING_GLASS "  Search entities...",
        addEntitySearchBuffer_.data(),
        addEntitySearchBuffer_.size());

    ImGui::Spacing();
    ImGui::BeginChild("AddEntityList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f), true);

    bool pickedEntity = false;
    for (const auto &category : ENTITY_PICKER_CATEGORIES)
    {
      if (!category_has_matches(category, addEntitySearchBuffer_.data()))
      {
        continue;
      }

      if (addEntitySearchBuffer_[0] != '\0')
      {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      }

      const bool open = ImGui::TreeNodeEx(
          category.id,
          ImGuiTreeNodeFlags_OpenOnArrow |
              ImGuiTreeNodeFlags_OpenOnDoubleClick |
              ImGuiTreeNodeFlags_SpanAvailWidth |
              ImGuiTreeNodeFlags_DefaultOpen,
          "%s %s",
          category.icon,
          category.label);

      if (!open)
      {
        continue;
      }

      for (const auto &option : ENTITY_PICKER_OPTIONS)
      {
        if (std::string_view(option.categoryId) != category.id ||
            !entity_picker_option_matches(option, addEntitySearchBuffer_.data()))
        {
          continue;
        }

        ImGui::PushID(option.label);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 6.0f));
        ImGui::TreeNodeEx(
            "entity_option",
            ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s %s",
            option.icon,
            option.label);
        ImGui::PopStyleVar();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && option.description[0] != '\0')
        {
          ImGui::SetTooltip("%s", option.description);
        }

        if (ImGui::IsItemClicked())
        {
          request_entity_creation(option.preset, *pendingAddEntityParent_);

          reset_add_entity_dialog();
          ImGui::CloseCurrentPopup();
          pickedEntity = true;
        }

        ImGui::PopID();

        if (pickedEntity)
        {
          break;
        }
      }

      ImGui::TreePop();

      if (pickedEntity)
      {
        break;
      }
    }

    if (!pickedEntity)
    {
      bool hasVisibleOptions = false;
      for (const auto &category : ENTITY_PICKER_CATEGORIES)
      {
        if (category_has_matches(category, addEntitySearchBuffer_.data()))
        {
          hasVisibleOptions = true;
          break;
        }
      }

      if (!hasVisibleOptions)
      {
        ImGui::TextDisabled("No entity types match that search.");
      }
    }

    ImGui::EndChild();

    if (ImGui::Button("Cancel"))
    {
      reset_add_entity_dialog();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
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
    case EditorEntityPreset::Plane:
      createdEntity = EntityFactory::createPlane(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::PhysicsCube:
      createdEntity = EntityFactory::createPhysicsCube(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::DirectionalLight:
      createdEntity = EntityFactory::createDirectionalLight(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::PointLight:
      createdEntity = EntityFactory::createPointLight(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::SpotLight:
      createdEntity = EntityFactory::createSpotLight(entityManager, componentManager, parent);
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
      stop_play_mode(entityManager, componentManager, scriptRuntime);
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
      stop_play_mode(entityManager, componentManager, scriptRuntime);
      break;
    }

    state.pendingPlayAction = EditorPlayAction::None;
  }

  void Editor::start_play_mode(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    playModeSnapshot_ = snapshot_all_worlds(entityManager, componentManager);
    prePlaySelectedEntity_ = state.selectedEntity;
    prePlayLoadedWorld_ = state.loadedWorld;

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

  void Editor::stop_play_mode(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    scriptRuntime.stop();

    if (!playModeSnapshot_.empty())
    {
      std::unordered_map<Entity::EntityId, Entity::EntityId> idMap;
      restore_all_worlds_from_snapshot(playModeSnapshot_, entityManager, componentManager, &idMap);

      if (prePlayLoadedWorld_.has_value())
      {
        auto it = idMap.find(*prePlayLoadedWorld_);
        state.loadedWorld = (it != idMap.end()) ? it->second : std::optional<Entity::EntityId>{};
      }
      else
      {
        state.loadedWorld.reset();
      }

      if (prePlaySelectedEntity_.has_value())
      {
        auto it = idMap.find(*prePlaySelectedEntity_);
        state.selectedEntity = (it != idMap.end()) ? it->second : state.loadedWorld;
      }
      else
      {
        state.selectedEntity = state.loadedWorld;
      }

      playModeSnapshot_ = nlohmann::json();
      prePlaySelectedEntity_.reset();
      prePlayLoadedWorld_.reset();
    }

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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 6.0f));
    const bool open = ImGui::TreeNodeEx("entity", flags, "%s", label.c_str());
    ImGui::PopStyleVar();
    if (ImGui::IsItemClicked())
    {
      select_entity(entity);
    }

    if (ImGui::BeginPopupContextItem())
    {
      if (ImGui::MenuItem(ICON_FA_CIRCLE_PLUS "  Add Entity"))
      {
        request_add_entity_picker(entity);
        ImGui::CloseCurrentPopup();
      }

      ImGui::Separator();
      if (ImGui::MenuItem(ICON_FA_TRASH "  Delete Entity and Children"))
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
