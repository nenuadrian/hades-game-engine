#include "editor.hpp"

#include "native_dialogs.hpp"
#include "workspace_file_operations.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"

namespace hades
{
  namespace
  {
    constexpr char WORKSPACE_CREATE_POPUP_TITLE[] = "Create Workspace Item";
    constexpr char WORKSPACE_IMPORT_POPUP_TITLE[] = "Import Into Workspace";
    constexpr char WORKSPACE_DELETE_POPUP_TITLE[] = "Delete Workspace Item";

    std::string path_display_name(const std::filesystem::path &path)
    {
      const std::string filename = path.filename().string();
      return filename.empty() ? path.string() : filename;
    }

    bool has_path_separator(const std::string &name)
    {
      return name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
    }

    template <std::size_t Size>
    void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
    {
      buffer.fill('\0');
      const std::size_t copyLength = std::min(value.size(), Size - 1);
      std::copy_n(value.data(), copyLength, buffer.data());
      buffer[copyLength] = '\0';
    }

    std::string workspace_delete_button_label(const std::filesystem::path &path)
    {
      std::error_code errorCode;
      return std::filesystem::is_directory(path, errorCode) ? "Delete Folder" : "Delete File";
    }

    bool path_is_same_or_within(
        const std::filesystem::path &parentPath,
        const std::filesystem::path &candidatePath)
    {
      const std::filesystem::path normalizedParent = parentPath.lexically_normal();
      const std::filesystem::path normalizedCandidate = candidatePath.lexically_normal();
      if (normalizedParent == normalizedCandidate)
      {
        return true;
      }

      const std::filesystem::path relativePath = normalizedCandidate.lexically_relative(normalizedParent);
      if (relativePath.empty())
      {
        return false;
      }

      for (const auto &segment : relativePath)
      {
        if (segment == "..")
        {
          return false;
        }
      }

      return true;
    }

    std::string class_name_from_stem(const std::string &stem)
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
      return "#include \"engine/hades.hpp\"\n"
             "\n"
             "class " +
             className +
             " : public hades::HadesScript\n"
             "{\n"
             "public:\n"
             "  void onStart(hades::ScriptContext &ctx) override\n"
             "  {\n"
             "  }\n"
             "\n"
             "  void onUpdate(hades::ScriptContext &ctx, float deltaTime) override\n"
             "  {\n"
             "  }\n"
             "};\n"
             "\n"
             "HADES_REGISTER_SCRIPT(" +
             className + ")\n";
    }

    std::string trim(const std::string &value)
    {
      const auto start = value.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
      {
        return {};
      }

      const auto end = value.find_last_not_of(" \t\r\n");
      return value.substr(start, end - start + 1);
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
          targetPath += ".cpp";
        }
        else if (targetPath.extension() != ".cpp")
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Scripts must use the .cpp extension.";
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

      output << build_script_template(class_name_from_stem(targetPath.stem().string()));
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

  }

  void Editor::render_workspace_dialogs(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    render_workspace_create_dialog();
    render_workspace_import_dialog();
    render_workspace_delete_dialog(entityManager, componentManager);
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
    ImGui::TextWrapped("%s", creatingScript ? "New Script" : "New Folder");
    ImGui::TextWrapped("%s", pendingWorkspaceCreateParentPath_.string().c_str());
    ImGui::InputText(creatingScript ? "Script Name" : "Folder Name", workspaceCreateNameBuffer_.data(), workspaceCreateNameBuffer_.size());

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

  void Editor::render_workspace_import_dialog()
  {
    if (openWorkspaceImportDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_IMPORT_POPUP_TITLE);
      openWorkspaceImportDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_IMPORT_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    ImGui::TextWrapped("Import File");
    ImGui::TextWrapped("%s", pendingWorkspaceImportParentPath_.string().c_str());
    ImGui::InputText("Source File", workspaceImportSourcePathBuffer_.data(), workspaceImportSourcePathBuffer_.size());

    if (ImGui::Button("Browse File..."))
    {
      std::string pickerError;
      const auto pickedFile = hades::pick_file_with_native_dialog("Select a file to import", &pickerError);
      if (pickedFile.has_value())
      {
        set_buffer_text(workspaceImportSourcePathBuffer_, pickedFile->string());
        workspaceImportError_.clear();
      }
      else if (!pickerError.empty())
      {
        workspaceImportError_ = std::move(pickerError);
      }
    }

    if (!workspaceImportError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceImportError_.c_str());
    }

    const bool canImportFile = !trim(workspaceImportSourcePathBuffer_.data()).empty();
    if (!canImportFile)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Import"))
    {
      std::string errorMessage;
      if (copy_file_to_directory(
              std::filesystem::path(workspaceImportSourcePathBuffer_.data()),
              pendingWorkspaceImportParentPath_,
              nullptr,
              &errorMessage))
      {
        workspaceImportError_.clear();
        pendingWorkspaceImportParentPath_.clear();
        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceImportError_ = std::move(errorMessage);
      }
    }
    if (!canImportFile)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceImportError_.clear();
      pendingWorkspaceImportParentPath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::render_workspace_delete_dialog(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (openWorkspaceDeleteDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_DELETE_POPUP_TITLE);
      openWorkspaceDeleteDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_DELETE_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    std::error_code errorCode;
    const bool deletingDirectory = std::filesystem::is_directory(pendingWorkspaceDeletePath_, errorCode);

    ImGui::TextWrapped("%s", deletingDirectory ? "Delete Folder" : "Delete File");
    ImGui::TextWrapped("%s", pendingWorkspaceDeletePath_.string().c_str());

    if (!workspaceDeleteError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceDeleteError_.c_str());
    }

    if (ImGui::Button(workspace_delete_button_label(pendingWorkspaceDeletePath_).c_str()))
    {
      WorkspaceDeleteResult deleteResult;
      std::string errorMessage;
      if (delete_workspace_item(
              activeWorkspacePath_,
              pendingWorkspaceDeletePath_,
              entityManager,
              componentManager,
              &deleteResult,
              &errorMessage))
      {
        for (const auto &relativeScriptPath : deleteResult.removedScriptPaths)
        {
          const std::string pathKey = (activeWorkspacePath_ / relativeScriptPath).string();
          parsedScriptCache_.erase(pathKey);
          parsedScriptModTimes_.erase(pathKey);
        }

        workspaceDeleteError_.clear();
        pendingWorkspaceDeletePath_.clear();

        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceDeleteError_ = std::move(errorMessage);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceDeleteError_.clear();
      pendingWorkspaceDeletePath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

}
