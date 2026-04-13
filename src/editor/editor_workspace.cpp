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
      if (path.extension() != ".cs")
      {
        return false;
      }

      if (path.filename() == "AssemblyInfo.cs")
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

      if (ext == ".cs")
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

    bool subtree_matches_filter(const Editor::WorkspaceTreeNode &node, const char *filter)
    {
      if (filter[0] == '\0')
      {
        return true;
      }

      const std::string lowerFilter = to_lower(filter);
      const std::string lowerName = to_lower(path_display_name(node.path));
      if (lowerName.find(lowerFilter) != std::string::npos)
      {
        return true;
      }

      for (const auto &child : node.children)
      {
        if (subtree_matches_filter(child, filter))
        {
          return true;
        }
      }

      return false;
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

  void Editor::render_workspace_tree_node(const WorkspaceTreeNode &node, int depth, int &rowIndex, const char *filter)
  {
    const std::string label = path_display_name(node.path);
    const bool isLeaf = !node.directory || node.children.empty();
    const bool isRenaming = !workspaceRenamePath_.empty() &&
                            workspaceRenamePath_.lexically_normal() == node.path.lexically_normal();

    if (filter[0] != '\0' && !subtree_matches_filter(node, filter))
    {
      return;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const bool isAlternateRow = (rowIndex % 2 != 0);
    if (isAlternateRow)
    {
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float availWidth = ImGui::GetContentRegionAvail().x;
      const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2;
      drawList->AddRectFilled(
          ImVec2(pos.x, pos.y),
          ImVec2(pos.x + availWidth, pos.y + rowHeight),
          IM_COL32(50, 50, 50, 60));
    }
    ++rowIndex;

    const FileTypeVisual fileVisual = get_file_type_visual(node.path, node.directory, false);
    ImGui::PushStyleColor(ImGuiCol_Text, fileVisual.iconColor);
    const bool nodeOpen = ImGui::TreeNodeEx(("##ws" + node.path.string()).c_str(), flags);
    ImGui::PopStyleColor();

    const bool treeNodeHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_None);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, fileVisual.iconColor);
    ImGui::TextUnformatted(fileVisual.icon);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (isRenaming)
    {
      if (workspaceRenameFocusPending_)
      {
        ImGui::SetKeyboardFocusHere();
        workspaceRenameFocusPending_ = false;
      }

      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
      ImGui::TextUnformatted(label.c_str());
    }

    if (!isRenaming && treeNodeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      if (!node.directory && node.path.extension() == ".cs")
      {
        open_in_external_editor(externalEditor_, activeWorkspacePath_, node.path);
      }
    }

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
    if (ImGui::BeginPopupContextItem(contextId.c_str()))
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

    if (nodeOpen && !isLeaf)
    {
      for (const auto &child : node.children)
      {
        render_workspace_tree_node(child, depth + 1, rowIndex, filter);
      }
      ImGui::TreePop();
    }
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

    // --- Filter bar ---
    {
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##wsfilter", ICON_FA_MAGNIFYING_GLASS "  Filter...",
                               workspaceFilterBuffer_.data(), workspaceFilterBuffer_.size());
      ImGui::Spacing();
    }

    // --- File tree ---
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    int workspaceRowIndex = 0;
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    render_workspace_tree_node(*workspaceTreeRoot_, 0, workspaceRowIndex, workspaceFilterBuffer_.data());
    ImGui::PopStyleVar(2);

    if (ImGui::BeginPopupContextWindow("WorkspaceRootContext", ImGuiPopupFlags_NoOpenOverItems))
    {
      render_workspace_create_menu(activeWorkspacePath_);
      ImGui::EndPopup();
    }
    ImGui::End();
  }
}
