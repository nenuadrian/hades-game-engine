#include "editor.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

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
#include "../engine/components/script_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/gui/imgui.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char COMPONENTS_WINDOW_TITLE[] = "Components";
    constexpr char GAME_WINDOW_TITLE[] = "Game";
    constexpr char IMPORT_MODEL_POPUP_TITLE[] = "Import Model";
    constexpr char WORKSPACE_CREATE_POPUP_TITLE[] = "Create Workspace Item";
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

    std::string path_display_name(const std::filesystem::path &path)
    {
      const std::string filename = path.filename().string();
      return filename.empty() ? path.string() : filename;
    }

    std::string relative_workspace_path(const std::filesystem::path &workspacePath, const std::filesystem::path &path)
    {
      const std::filesystem::path relativePath = path.lexically_relative(workspacePath);
      return relativePath.empty() ? path.generic_string() : relativePath.generic_string();
    }

    bool has_path_separator(const std::string &name)
    {
      return name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
    }

    std::string csharp_class_name_from_stem(const std::string &stem)
    {
      std::string className;
      bool capitalizeNext = true;
      for (const char character : stem)
      {
        const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter) == 0)
        {
          capitalizeNext = true;
          continue;
        }

        if (className.empty() && std::isdigit(unsignedCharacter) != 0)
        {
          className.push_back('_');
        }

        if (capitalizeNext)
        {
          className.push_back(static_cast<char>(std::toupper(unsignedCharacter)));
          capitalizeNext = false;
        }
        else
        {
          className.push_back(character);
        }
      }

      return className.empty() ? "NewScript" : className;
    }

    std::string build_script_template(const std::string &className)
    {
      return "using Hades.Scripting;\n\n"
             "public sealed class " +
             className +
             " : HadesScript\n"
             "{\n"
             "    public override void OnStart(EntityContext context)\n"
             "    {\n"
             "    }\n"
             "\n"
             "    public override void OnUpdate(EntityContext context, float deltaTime)\n"
             "    {\n"
             "    }\n"
             "}\n";
    }

    std::string trim(const std::string &s)
    {
      const auto start = s.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
      {
        return {};
      }
      const auto end = s.find_last_not_of(" \t\r\n");
      return s.substr(start, end - start + 1);
    }

    std::vector<std::pair<std::string, std::string>> parse_public_fields(
        const std::filesystem::path &scriptPath)
    {
      std::vector<std::pair<std::string, std::string>> fields;

      std::ifstream file(scriptPath);
      if (!file.is_open())
      {
        return fields;
      }

      bool insideTargetClass = false;
      int braceDepth = 0;
      int classBraceDepth = -1;

      std::string line;
      while (std::getline(file, line))
      {
        const std::string trimmed = trim(line);

        // Look for a class that extends HadesScript.
        if (!insideTargetClass)
        {
          if (trimmed.find("HadesScript") != std::string::npos &&
              trimmed.find("class ") != std::string::npos)
          {
            insideTargetClass = true;
            // Count any opening braces on this line.
            for (char ch : trimmed)
            {
              if (ch == '{')
              {
                if (classBraceDepth < 0)
                {
                  classBraceDepth = braceDepth;
                }
                ++braceDepth;
              }
              else if (ch == '}')
              {
                --braceDepth;
              }
            }
            continue;
          }
        }

        // Track braces.
        for (char ch : trimmed)
        {
          if (ch == '{')
          {
            if (insideTargetClass && classBraceDepth < 0)
            {
              classBraceDepth = braceDepth;
            }
            ++braceDepth;
          }
          else if (ch == '}')
          {
            --braceDepth;
            if (insideTargetClass && braceDepth <= classBraceDepth)
            {
              // Exited the class body.
              insideTargetClass = false;
              classBraceDepth = -1;
            }
          }
        }

        if (!insideTargetClass || classBraceDepth < 0)
        {
          continue;
        }

        // Only look at direct class-body members (one brace level inside the class).
        if (braceDepth != classBraceDepth + 1)
        {
          continue;
        }

        // Skip lines that are methods, overrides, static, or don't start with public.
        if (trimmed.find("public") != 0)
        {
          continue;
        }
        if (trimmed.find("override") != std::string::npos ||
            trimmed.find("virtual") != std::string::npos ||
            trimmed.find("static") != std::string::npos ||
            trimmed.find("(") != std::string::npos ||
            trimmed.find("void") != std::string::npos ||
            trimmed.find("class ") != std::string::npos)
        {
          continue;
        }

        // Parse: public <type> <name> [= ...] ;
        std::istringstream iss(trimmed);
        std::string keyword;
        std::string type;
        std::string name;
        if (!(iss >> keyword >> type >> name))
        {
          continue;
        }

        // Remove trailing ; or = from name.
        while (!name.empty() && (name.back() == ';' || name.back() == '='))
        {
          name.pop_back();
        }

        if (!name.empty() && !type.empty())
        {
          fields.emplace_back(type, name);
        }
      }

      return fields;
    }

    bool create_workspace_item(
        const std::filesystem::path &parentPath,
        const std::string &rawName,
        const Editor::WorkspaceCreateKind kind,
        std::string *errorMessage)
    {
      if (rawName.empty())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Enter a name before creating the item.";
        }
        return false;
      }

      if (has_path_separator(rawName))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Names cannot contain path separators.";
        }
        return false;
      }

      std::error_code errorCode;
      if (!std::filesystem::exists(parentPath, errorCode) || !std::filesystem::is_directory(parentPath, errorCode))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "The selected parent folder is no longer available.";
        }
        return false;
      }

      std::filesystem::path targetPath = parentPath / rawName;
      if (kind == Editor::WorkspaceCreateKind::Script)
      {
        if (!targetPath.has_extension())
        {
          targetPath += ".cs";
        }
        else if (targetPath.extension() != ".cs")
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Scripts must use the .cs extension.";
          }
          return false;
        }
      }

      if (std::filesystem::exists(targetPath, errorCode))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "'" + path_display_name(targetPath) + "' already exists.";
        }
        return false;
      }

      if (kind == Editor::WorkspaceCreateKind::Folder)
      {
        if (!std::filesystem::create_directory(targetPath, errorCode))
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Unable to create folder '" + targetPath.string() + "': " + errorCode.message();
          }
          return false;
        }
        return true;
      }

      std::ofstream output(targetPath);
      if (!output)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Unable to create script '" + targetPath.string() + "'.";
        }
        return false;
      }

      output << build_script_template(csharp_class_name_from_stem(targetPath.stem().string()));
      if (!output.good())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Unable to write script '" + targetPath.string() + "'.";
        }
        return false;
      }

      return true;
    }

    bool build_workspace_tree(
        const std::filesystem::path &path,
        const std::filesystem::path &workspacePath,
        Editor::WorkspaceTreeNode &node,
        std::vector<std::string> &scriptFiles,
        std::string *errorMessage)
    {
      node.path = path;
      node.directory = std::filesystem::is_directory(path);
      node.children.clear();

      if (!node.directory)
      {
        if (path.extension() == ".cs")
        {
          scriptFiles.push_back(relative_workspace_path(workspacePath, path));
        }
        return true;
      }

      std::error_code errorCode;
      std::vector<std::filesystem::directory_entry> entries;
      for (std::filesystem::directory_iterator iterator(path, errorCode); !errorCode && iterator != std::filesystem::directory_iterator(); iterator.increment(errorCode))
      {
        entries.push_back(*iterator);
      }

      if (errorCode)
      {
        if (errorMessage != nullptr && errorMessage->empty())
        {
          *errorMessage = "Unable to inspect workspace folder '" + path.string() + "': " + errorCode.message();
        }
        return false;
      }

      std::sort(
          entries.begin(),
          entries.end(),
          [](const std::filesystem::directory_entry &lhs, const std::filesystem::directory_entry &rhs)
          {
            std::error_code lhsError;
            std::error_code rhsError;
            const bool lhsDirectory = lhs.is_directory(lhsError);
            const bool rhsDirectory = rhs.is_directory(rhsError);
            if (lhsDirectory != rhsDirectory)
            {
              return lhsDirectory > rhsDirectory;
            }

            return lhs.path().filename().string() < rhs.path().filename().string();
          });

      for (const auto &entry : entries)
      {
        Editor::WorkspaceTreeNode child;
        build_workspace_tree(entry.path(), workspacePath, child, scriptFiles, errorMessage);
        node.children.push_back(std::move(child));
      }

      return true;
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

  void Editor::render(
      float deltaTime,
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    configure_default_dock_layout(gui->render_frame());
    refresh_workspace_cache(deltaTime, workspacePath);

    // File watch: detect script changes and trigger background compile.
    if (!activeWorkspacePath_.empty() && !workspaceScriptFiles_.empty())
    {
      bool scriptsChanged = false;
      for (const auto &relPath : workspaceScriptFiles_)
      {
        const auto fullPath = activeWorkspacePath_ / relPath;
        std::error_code ec;
        const auto modTime = std::filesystem::last_write_time(fullPath, ec);
        if (ec)
        {
          continue;
        }
        auto it = scriptModTimes_.find(relPath);
        if (it == scriptModTimes_.end())
        {
          scriptModTimes_[relPath] = modTime;
          scriptsChanged = true;
        }
        else if (it->second != modTime)
        {
          it->second = modTime;
          scriptsChanged = true;
        }
      }

      if (scriptsChanged && !backgroundCompileInProgress_)
      {
        std::vector<std::filesystem::path> sourceFiles;
        sourceFiles.reserve(workspaceScriptFiles_.size());
        for (const auto &relPath : workspaceScriptFiles_)
        {
          sourceFiles.push_back(activeWorkspacePath_ / relPath);
        }

        backgroundCompileInProgress_ = true;
        backgroundCompileResult_ = std::async(std::launch::async,
            [files = std::move(sourceFiles)]() -> std::string
            {
              std::string error;
              ScriptRuntime::compile(files, &error);
              return error;
            });
      }

      if (backgroundCompileInProgress_ && backgroundCompileResult_.valid() &&
          backgroundCompileResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      {
        lastCompileError_ = backgroundCompileResult_.get();
        lastCompileSucceeded_ = lastCompileError_.empty();
        backgroundCompileInProgress_ = false;
      }
    }

    handle_entity_creation_requests(entityManager, componentManager);
    import_model(entityManager, componentManager);
    handle_play_mode_requests(entityManager, componentManager, scriptRuntime);
    workspace();
    entities(entityManager, componentManager);
    properties(entityManager, componentManager);
    components(entityManager, componentManager);
    game(entityManager, componentManager, scriptRuntime);
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
    ImGuiID workspaceDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.22f, nullptr, &mainDockId);
    const ImGuiID entitiesDockId = ImGui::DockBuilderSplitNode(workspaceDockId, ImGuiDir_Down, 0.56f, nullptr, &workspaceDockId);
    ImGuiID inspectorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.34f, nullptr, &mainDockId);
    const ImGuiID componentsDockId = ImGui::DockBuilderSplitNode(inspectorDockId, ImGuiDir_Right, 0.45f, nullptr, &inspectorDockId);

    ImGui::DockBuilderDockWindow(WORKSPACE_WINDOW_TITLE, workspaceDockId);
    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(COMPONENTS_WINDOW_TITLE, componentsDockId);
    ImGui::DockBuilderDockWindow(GAME_WINDOW_TITLE, mainDockId);
    ImGui::DockBuilderFinish(dockspaceId);
  }

  void Editor::refresh_workspace_cache(float deltaTime, const std::filesystem::path &workspacePath)
  {
    const double now = ImGui::GetTime();
    if (workspacePath != activeWorkspacePath_)
    {
      activeWorkspacePath_ = workspacePath;
      workspaceTreeRoot_.reset();
      workspaceScriptFiles_.clear();
      workspaceScanError_.clear();
      nextWorkspaceScanTime_ = 0.0;
    }

    if (activeWorkspacePath_.empty() || (workspaceTreeRoot_.has_value() && now < nextWorkspaceScanTime_))
    {
      return;
    }

    WorkspaceTreeNode rootNode;
    std::vector<std::string> scriptFiles;
    std::string scanError;
    build_workspace_tree(activeWorkspacePath_, activeWorkspacePath_, rootNode, scriptFiles, &scanError);
    std::sort(scriptFiles.begin(), scriptFiles.end());
    workspaceTreeRoot_ = std::move(rootNode);
    workspaceScriptFiles_ = std::move(scriptFiles);
    workspaceScanError_ = std::move(scanError);
    nextWorkspaceScanTime_ = now + std::max(static_cast<double>(deltaTime), 1.0);
  }

  void Editor::invalidate_workspace_cache()
  {
    workspaceTreeRoot_.reset();
    workspaceScriptFiles_.clear();
    workspaceScanError_.clear();
    nextWorkspaceScanTime_ = 0.0;
  }

  void Editor::request_workspace_item_creation(WorkspaceCreateKind kind, const std::filesystem::path &parentPath)
  {
    pendingWorkspaceCreateKind_ = kind;
    pendingWorkspaceCreateParentPath_ = parentPath;
    workspaceCreateNameBuffer_.fill('\0');
    workspaceCreateError_.clear();
    openWorkspaceCreateDialog_ = true;
  }

  void Editor::render_workspace_create_dialog()
  {
    if (openWorkspaceCreateDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_CREATE_POPUP_TITLE);
      openWorkspaceCreateDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_CREATE_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    const bool creatingScript = pendingWorkspaceCreateKind_ == WorkspaceCreateKind::Script;
    ImGui::TextWrapped(
        "%s in:",
        creatingScript ? "Create a new C# script" : "Create a new folder");
    ImGui::TextWrapped("%s", pendingWorkspaceCreateParentPath_.string().c_str());
    ImGui::InputText(creatingScript ? "Script Name" : "Folder Name", workspaceCreateNameBuffer_.data(), workspaceCreateNameBuffer_.size());

    if (creatingScript)
    {
      ImGui::TextDisabled("The .cs extension will be added automatically when needed.");
    }

    if (!workspaceCreateError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceCreateError_.c_str());
    }

    if (ImGui::Button(creatingScript ? "Create Script" : "Create Folder"))
    {
      std::string errorMessage;
      if (create_workspace_item(
              pendingWorkspaceCreateParentPath_,
              std::string(workspaceCreateNameBuffer_.data()),
              pendingWorkspaceCreateKind_,
              &errorMessage))
      {
        workspaceCreateError_.clear();
        pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
        pendingWorkspaceCreateParentPath_.clear();
        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceCreateError_ = std::move(errorMessage);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceCreateError_.clear();
      pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
      pendingWorkspaceCreateParentPath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::render_workspace_tree_node(const WorkspaceTreeNode &node)
  {
    const std::string label = path_display_name(node.path);
    if (!node.directory)
    {
      ImGui::BulletText("%s", label.c_str());
      return;
    }

    const std::string treeNodeId = label + "##" + node.path.string();
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                     (node.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
    const bool open = ImGui::TreeNodeEx(treeNodeId.c_str(), flags);
    if (ImGui::BeginPopupContextItem())
    {
      if (ImGui::MenuItem("New Folder"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Folder, node.path);
      }
      if (ImGui::MenuItem("New Script"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Script, node.path);
      }
      ImGui::EndPopup();
    }

    if (!open)
    {
      return;
    }

    for (const auto &child : node.children)
    {
      render_workspace_tree_node(child);
    }

    ImGui::TreePop();
  }

  void Editor::workspace()
  {
    ImGui::Begin(WORKSPACE_WINDOW_TITLE);
    render_workspace_create_dialog();

    if (activeWorkspacePath_.empty())
    {
      render_selection_hint("Open a workspace to browse its files.");
      ImGui::End();
      return;
    }

    ImGui::TextWrapped("%s", activeWorkspacePath_.string().c_str());
    if (ImGui::Button("New Folder"))
    {
      request_workspace_item_creation(WorkspaceCreateKind::Folder, activeWorkspacePath_);
    }

    ImGui::SameLine();
    if (ImGui::Button("New Script"))
    {
      request_workspace_item_creation(WorkspaceCreateKind::Script, activeWorkspacePath_);
    }

    ImGui::Separator();

    if (!workspaceScanError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceScanError_.c_str());
      ImGui::Separator();
    }

    if (!workspaceTreeRoot_.has_value())
    {
      render_selection_hint("Workspace files are not available yet.");
      ImGui::End();
      return;
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    render_workspace_tree_node(*workspaceTreeRoot_);
    if (ImGui::BeginPopupContextWindow("WorkspaceRootContext", ImGuiPopupFlags_NoOpenOverItems))
    {
      if (ImGui::MenuItem("New Folder"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Folder, activeWorkspacePath_);
      }
      if (ImGui::MenuItem("New Script"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Script, activeWorkspacePath_);
      }
      ImGui::EndPopup();
    }
    ImGui::End();
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
    const auto selection = select_main_camera(entityManager, componentManager);
    if (selection.status != MainCameraSelectionStatus::Ready || !selection.entity.has_value())
    {
      state.isPlaying = false;
      state.activeCamera.reset();
      state.playModeMessage = main_camera_selection_message(selection.status);
      return;
    }

    std::string scriptError;
    if (!scriptRuntime.start(componentManager, entityManager, activeWorkspacePath_, &scriptError))
    {
      state.isPlaying = false;
      state.activeCamera.reset();
      state.playModeMessage = scriptError;
      return;
    }

    state.isPlaying = true;
    state.activeCamera = selection.entity;
    state.playModeMessage.clear();
  }

  void Editor::stop_play_mode(ScriptRuntime &scriptRuntime)
  {
    scriptRuntime.stop();
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
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      ImGui::TextDisabled("%s", componentManager.getComponent<NameComponent>(entity).value.c_str());
    }
    ImGui::Separator();

    std::size_t componentTypeCount = 0;
    componentTypeCount += componentManager.hasComponent<NameComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<TransformHierarchyComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<PositionComponent3D>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<CameraComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<AudioListenerComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<PrimitiveComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<AudioSourceComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<ModelComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<RenderComponent>(entity) ? 1U : 0U;
    componentTypeCount += componentManager.hasComponent<ScriptComponent>(entity) ? 1U : 0U;

    ImGui::Text("Component Types: %zu", componentTypeCount);
    if (componentManager.hasComponent<ScriptComponent>(entity))
    {
      const auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
      ImGui::Text("Script Components: %zu", scriptComponent.attachments.size());
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

    ImGui::Separator();
    ImGui::TextDisabled("Expand attached components in the Components panel to edit their details.");

    ImGui::End();
  }

  void Editor::components(EntityManager &entityManager, ComponentManager &componentManager)
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
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      ImGui::TextDisabled("%s", componentManager.getComponent<NameComponent>(entity).value.c_str());
    }
    ImGui::Separator();

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

    ImGui::SameLine();
    ImGui::TextDisabled("Expand a component to inspect or edit it.");
    ImGui::Separator();

    if (componentManager.hasComponent<NameComponent>(entity) && ImGui::CollapsingHeader("Name"))
    {
      auto &name = componentManager.getComponent<NameComponent>(entity);
      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", name.value.c_str());
      if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
      {
        name.value = nameBuffer.data();
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
          std::array<char, 160> classBuffer{};
          std::snprintf(classBuffer.data(), classBuffer.size(), "%s", attachment.className.c_str());

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
            const std::string previousStem = std::filesystem::path(previousScriptPath).stem().string();
            if (attachment.className.empty() || attachment.className == previousStem)
            {
              attachment.className = std::filesystem::path(attachment.scriptPath).stem().string();
            }
          }

          if (scriptOptions.empty())
          {
            ImGui::TextDisabled("No .cs scripts were found in the workspace.");
          }
          else if (!attachment.scriptPath.empty())
          {
            ImGui::TextDisabled("Workspace path: %s", attachment.scriptPath.c_str());
          }

          if (ImGui::InputText("Class Name", classBuffer.data(), classBuffer.size()))
          {
            attachment.className = classBuffer.data();
          }

          if (ImGui::Button("Use File Name"))
          {
            attachment.className = std::filesystem::path(attachment.scriptPath).stem().string();
          }
          ImGui::SameLine();
          if (ImGui::Button("Remove Script Component"))
          {
            removeAttachmentIndex = index;
          }

          // Display public fields parsed from the script file.
          if (!attachment.scriptPath.empty() && !activeWorkspacePath_.empty())
          {
            const auto resolvedPath = activeWorkspacePath_ / attachment.scriptPath;
            const std::string pathKey = resolvedPath.string();

            // Check if we need to re-parse (file changed or not yet cached).
            std::error_code modEc;
            const auto modTime = std::filesystem::last_write_time(resolvedPath, modEc);
            bool needsParse = false;
            if (!modEc)
            {
              auto modIt = parsedFieldsModTimes_.find(pathKey);
              if (modIt == parsedFieldsModTimes_.end() || modIt->second != modTime)
              {
                needsParse = true;
                parsedFieldsModTimes_[pathKey] = modTime;
              }
            }

            if (needsParse)
            {
              parsedFieldsCache_[pathKey] = parse_public_fields(resolvedPath);
            }

            auto cacheIt = parsedFieldsCache_.find(pathKey);
            if (cacheIt != parsedFieldsCache_.end() && !cacheIt->second.empty())
            {
              const auto &fields = cacheIt->second;

              // Remove stale entries from publicFieldValues.
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
        }

        ImGui::PopID();
      }

      if (removeAttachmentIndex.has_value())
      {
        scriptComponent.attachments.erase(scriptComponent.attachments.begin() + static_cast<std::ptrdiff_t>(*removeAttachmentIndex));
      }

      ImGui::TextDisabled("Scripts compile when Play starts using dotnet and must derive from Hades.Scripting.HadesScript.");
      ImGui::TextDisabled("Relative script paths resolve from the active workspace folder.");

      if (backgroundCompileInProgress_)
      {
        ImGui::TextDisabled("Compiling scripts...");
      }
      else if (!lastCompileSucceeded_)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Compile error: %s", lastCompileError_.c_str());
        ImGui::PopStyleColor();
      }
      else if (!scriptComponent.attachments.empty())
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextDisabled("Scripts compiled successfully.");
        ImGui::PopStyleColor();
      }
    }

    ImGui::End();
  }

  void Editor::game(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    ImGui::Begin(GAME_WINDOW_TITLE);

    if (ImGui::Button(state.isPlaying ? "Stop" : "Play"))
    {
      if (state.isPlaying)
      {
        stop_play_mode(scriptRuntime);
      }
      else
      {
        start_play_mode(entityManager, componentManager, scriptRuntime);
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
