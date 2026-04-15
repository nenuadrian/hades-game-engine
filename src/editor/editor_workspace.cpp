#include "editor.hpp"

#include "workspace_file_operations.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"

namespace hades
{
  namespace
  {
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";

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

    bool is_ignored_script_scan_component(std::string_view component)
    {
      if (component.empty())
      {
        return false;
      }

      if (component == ".git" ||
          component == ".hades" ||
          component == ".vs" ||
          component == ".idea" ||
          component == ".vscode" ||
          component == "bin" ||
          component == "obj" ||
          component == "out" ||
          component == "_deps" ||
          component == "node_modules")
      {
        return true;
      }

      return component == "build" ||
             component.rfind("build-", 0) == 0 ||
             component.rfind("cmake-build-", 0) == 0;
    }

    bool should_include_workspace_script_file(
        const std::filesystem::path &workspacePath,
        const std::filesystem::path &path)
    {
      if (path.extension() != ".cpp")
      {
        return false;
      }

      const std::filesystem::path relativePath = path.lexically_relative(workspacePath);
      if (relativePath.empty())
      {
        return true;
      }

      for (const auto &component : relativePath)
      {
        const std::string componentName = component.string();
        if (is_ignored_script_scan_component(componentName))
        {
          return false;
        }
      }

      return true;
    }

    template <std::size_t Size>
    void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
    {
      buffer.fill('\0');
      const std::size_t copyLength = std::min(value.size(), Size - 1);
      std::copy_n(value.data(), copyLength, buffer.data());
      buffer[copyLength] = '\0';
    }

