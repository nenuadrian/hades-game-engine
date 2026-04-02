#include "editor.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/gui/imgui.hpp"
#include "../engine/runtime/main_camera_selection.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char COMPONENTS_WINDOW_TITLE[] = "Components";
    constexpr char GAME_WINDOW_TITLE[] = "Game";
    constexpr char IMPORT_MODEL_POPUP_TITLE[] = "Import Model";
    constexpr float PI = 3.14159265358979323846f;
    constexpr float CUBE_HALF_EXTENT = 0.5f;

    struct Vec3
    {
      float x;
      float y;
      float z;
    };

    const char *primitive_type_label(PrimitiveType type)
    {
      switch (type)
      {
      case PrimitiveType::Cube:
        return "Cube";
      }

      return "Unknown";
    }

    void render_selection_hint(const char *message)
    {
      ImGui::TextDisabled("%s", message);
    }

    void render_component_entry(const char *label)
    {
      ImGui::BulletText("%s", label);
    }

    Vec3 make_vec3(float x, float y, float z)
    {
      return Vec3{x, y, z};
    }

    Vec3 make_vec3(const PositionComponent3D &position)
    {
      return make_vec3(position.x, position.y, position.z);
    }

    bool project_point(
        const Vec3 &worldPoint,
        const PositionComponent3D &cameraPosition,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        ImVec2 &screenPoint)
    {
      if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f || camera.fovY <= 0.0f)
      {
        return false;
      }

      const float relativeX = worldPoint.x - cameraPosition.x;
      const float relativeY = worldPoint.y - cameraPosition.y;
      const float relativeZ = worldPoint.z - cameraPosition.z;
      if (relativeZ <= camera.nearClip || relativeZ >= camera.farClip)
      {
        return false;
      }

      const float aspectRatio = canvasSize.x / canvasSize.y;
      const float halfFovRadians = (camera.fovY * 0.5f) * (PI / 180.0f);
      const float tanHalfFov = std::tan(halfFovRadians);
      if (tanHalfFov <= 0.0f)
      {
        return false;
      }

      const float normalizedX = relativeX / (relativeZ * tanHalfFov * aspectRatio);
      const float normalizedY = relativeY / (relativeZ * tanHalfFov);
      if (std::abs(normalizedX) > 10.0f || std::abs(normalizedY) > 10.0f)
      {
        return false;
      }

      screenPoint.x = canvasOrigin.x + ((normalizedX + 1.0f) * 0.5f * canvasSize.x);
      screenPoint.y = canvasOrigin.y + ((1.0f - normalizedY) * 0.5f * canvasSize.y);
      return true;
    }

    std::array<Vec3, 8> cube_corners(const PositionComponent3D &position)
    {
      return {
          make_vec3(position.x - CUBE_HALF_EXTENT, position.y - CUBE_HALF_EXTENT, position.z - CUBE_HALF_EXTENT),
          make_vec3(position.x + CUBE_HALF_EXTENT, position.y - CUBE_HALF_EXTENT, position.z - CUBE_HALF_EXTENT),
          make_vec3(position.x + CUBE_HALF_EXTENT, position.y + CUBE_HALF_EXTENT, position.z - CUBE_HALF_EXTENT),
          make_vec3(position.x - CUBE_HALF_EXTENT, position.y + CUBE_HALF_EXTENT, position.z - CUBE_HALF_EXTENT),
          make_vec3(position.x - CUBE_HALF_EXTENT, position.y - CUBE_HALF_EXTENT, position.z + CUBE_HALF_EXTENT),
          make_vec3(position.x + CUBE_HALF_EXTENT, position.y - CUBE_HALF_EXTENT, position.z + CUBE_HALF_EXTENT),
          make_vec3(position.x + CUBE_HALF_EXTENT, position.y + CUBE_HALF_EXTENT, position.z + CUBE_HALF_EXTENT),
          make_vec3(position.x - CUBE_HALF_EXTENT, position.y + CUBE_HALF_EXTENT, position.z + CUBE_HALF_EXTENT),
      };
    }
  }

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

    MenuBarItem addEntity;
    addEntity.title = "Add Entity";

    MenuBarItem addCamera;
    addCamera.title = "Camera";
    addCamera.on_activate = [this]()
    {
      state.pendingEntityPreset = EditorEntityPreset::Camera;
    };

    MenuBarItem addCube;
    addCube.title = "Cube";
    addCube.on_activate = [this]()
    {
      state.pendingEntityPreset = EditorEntityPreset::Cube;
    };

    MenuBarItem addAudioEmitter;
    addAudioEmitter.title = "Audio Emitter";
    addAudioEmitter.on_activate = [this]()
    {
      state.pendingEntityPreset = EditorEntityPreset::AudioEmitter;
    };

    MenuBarItem importModel;
    importModel.title = "Import Model...";
    importModel.on_activate = [this]()
    {
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
    };

    addEntity.children_menu_items.push_back(addCamera);
    addEntity.children_menu_items.push_back(addCube);
    addEntity.children_menu_items.push_back(addAudioEmitter);
    addEntity.children_menu_items.push_back(importModel);
    gui->menu_bar_items.push_back(addEntity);

    MenuBarItem game;
    game.title = "Game";

    MenuBarItem play;
    play.title = "Play";
    play.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Start;
    };

    MenuBarItem stop;
    stop.title = "Stop";
    stop.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Stop;
    };

    game.children_menu_items.push_back(play);
    game.children_menu_items.push_back(stop);
    gui->menu_bar_items.push_back(game);
  }

  Editor::~Editor() = default;

  void Editor::render(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager)
  {
    configure_default_dock_layout(gui->render_frame());
    handle_entity_creation_requests(entityManager, componentManager);
    import_model(entityManager, componentManager);
    handle_play_mode_requests(entityManager, componentManager);
    entities(entityManager, componentManager);
    properties(entityManager, componentManager);
    components(componentManager);
    game(entityManager, componentManager);
    debug(deltaTime);
  }

  void Editor::configure_default_dock_layout(std::uint32_t dockspaceId)
  {
    if (dockLayoutInitialized || dockspaceId == 0)
    {
      return;
    }

    ImGuiDockNode *existingNode = ImGui::DockBuilderGetNode(dockspaceId);
    if (existingNode != nullptr && existingNode->ChildNodes[0] != nullptr)
    {
      dockLayoutInitialized = true;
      return;
    }

    dockLayoutInitialized = true;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID mainDockId = dockspaceId;
    const ImGuiID entitiesDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.22f, nullptr, &mainDockId);
    ImGuiID inspectorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.34f, nullptr, &mainDockId);
    const ImGuiID componentsDockId = ImGui::DockBuilderSplitNode(inspectorDockId, ImGuiDir_Right, 0.45f, nullptr, &inspectorDockId);

    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(COMPONENTS_WINDOW_TITLE, componentsDockId);
    ImGui::DockBuilderDockWindow(GAME_WINDOW_TITLE, mainDockId);
    ImGui::DockBuilderFinish(dockspaceId);
  }

  void Editor::handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (state.pendingEntityPreset == EditorEntityPreset::None)
    {
      return;
    }

    const auto parent = get_selected_parent(componentManager);
    Entity::EntityId createdEntity = Entity::INVALID;

    switch (state.pendingEntityPreset)
    {
    case EditorEntityPreset::Camera:
      createdEntity = EntityFactory::createCamera(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::Cube:
      createdEntity = EntityFactory::createCube(entityManager, componentManager, parent);
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

    ImGui::TextWrapped("Import a mesh scene supported by Assimp and attach it as a model entity.");
    ImGui::InputText("Path", importModelPathBuffer.data(), importModelPathBuffer.size());

    if (!importModelError.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", importModelError.c_str());
    }

    if (ImGui::Button("Import"))
    {
      std::string errorMessage;
      const auto parent = get_selected_parent(componentManager);
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

  void Editor::handle_play_mode_requests(EntityManager &entityManager, ComponentManager &componentManager)
  {
    switch (state.pendingPlayAction)
    {
    case EditorPlayAction::None:
      return;
    case EditorPlayAction::Start:
      start_play_mode(entityManager, componentManager);
      break;
    case EditorPlayAction::Stop:
      stop_play_mode();
      break;
    }

    state.pendingPlayAction = EditorPlayAction::None;
  }

  void Editor::start_play_mode(EntityManager &entityManager, ComponentManager &componentManager)
  {
    const auto selection = select_main_camera(entityManager, componentManager);
    if (selection.status != MainCameraSelectionStatus::Ready || !selection.entity.has_value())
    {
      state.isPlaying = false;
      state.activeCamera.reset();
      state.playModeMessage = main_camera_selection_message(selection.status);
      return;
    }

    state.isPlaying = true;
    state.activeCamera = selection.entity;
    state.playModeMessage.clear();
  }

  void Editor::stop_play_mode()
  {
    state.isPlaying = false;
    state.activeCamera.reset();
    state.playModeMessage.clear();
  }

  void Editor::set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager)
  {
    for (Entity::EntityId current : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<CameraComponent>(current))
      {
        continue;
      }

      auto &camera = componentManager.getComponent<CameraComponent>(current);
      camera.isMainCamera = (current == entity);
    }
  }

  std::optional<Entity::EntityId> Editor::get_selected_parent(ComponentManager &componentManager) const
  {
    if (!state.selectedEntity.has_value())
    {
      return std::nullopt;
    }

    if (!componentManager.hasComponent<TransformHierarchyComponent>(*state.selectedEntity))
    {
      return std::nullopt;
    }

    return state.selectedEntity;
  }

  std::string Editor::entity_label(Entity::EntityId entity, ComponentManager &componentManager) const
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

    return name + " (" + std::to_string(entity) + ")";
  }

  void Editor::entities(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(ENTITY_WINDOW_TITLE);
    render_hierarchies(entityManager, componentManager);
    ImGui::End();
  }

  void Editor::properties(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(PROPERTIES_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      render_selection_hint("Select an entity to edit its properties.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    ImGui::Text("Entity %u", entity);
    ImGui::Separator();

    if (componentManager.hasComponent<NameComponent>(entity))
    {
      auto &name = componentManager.getComponent<NameComponent>(entity);
      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", name.value.c_str());
      if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
      {
        name.value = nameBuffer.data();
      }
    }

    if (componentManager.hasComponent<PositionComponent3D>(entity))
    {
      auto &position = componentManager.getComponent<PositionComponent3D>(entity);
      ImGui::TextUnformatted("Transform");
      ImGui::DragFloat3("Position", &position.x, 0.1f);
      ImGui::Separator();
    }

    if (componentManager.hasComponent<CameraComponent>(entity))
    {
      auto &camera = componentManager.getComponent<CameraComponent>(entity);
      bool isMainCamera = camera.isMainCamera;

      ImGui::TextUnformatted("Camera");
      if (ImGui::Checkbox("Main Camera", &isMainCamera))
      {
        if (isMainCamera)
        {
          set_main_camera(entity, entityManager, componentManager);
        }
        else
        {
          camera.isMainCamera = false;
        }

        state.playModeMessage.clear();
      }

      ImGui::DragFloat("Field of view", &camera.fovY, 0.5f, 1.0f, 179.0f);
      ImGui::DragFloat("Near clip", &camera.nearClip, 0.01f, 0.001f, camera.farClip);
      ImGui::DragFloat("Far clip", &camera.farClip, 1.0f, camera.nearClip, 10000.0f);

      if (camera.fovY < 1.0f)
      {
        camera.fovY = 1.0f;
      }
      else if (camera.fovY > 179.0f)
      {
        camera.fovY = 179.0f;
      }
      if (camera.nearClip < 0.001f)
      {
        camera.nearClip = 0.001f;
      }
      if (camera.farClip <= camera.nearClip)
      {
        camera.farClip = camera.nearClip + 0.001f;
      }

      ImGui::Separator();
    }

    if (componentManager.hasComponent<AudioListenerComponent>(entity))
    {
      auto &listener = componentManager.getComponent<AudioListenerComponent>(entity);
      ImGui::TextUnformatted("Audio Listener");
      ImGui::Checkbox("Listener Enabled", &listener.enabled);
      ImGui::DragFloat3("Listener Forward", &listener.forwardX, 0.01f, -1.0f, 1.0f);
      ImGui::DragFloat3("Listener Up", &listener.upX, 0.01f, -1.0f, 1.0f);
      ImGui::Separator();
    }

    if (componentManager.hasComponent<AudioSourceComponent>(entity))
    {
      auto &source = componentManager.getComponent<AudioSourceComponent>(entity);
      std::array<char, 260> assetPathBuffer{};
      std::snprintf(assetPathBuffer.data(), assetPathBuffer.size(), "%s", source.assetPath.c_str());

      ImGui::TextUnformatted("Audio Source");
      if (ImGui::InputText("Audio Clip", assetPathBuffer.data(), assetPathBuffer.size()))
      {
        source.assetPath = assetPathBuffer.data();
      }

      int selectedBus = static_cast<int>(source.bus);
      const char *busLabels[] = {"Master", "Music", "SFX", "Voice"};
      if (ImGui::Combo("Audio Bus", &selectedBus, busLabels, IM_ARRAYSIZE(busLabels)))
      {
        source.bus = static_cast<AudioBus>(selectedBus);
      }

      ImGui::Checkbox("Play On Start", &source.playOnStart);
      ImGui::Checkbox("Looping", &source.looping);
      ImGui::Checkbox("Streaming", &source.streaming);
      ImGui::Checkbox("Spatialized", &source.spatialized);
      ImGui::SliderFloat("Volume", &source.volume, 0.0f, 2.0f);
      ImGui::SliderFloat("Pitch", &source.pitch, 0.1f, 4.0f);

      if (source.spatialized)
      {
        ImGui::DragFloat("Min Distance", &source.minDistance, 0.1f, 0.1f, 1000.0f);
        ImGui::DragFloat("Max Distance", &source.maxDistance, 0.5f, source.minDistance, 5000.0f);
        ImGui::SliderFloat("Rolloff", &source.rolloff, 0.1f, 4.0f);
      }

      if (source.pitch < 0.1f)
      {
        source.pitch = 0.1f;
      }
      if (source.minDistance < 0.1f)
      {
        source.minDistance = 0.1f;
      }
      if (source.maxDistance < source.minDistance)
      {
        source.maxDistance = source.minDistance;
      }
      if (source.rolloff < 0.1f)
      {
        source.rolloff = 0.1f;
      }

      ImGui::TextDisabled("Audio paths resolve relative to the engine process working directory.");
      ImGui::Separator();
    }

    if (componentManager.hasComponent<PrimitiveComponent>(entity))
    {
      const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
      ImGui::TextUnformatted("Primitive");
      ImGui::Text("Type: %s", primitive_type_label(primitive.type));
      ImGui::Separator();
    }

    if (componentManager.hasComponent<ModelComponent>(entity))
    {
      const auto &modelComponent = componentManager.getComponent<ModelComponent>(entity);
      const auto &model = modelComponent.model;

      ImGui::TextUnformatted("Imported Model");
      ImGui::TextWrapped("%s", model.sourcePath.c_str());
      ImGui::Text("Format: %s", model.formatHint.empty() ? "Unknown" : model.formatHint.c_str());
      ImGui::Text("Meshes: %zu", model.meshes.size());
      ImGui::Text("Materials: %zu", model.materials.size());
      ImGui::Text("Vertices: %zu", model.totalVertexCount);
      ImGui::Text("Faces: %zu", model.totalFaceCount);

      if (ImGui::CollapsingHeader("Mesh Details", ImGuiTreeNodeFlags_DefaultOpen))
      {
        for (std::size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
          const auto &mesh = model.meshes[meshIndex];
          ImGui::PushID(static_cast<int>(meshIndex));
          ImGui::SeparatorText(mesh.name.c_str());
          ImGui::Text("Vertices: %zu", mesh.vertexCount);
          ImGui::Text("Faces: %zu", mesh.faceCount);
          ImGui::Text("Material Slot: %zu", mesh.materialIndex);
          ImGui::PopID();
        }
      }

      if (ImGui::CollapsingHeader("Materials"))
      {
        for (const auto &material : model.materials)
        {
          ImGui::BulletText("%s", material.name.c_str());
        }
      }

      ImGui::Separator();
    }

    ImGui::End();
  }

  void Editor::components(ComponentManager &componentManager)
  {
    ImGui::Begin(COMPONENTS_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      render_selection_hint("Select an entity to inspect its components.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;

    ImGui::Text("Entity %u", entity);
    ImGui::Separator();
    ImGui::TextUnformatted("Attached Components");

    if (componentManager.hasComponent<NameComponent>(entity))
    {
      render_component_entry("Name");
    }
    if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      render_component_entry("TransformHierarchy");
    }
    if (componentManager.hasComponent<PositionComponent3D>(entity))
    {
      render_component_entry("Position3D");
    }
    if (componentManager.hasComponent<CameraComponent>(entity))
    {
      render_component_entry("Camera");
    }
    if (componentManager.hasComponent<AudioListenerComponent>(entity))
    {
      render_component_entry("AudioListener");
    }
    if (componentManager.hasComponent<PrimitiveComponent>(entity))
    {
      render_component_entry("Primitive");
    }
    if (componentManager.hasComponent<AudioSourceComponent>(entity))
    {
      render_component_entry("AudioSource");
    }
    if (componentManager.hasComponent<ModelComponent>(entity))
    {
      render_component_entry("Model");
    }
    if (componentManager.hasComponent<RenderComponent>(entity))
    {
      render_component_entry("Render");
    }

    if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      ImGui::Separator();
      ImGui::TextUnformatted("Hierarchy");

      if (hierarchy.parent.has_value())
      {
        const Entity::EntityId parent = *hierarchy.parent;
        if (ImGui::Selectable(entity_label(parent, componentManager).c_str(), false))
        {
          state.selectedEntity = parent;
        }
      }
      else
      {
        ImGui::TextDisabled("Parent: Root");
      }

      if (hierarchy.children.empty())
      {
        ImGui::TextDisabled("No child entities.");
      }
      else
      {
        ImGui::Text("Children (%zu)", hierarchy.children.size());
        for (const Entity::EntityId child : hierarchy.children)
        {
          if (ImGui::Selectable(entity_label(child, componentManager).c_str(), false))
          {
            state.selectedEntity = child;
          }
        }
      }
    }

    ImGui::End();
  }

  void Editor::game(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(GAME_WINDOW_TITLE);

    if (ImGui::Button(state.isPlaying ? "Stop" : "Play"))
    {
      if (state.isPlaying)
      {
        stop_play_mode();
      }
      else
      {
        start_play_mode(entityManager, componentManager);
      }
    }

    if (state.isPlaying && state.activeCamera.has_value())
    {
      ImGui::SameLine();
      ImGui::Text("Active Camera: %s", entity_label(*state.activeCamera, componentManager).c_str());
    }
    else
    {
      const auto selection = select_main_camera(entityManager, componentManager);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", main_camera_selection_message(selection.status));
    }

    if (!state.playModeMessage.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", state.playModeMessage.c_str());
    }

    if (!state.isPlaying || !state.activeCamera.has_value())
    {
      ImGui::Spacing();
      ImGui::TextWrapped("Play mode uses the camera marked as Main Camera and starts from that view.");
      ImGui::End();
      return;
    }

    const Entity::EntityId cameraEntity = *state.activeCamera;
    if (!componentManager.hasComponent<CameraComponent>(cameraEntity) ||
        !componentManager.hasComponent<PositionComponent3D>(cameraEntity))
    {
      ImGui::Spacing();
      ImGui::TextColored(
          ImVec4(0.88f, 0.42f, 0.42f, 1.0f),
          "The active camera no longer has the required camera and transform components.");
      ImGui::End();
      return;
    }

    const auto &camera = componentManager.getComponent<CameraComponent>(cameraEntity);
    const auto &cameraPosition = componentManager.getComponent<PositionComponent3D>(cameraEntity);

    ImGui::Spacing();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 64.0f || canvasSize.y < 64.0f)
    {
      ImGui::End();
      return;
    }

    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y);
    ImGui::InvisibleButton("game_canvas", canvasSize);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasOrigin, canvasMax, IM_COL32(17, 20, 24, 255));
    drawList->AddRect(canvasOrigin, canvasMax, IM_COL32(70, 76, 86, 255));

    const ImVec2 canvasCenter(canvasOrigin.x + (canvasSize.x * 0.5f), canvasOrigin.y + (canvasSize.y * 0.5f));
    drawList->AddLine(
        ImVec2(canvasCenter.x - 8.0f, canvasCenter.y),
        ImVec2(canvasCenter.x + 8.0f, canvasCenter.y),
        IM_COL32(90, 96, 110, 160),
        1.0f);
    drawList->AddLine(
        ImVec2(canvasCenter.x, canvasCenter.y - 8.0f),
        ImVec2(canvasCenter.x, canvasCenter.y + 8.0f),
        IM_COL32(90, 96, 110, 160),
        1.0f);

    drawList->PushClipRect(canvasOrigin, canvasMax, true);

    static constexpr int cubeEdges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    int visiblePrimitiveCount = 0;
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (entity == cameraEntity ||
          !componentManager.hasComponent<PrimitiveComponent>(entity) ||
          !componentManager.hasComponent<PositionComponent3D>(entity))
      {
        continue;
      }

      const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
      if (primitive.type != PrimitiveType::Cube)
      {
        continue;
      }

      const auto &position = componentManager.getComponent<PositionComponent3D>(entity);
      const auto corners = cube_corners(position);
      std::array<ImVec2, 8> projectedCorners{};

      bool isVisible = true;
      for (std::size_t i = 0; i < corners.size(); ++i)
      {
        if (!project_point(corners[i], cameraPosition, camera, canvasOrigin, canvasSize, projectedCorners[i]))
        {
          isVisible = false;
          break;
        }
      }

      if (!isVisible)
      {
        continue;
      }

      ++visiblePrimitiveCount;
      for (const auto &edge : cubeEdges)
      {
        drawList->AddLine(projectedCorners[edge[0]], projectedCorners[edge[1]], IM_COL32(223, 228, 235, 255), 1.5f);
      }

      ImVec2 centerPoint;
      if (project_point(make_vec3(position), cameraPosition, camera, canvasOrigin, canvasSize, centerPoint))
      {
        const std::string label = entity_label(entity, componentManager);
        drawList->AddText(
            ImVec2(centerPoint.x + 6.0f, centerPoint.y + 6.0f),
            IM_COL32(205, 210, 218, 255),
            label.c_str());
      }
    }

    if (visiblePrimitiveCount == 0)
    {
      const char *message = "No primitives are visible from the active camera.";
      const ImVec2 textSize = ImGui::CalcTextSize(message);
      drawList->AddText(
          ImVec2(canvasCenter.x - (textSize.x * 0.5f), canvasCenter.y - (textSize.y * 0.5f)),
          IM_COL32(120, 128, 142, 255),
          message);
    }

    drawList->PopClipRect();
    ImGui::End();
  }

  void Editor::render_hierarchy(Entity::EntityId entity, ComponentManager &componentManager)
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
    const bool open = ImGui::TreeNodeEx("entity", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked())
    {
      state.selectedEntity = entity;
    }

    if (open && !hierarchy.children.empty())
    {
      for (const auto &child : hierarchy.children)
      {
        render_hierarchy(child, componentManager);
      }
      ImGui::TreePop();
    }

    ImGui::PopID();
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
        render_hierarchy(entity, componentManager);
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
    ImGui::Text("Play Mode: %s", state.isPlaying ? "Playing" : "Stopped");
    if (state.activeCamera.has_value())
    {
      ImGui::Text("Active Camera: %u", *state.activeCamera);
    }
    ImGui::End();
  }
}
