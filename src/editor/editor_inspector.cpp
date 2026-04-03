#include "editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  namespace
  {
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char COMPONENTS_WINDOW_TITLE[] = "Components";

    const char *primitive_type_label(PrimitiveType type)
    {
      switch (type)
      {
      case PrimitiveType::Cube:
        return "Cube";
      }

      return "Unknown";
    }

    std::string script_component_label(const ScriptAttachment &attachment, std::size_t index)
    {
      std::string suffix;
      if (!attachment.className.empty())
      {
        suffix = attachment.className;
      }
      else if (!attachment.scriptPath.empty())
      {
        suffix = std::filesystem::path(attachment.scriptPath).stem().string();
      }

      std::string label = "Script Component " + std::to_string(index + 1);
      if (!suffix.empty())
      {
        label += ": " + suffix;
      }

      return label;
    }

  }

  void Editor::properties(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;

    ImGui::Begin(PROPERTIES_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      ImGui::TextDisabled("No selection.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    const bool isWorld = componentManager.hasComponent<WorldComponent>(entity);
    ImGui::Text("%s %u", isWorld ? "World" : "Entity", entity);
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
    else
    {
      ImGui::TextDisabled("Unnamed");
    }

    if (isWorld)
    {
      const auto &world = componentManager.getComponent<WorldComponent>(entity);
      ImGui::Text("Startup World: %s", world.isDefault ? "Yes" : "No");
    }

    ImGui::End();
  }

  void Editor::components(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(COMPONENTS_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      ImGui::TextDisabled("No selection.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    const bool isWorld = componentManager.hasComponent<WorldComponent>(entity);

    ImGui::Text("%s %u", isWorld ? "World" : "Entity", entity);
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      ImGui::TextDisabled("%s", componentManager.getComponent<NameComponent>(entity).value.c_str());
    }
    ImGui::Separator();

    if (isWorld)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add Script Component"))
    {
      if (!componentManager.hasComponent<ScriptComponent>(entity))
      {
        ScriptComponent scriptComponent;
        scriptComponent.attachments.push_back(ScriptAttachment());
        componentManager.addComponent(entity, scriptComponent);
      }
      else
      {
        componentManager.getComponent<ScriptComponent>(entity).attachments.push_back(ScriptAttachment());
      }
    }
    if (isWorld)
    {
      ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (componentManager.hasComponent<WorldComponent>(entity) && ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen))
    {
      const auto &world = componentManager.getComponent<WorldComponent>(entity);
      ImGui::Text("Startup World: %s", world.isDefault ? "Yes" : "No");
      if (!world.isDefault && ImGui::Button("Set As Default World"))
      {
        set_default_world(entity, entityManager, componentManager);
      }
    }

    if (componentManager.hasComponent<PositionComponent3D>(entity) && ImGui::CollapsingHeader("Transform"))
    {
      auto &position = componentManager.getComponent<PositionComponent3D>(entity);
      ImGui::DragFloat3("Position", &position.x, 0.1f);
    }

    if (componentManager.hasComponent<CameraComponent>(entity) && ImGui::CollapsingHeader("Camera"))
    {
      auto &camera = componentManager.getComponent<CameraComponent>(entity);
      bool isMainCamera = camera.isMainCamera;

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
    }

    if (componentManager.hasComponent<AudioListenerComponent>(entity) && ImGui::CollapsingHeader("Audio Listener"))
    {
      auto &listener = componentManager.getComponent<AudioListenerComponent>(entity);
      ImGui::Checkbox("Listener Enabled", &listener.enabled);
      ImGui::DragFloat3("Listener Forward", &listener.forwardX, 0.01f, -1.0f, 1.0f);
      ImGui::DragFloat3("Listener Up", &listener.upX, 0.01f, -1.0f, 1.0f);
    }

    if (componentManager.hasComponent<PrimitiveComponent>(entity) && ImGui::CollapsingHeader("Primitive"))
    {
      const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
      ImGui::Text("Type: %s", primitive_type_label(primitive.type));
    }

    if (componentManager.hasComponent<TextComponent>(entity) && ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto &text = componentManager.getComponent<TextComponent>(entity);
      ImGui::InputTextMultiline(
          "Content",
          &text.content,
          ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() * 6.0f));
      ImGui::DragFloat("Font Size", &text.fontSize, 0.05f, 0.05f, 64.0f);
      ImGui::DragFloat("Wrap Width", &text.wrapWidth, 0.05f, 0.0f, 256.0f);
      ImGui::DragFloat("Line Spacing", &text.lineSpacing, 0.01f, 0.8f, 4.0f);
      ImGui::DragFloat("Yaw", &text.yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f deg");
      ImGui::DragFloat("Pitch", &text.pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f deg");
      ImGui::DragFloat("Roll", &text.rollDegrees, 0.5f, -180.0f, 180.0f, "%.1f deg");

      if (text.fontSize < 0.05f)
      {
        text.fontSize = 0.05f;
      }
      if (text.wrapWidth < 0.0f)
      {
        text.wrapWidth = 0.0f;
      }
      if (text.lineSpacing < 0.8f)
      {
        text.lineSpacing = 0.8f;
      }

      text.yawDegrees = std::remainder(text.yawDegrees, 360.0f);
      text.pitchDegrees = std::clamp(text.pitchDegrees, -89.0f, 89.0f);
      text.rollDegrees = std::remainder(text.rollDegrees, 360.0f);

      ImGui::TextDisabled("Text is rendered as world-space vector strokes.");
    }

    if (componentManager.hasComponent<AudioSourceComponent>(entity) && ImGui::CollapsingHeader("Audio Source"))
    {
      auto &source = componentManager.getComponent<AudioSourceComponent>(entity);
      std::array<char, 260> assetPathBuffer{};
      std::snprintf(assetPathBuffer.data(), assetPathBuffer.size(), "%s", source.assetPath.c_str());

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
    }

    if (componentManager.hasComponent<ModelComponent>(entity) && ImGui::CollapsingHeader("Imported Model"))
    {
      const auto &modelComponent = componentManager.getComponent<ModelComponent>(entity);
      const auto &model = modelComponent.model;

      ImGui::TextWrapped("%s", model.sourcePath.c_str());
      ImGui::Text("Format: %s", model.formatHint.empty() ? "Unknown" : model.formatHint.c_str());
      ImGui::Text("Meshes: %zu", model.meshes.size());
      ImGui::Text("Materials: %zu", model.materials.size());
      ImGui::Text("Vertices: %zu", model.totalVertexCount);
      ImGui::Text("Faces: %zu", model.totalFaceCount);

      if (ImGui::CollapsingHeader("Mesh Details"))
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
    }

    if (componentManager.hasComponent<RenderComponent>(entity) && ImGui::CollapsingHeader("Render"))
    {
      const auto &render = componentManager.getComponent<RenderComponent>(entity);
      ImGui::Text("Program: %d", render.program);
    }

    if (componentManager.hasComponent<TransformHierarchyComponent>(entity) &&
        ImGui::CollapsingHeader("Transform Hierarchy"))
    {
      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);

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
        ImGui::TextDisabled("%s", isWorld ? "World Root" : "Parent: Root");
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

    if (componentManager.hasComponent<ScriptComponent>(entity))
    {
      auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
      std::optional<std::size_t> removeAttachmentIndex;

      if (scriptComponent.attachments.empty() && ImGui::CollapsingHeader("Scripts"))
      {
        ImGui::TextDisabled("No script components attached.");
      }

      for (std::size_t index = 0; index < scriptComponent.attachments.size(); ++index)
      {
        auto &attachment = scriptComponent.attachments[index];
        ImGui::PushID(static_cast<int>(index));

        const std::string label = script_component_label(attachment, index);
        if (ImGui::CollapsingHeader(label.c_str()))
        {
          ImGui::Checkbox("Enabled", &attachment.enabled);
          std::vector<std::string> scriptOptions = workspaceScriptFiles_;
          if (!attachment.scriptPath.empty() &&
              std::find(scriptOptions.begin(), scriptOptions.end(), attachment.scriptPath) == scriptOptions.end())
          {
            scriptOptions.push_back(attachment.scriptPath);
            std::sort(scriptOptions.begin(), scriptOptions.end());
          }

          const std::string previousScriptPath = attachment.scriptPath;
          const std::string previewValue = attachment.scriptPath.empty() ? "<Select a workspace script>" : attachment.scriptPath;
          if (ImGui::BeginCombo("Script", previewValue.c_str()))
          {
            const bool noneSelected = attachment.scriptPath.empty();
            if (ImGui::Selectable("<None>", noneSelected))
            {
              attachment.scriptPath.clear();
            }
            if (noneSelected)
            {
              ImGui::SetItemDefaultFocus();
            }

            for (const auto &scriptPath : scriptOptions)
            {
              const bool selected = (attachment.scriptPath == scriptPath);
              if (ImGui::Selectable(scriptPath.c_str(), selected))
              {
                attachment.scriptPath = scriptPath;
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          if (previousScriptPath != attachment.scriptPath)
          {
            attachment.className.clear();
          }

          if (scriptOptions.empty())
          {
            ImGui::TextDisabled("No scripts.");
          }
          else if (!attachment.scriptPath.empty())
          {
            ImGui::TextDisabled("%s", attachment.scriptPath.c_str());
            if (!activeWorkspacePath_.empty() && ImGui::Button("Open in Script Editor"))
            {
              request_script_editor_open(activeWorkspacePath_ / attachment.scriptPath, attachment.scriptPath);
            }
          }

          const std::vector<ParsedScriptClass> *parsedClasses = nullptr;
          const ParsedScriptClass *selectedClass = nullptr;
          if (!attachment.scriptPath.empty() && !activeWorkspacePath_.empty())
          {
            const auto resolvedPath = activeWorkspacePath_ / attachment.scriptPath;
            const std::string pathKey = resolvedPath.string();

            std::error_code modEc;
            const auto modTime = std::filesystem::last_write_time(resolvedPath, modEc);
            bool needsParse = false;
            if (!modEc)
            {
              auto modIt = parsedScriptModTimes_.find(pathKey);
              if (modIt == parsedScriptModTimes_.end() || modIt->second != modTime)
              {
                needsParse = true;
                parsedScriptModTimes_[pathKey] = modTime;
              }
            }
            else
            {
              parsedScriptCache_.erase(pathKey);
              parsedScriptModTimes_.erase(pathKey);
            }

            if (needsParse)
            {
              parsedScriptCache_[pathKey] = parse_script_classes(resolvedPath);
            }

            const auto cacheIt = parsedScriptCache_.find(pathKey);
            if (cacheIt != parsedScriptCache_.end())
            {
              parsedClasses = &cacheIt->second;
            }
          }

          if (previousScriptPath != attachment.scriptPath)
          {
            if (parsedClasses != nullptr && !parsedClasses->empty())
            {
              attachment.className = parsedClasses->front().qualifiedName;
            }
            else
            {
              attachment.className.clear();
            }
          }
          else if (parsedClasses != nullptr && !parsedClasses->empty() &&
                   find_script_class(*parsedClasses, attachment.className) == nullptr)
          {
            attachment.className = parsedClasses->front().qualifiedName;
          }
          else if (parsedClasses != nullptr && parsedClasses->empty())
          {
            attachment.className.clear();
          }

          if (parsedClasses != nullptr && !parsedClasses->empty())
          {
            selectedClass = find_script_class(*parsedClasses, attachment.className);
            if (selectedClass == nullptr)
            {
              attachment.className = parsedClasses->front().qualifiedName;
              selectedClass = &parsedClasses->front();
            }
          }

          if (attachment.scriptPath.empty())
          {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("Class", "<Select a script first>"))
            {
              ImGui::EndCombo();
            }
            ImGui::EndDisabled();
          }
          else if (parsedClasses == nullptr || parsedClasses->empty())
          {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("Class", "<No script classes found>"))
            {
              ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("No HadesScript classes found in this file.");
          }
          else if (selectedClass != nullptr &&
                   ImGui::BeginCombo("Class", selectedClass->qualifiedName.c_str()))
          {
            for (const auto &parsedClass : *parsedClasses)
            {
              const bool selected = (selectedClass == &parsedClass);
              if (ImGui::Selectable(parsedClass.qualifiedName.c_str(), selected))
              {
                attachment.className = parsedClass.qualifiedName;
                selectedClass = find_script_class(*parsedClasses, attachment.className);
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          if (ImGui::Button("Remove Script Component"))
          {
            removeAttachmentIndex = index;
          }

          if (selectedClass != nullptr && !selectedClass->publicFields.empty())
          {
            const auto &fields = selectedClass->publicFields;

            std::set<std::string> currentFieldNames;
            for (const auto &[type, name] : fields)
            {
              currentFieldNames.insert(name);
            }
            for (auto it = attachment.publicFieldValues.begin(); it != attachment.publicFieldValues.end();)
            {
              if (currentFieldNames.find(it->first) == currentFieldNames.end())
              {
                it = attachment.publicFieldValues.erase(it);
              }
              else
              {
                ++it;
              }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Public Fields:");
            for (const auto &[type, name] : fields)
            {
              auto &value = attachment.publicFieldValues[name];
              std::array<char, 256> fieldBuffer{};
              std::snprintf(fieldBuffer.data(), fieldBuffer.size(), "%s", value.c_str());

              const std::string fieldLabel = name + " (" + type + ")";
              if (ImGui::InputText(fieldLabel.c_str(), fieldBuffer.data(), fieldBuffer.size()))
              {
                value = fieldBuffer.data();
              }
            }
          }
        }

        ImGui::PopID();
      }

      if (removeAttachmentIndex.has_value())
      {
        scriptComponent.attachments.erase(scriptComponent.attachments.begin() + static_cast<std::ptrdiff_t>(*removeAttachmentIndex));
      }
    }

    ImGui::End();
  }
}