    std::string to_lower(const std::string &str)
    {
      std::string result = str;
      for (auto &c : result)
      {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return result;
    }

    struct FileTypeVisual
    {
      const char *icon;
      ImU32 iconColor;
    };

    FileTypeVisual get_file_type_visual(const std::filesystem::path &path, bool isDirectory, bool isOpen)
    {
      if (isDirectory)
      {
        return {isOpen ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER, IM_COL32(210, 180, 100, 255)};
      }

      const std::string ext = to_lower(path.extension().string());

      if (ext == ".cpp" || ext == ".hpp" || ext == ".h")
        return {ICON_FA_FILE_CODE, IM_COL32(130, 150, 210, 255)};
      if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        return {ICON_FA_IMAGE, IM_COL32(130, 190, 130, 255)};
      if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
        return {ICON_FA_CUBE, IM_COL32(210, 160, 100, 255)};
      if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
        return {ICON_FA_VOLUME_HIGH, IM_COL32(210, 200, 120, 255)};
      if (ext == ".glsl" || ext == ".hlsl" || ext == ".vert" || ext == ".frag" || ext == ".shader")
        return {ICON_FA_MICROCHIP, IM_COL32(120, 190, 200, 255)};
      if (ext == ".json" || ext == ".txt" || ext == ".xml" || ext == ".yaml" || ext == ".yml" || ext == ".cfg" || ext == ".ini")
        return {ICON_FA_FILE_LINES, IM_COL32(161, 151, 146, 255)};

      return {ICON_FA_FILE, IM_COL32(161, 151, 146, 255)};
    }

    const Editor::WorkspaceTreeNode *find_tree_node(
        const Editor::WorkspaceTreeNode &root,
        const std::filesystem::path &targetPath)
    {
      if (root.path.lexically_normal() == targetPath.lexically_normal())
      {
        return &root;
      }
      for (const auto &child : root.children)
      {
        const Editor::WorkspaceTreeNode *found = find_tree_node(child, targetPath);
        if (found != nullptr)
        {
          return found;
        }
      }
      return nullptr;
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
        if (should_include_workspace_script_file(workspacePath, path))
        {
          scriptFiles.push_back(relative_workspace_path(workspacePath, path));
        }
        return true;
      }

      std::error_code errorCode;
      std::vector<std::filesystem::directory_entry> entries;
      for (std::filesystem::directory_iterator iterator(path, errorCode); !errorCode && iterator != std::filesystem::directory_iterator(); iterator.increment(errorCode))
      {
        if (iterator->path().filename() == ".hades")
        {
          continue;
        }
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

  void Editor::refresh_workspace_cache(const std::filesystem::path &workspacePath)
  {
    if (workspacePath != activeWorkspacePath_)
    {
      activeWorkspacePath_ = workspacePath;
      pendingSavedWorldRestore_ = !activeWorkspacePath_.empty();
      workspaceGridCurrentDir_.clear();
      workspaceTreeRoot_.reset();
      workspaceScriptFiles_.clear();
      workspaceScanError_.clear();
      workspaceScriptListDirty_ = false;
      parsedScriptCache_.clear();
      parsedScriptModTimes_.clear();
      cachedDiskWorlds_.clear();
    }

    if (activeWorkspacePath_.empty() || workspaceTreeRoot_.has_value())
    {
      return;
    }

    WorkspaceTreeNode rootNode;
    std::vector<std::string> scriptFiles;
    std::string scanError;
    build_workspace_tree(activeWorkspacePath_, activeWorkspacePath_, rootNode, scriptFiles, &scanError);
    std::sort(scriptFiles.begin(), scriptFiles.end());
    workspaceScriptFiles_ = std::move(scriptFiles);
    workspaceTreeRoot_ = std::move(rootNode);
    workspaceScanError_ = std::move(scanError);
    cachedDiskWorlds_ = list_saved_worlds(activeWorkspacePath_);
  }

  void Editor::invalidate_workspace_cache()
  {
    workspaceTreeRoot_.reset();
    workspaceScanError_.clear();
  }

  void Editor::request_workspace_item_creation(WorkspaceCreateKind kind, const std::filesystem::path &parentPath)
  {
    pendingWorkspaceCreateKind_ = kind;
    pendingWorkspaceCreateParentPath_ = parentPath;
    workspaceCreateNameBuffer_.fill('\0');
    workspaceCreateError_.clear();
    openWorkspaceCreateDialog_ = true;
  }

  void Editor::request_workspace_item_import(const std::filesystem::path &destinationDirectory)
  {
    pendingWorkspaceImportParentPath_ = destinationDirectory;
    workspaceImportSourcePathBuffer_.fill('\0');
    workspaceImportError_.clear();
    openWorkspaceImportDialog_ = true;
  }

  void Editor::request_workspace_item_deletion(const std::filesystem::path &targetPath)
  {
    pendingWorkspaceDeletePath_ = targetPath;
    workspaceDeleteError_.clear();
    openWorkspaceDeleteDialog_ = true;
  }

  void Editor::render_workspace_create_menu(const std::filesystem::path &destination)
  {
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  New Folder"))
    {
      request_workspace_item_creation(WorkspaceCreateKind::Folder, destination);
    }
    if (ImGui::MenuItem(ICON_FA_FILE_CODE "  New Script"))
    {
      request_workspace_item_creation(WorkspaceCreateKind::Script, destination);
    }
    if (ImGui::MenuItem(ICON_FA_FILE_IMPORT "  Import"))
    {
      request_workspace_item_import(destination);
    }
  }

  void Editor::render_workspace_grid_cell(const WorkspaceTreeNode &node)
  {
    const std::string label = path_display_name(node.path);
    const bool isRenaming = !workspaceRenamePath_.empty() &&
                            workspaceRenamePath_.lexically_normal() == node.path.lexically_normal();

    const FileTypeVisual fileVisual = get_file_type_visual(node.path, node.directory, false);

    const float cellWidth = ImGui::GetColumnWidth();
    const float textLineHeight = ImGui::GetTextLineHeight();
    const float iconScale = 2.5f;
    const float iconHeight = textLineHeight * iconScale;
    const float cellPaddingY = 6.0f;
    const float cellHeight = cellPaddingY + iconHeight + 4.0f + textLineHeight + cellPaddingY;

    const ImVec2 cellStart = ImGui::GetCursorPos();
    const std::string cellId = "##wscell_" + node.path.string();

    ImGui::PushID(cellId.c_str());

    if (ImGui::Selectable("##sel", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellWidth, cellHeight)))
    {
      if (!isRenaming)
      {
        if (node.directory)
        {
          workspaceGridCurrentDir_ = node.path;
        }
        else if (node.path.extension() == ".cpp")
        {
          open_in_external_editor(externalEditor_, activeWorkspacePath_, node.path);
        }
      }
    }

    const bool cellHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      const std::string pathStr = node.path.string();
      ImGui::SetDragDropPayload("WORKSPACE_PATH", pathStr.c_str(), pathStr.size() + 1);
      ImGui::TextUnformatted(label.c_str());
      ImGui::EndDragDropSource();
    }

    if (node.directory && ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("WORKSPACE_PATH"))
      {
        const char *sourcePath = static_cast<const char *>(payload->Data);
        const std::filesystem::path source(sourcePath);
        const std::filesystem::path destination = node.path / source.filename();
        std::error_code errorCode;
        if (source != destination)
        {
          std::filesystem::rename(source, destination, errorCode);
          if (!errorCode)
          {
            invalidate_workspace_cache();
          }
        }
      }
      ImGui::EndDragDropTarget();
    }

    const std::string contextId = "wsctx_" + node.path.string();
    if (cellHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup(contextId.c_str());
    }
    if (ImGui::BeginPopup(contextId.c_str()))
    {
      if (node.directory)
      {
        render_workspace_create_menu(node.path);
        ImGui::Separator();
      }
      if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
      {
        workspaceRenamePath_ = node.path;
        set_buffer_text(workspaceRenameBuffer_, label);
        workspaceRenameFocusPending_ = true;
      }
      if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
      {
        request_workspace_item_deletion(node.path);
      }
      ImGui::EndPopup();
    }

    // Draw icon centered in cell.
    {
      const float origScale = ImGui::GetFont()->Scale;
      ImGui::GetFont()->Scale = iconScale;
      ImGui::PushFont(ImGui::GetFont());

      const ImVec2 iconSize = ImGui::CalcTextSize(fileVisual.icon);
      const float iconX = cellStart.x + (cellWidth - iconSize.x) * 0.5f;
      const float iconY = cellStart.y + cellPaddingY;
      ImGui::SetCursorPos(ImVec2(iconX, iconY));
      ImGui::PushStyleColor(ImGuiCol_Text, fileVisual.iconColor);
      ImGui::TextUnformatted(fileVisual.icon);
      ImGui::PopStyleColor();

      ImGui::GetFont()->Scale = origScale;
      ImGui::PopFont();
    }

    // Draw filename or rename input below icon.
    {
      const float textY = cellStart.y + cellPaddingY + iconHeight + 4.0f;

      if (isRenaming)
      {
        if (workspaceRenameFocusPending_)
        {
          ImGui::SetKeyboardFocusHere();
          workspaceRenameFocusPending_ = false;
        }

        ImGui::SetCursorPos(ImVec2(cellStart.x, textY));
        ImGui::SetNextItemWidth(cellWidth);
        const bool entered = ImGui::InputText(
            "##rename",
            workspaceRenameBuffer_.data(),
            workspaceRenameBuffer_.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool lostFocus = !ImGui::IsItemActive() && !workspaceRenameFocusPending_;
        if (entered || lostFocus)
        {
          const std::string newName(workspaceRenameBuffer_.data());
          if (!newName.empty() && newName != label)
          {
            std::error_code errorCode;
            std::filesystem::rename(node.path, node.path.parent_path() / newName, errorCode);
            if (!errorCode)
            {
              invalidate_workspace_cache();
            }
          }
          workspaceRenamePath_.clear();
        }
      }
      else
      {
        std::string displayName = label;
        if (displayName.size() > 14)
        {
          displayName = displayName.substr(0, 11) + "...";
        }

        const ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
        const float textX = cellStart.x + (cellWidth - textSize.x) * 0.5f;
        ImGui::SetCursorPos(ImVec2(textX, textY));
        ImGui::TextUnformatted(displayName.c_str());

        if (displayName != label && ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("%s", label.c_str());
        }
      }
    }

    // Advance cursor past the cell.
    ImGui::SetCursorPos(ImVec2(cellStart.x, cellStart.y + cellHeight));

    ImGui::PopID();
  }

  void Editor::workspace(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;
    (void)componentManager;

    ImGui::Begin(WORKSPACE_WINDOW_TITLE);

    if (activeWorkspacePath_.empty())
    {
      ImGui::TextDisabled("No workspace.");
      ImGui::End();
      return;
    }

    if (!workspaceScanError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceScanError_.c_str());
      ImGui::Separator();
    }

    if (!workspaceTreeRoot_.has_value())
    {
      ImGui::TextDisabled("No files.");
      ImGui::End();
      return;
    }

    // --- Breadcrumb navigation ---
    {
      if (workspaceGridCurrentDir_.empty())
      {
        workspaceGridCurrentDir_ = activeWorkspacePath_;
      }

      if (ImGui::SmallButton(ICON_FA_HOUSE))
      {
        workspaceGridCurrentDir_ = activeWorkspacePath_;
      }

      const std::filesystem::path relativePath = workspaceGridCurrentDir_.lexically_relative(activeWorkspacePath_);
      if (!relativePath.empty() && relativePath != ".")
      {
        std::filesystem::path accumulated = activeWorkspacePath_;
        for (const auto &segment : relativePath)
        {
          accumulated /= segment;
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
          ImGui::TextUnformatted(ICON_FA_CHEVRON_RIGHT);
          ImGui::PopStyleColor();
          ImGui::SameLine();
          const std::string segStr = segment.string();
          const std::string btnId = segStr + "##bc_" + accumulated.string();
          if (ImGui::SmallButton(btnId.c_str()))
          {
            workspaceGridCurrentDir_ = accumulated;
          }
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
    }

    // --- Resolve current directory node ---
    const WorkspaceTreeNode *currentNode = find_tree_node(*workspaceTreeRoot_, workspaceGridCurrentDir_);
    if (currentNode == nullptr)
    {
      workspaceGridCurrentDir_ = activeWorkspacePath_;
      currentNode = &(*workspaceTreeRoot_);
    }

    // --- Grid view ---
    if (currentNode->children.empty())
    {
      ImGui::TextDisabled("Empty folder.");
    }
    else
    {
      constexpr int columns = 6;
      if (ImGui::BeginTable("WorkspaceGrid", columns, ImGuiTableFlags_SizingStretchSame))
      {
        for (const auto &child : currentNode->children)
        {
          ImGui::TableNextColumn();
          render_workspace_grid_cell(child);
        }
        ImGui::EndTable();
      }
    }

    // Drop target for current directory background.
    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("WORKSPACE_PATH"))
      {
        const char *sourcePath = static_cast<const char *>(payload->Data);
        const std::filesystem::path source(sourcePath);
        const std::filesystem::path destination = currentNode->path / source.filename();
        std::error_code errorCode;
        if (source != destination)
        {
          std::filesystem::rename(source, destination, errorCode);
          if (!errorCode)
          {
            invalidate_workspace_cache();
          }
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextWindow("WorkspaceRootContext", ImGuiPopupFlags_NoOpenOverItems))
    {
      render_workspace_create_menu(currentNode->path);
      ImGui::EndPopup();
    }
    ImGui::End();
  }
}
