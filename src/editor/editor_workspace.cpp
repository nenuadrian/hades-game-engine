#include "editor.hpp"

#include "native_dialogs.hpp"
#include "script_document.hpp"
#include "workspace_file_operations.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <utility>

#include "imgui.h"
#include "TextEditor.h"
#include "../engine/components/script_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";
    constexpr char SCRIPT_EDITOR_WINDOW_TITLE[] = "Script Editor";
    constexpr char WORKSPACE_CREATE_POPUP_TITLE[] = "Create Workspace Item";
    constexpr char WORKSPACE_IMPORT_POPUP_TITLE[] = "Import Into Workspace";
    constexpr char WORKSPACE_DELETE_POPUP_TITLE[] = "Delete Workspace Item";
    constexpr char SCRIPT_EDITOR_UNSAVED_POPUP_TITLE[] = "Unsaved Script Changes";

    TextEditor::LanguageDefinition create_csharp_language_definition()
    {
      TextEditor::LanguageDefinition langDef;

      static const char *const keywords[] = {
          "abstract", "as", "base", "bool", "break", "byte", "case", "catch",
          "char", "checked", "class", "const", "continue", "decimal", "default",
          "delegate", "do", "double", "else", "enum", "event", "explicit",
          "extern", "false", "finally", "fixed", "float", "for", "foreach",
          "goto", "if", "implicit", "in", "int", "interface", "internal",
          "is", "lock", "long", "namespace", "new", "null", "object",
          "operator", "out", "override", "params", "private", "protected",
          "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
          "sizeof", "stackalloc", "static", "string", "struct", "switch",
          "this", "throw", "true", "try", "typeof", "uint", "ulong",
          "unchecked", "unsafe", "ushort", "using", "var", "virtual", "void",
          "volatile", "while", "yield", "async", "await", "dynamic",
          "get", "set", "partial", "where", "nameof"};

      for (const auto &k : keywords)
        langDef.mKeywords.insert(k);

      static const char *const identifiers[] = {
          "Console", "Math", "String", "Int32", "Int64", "Boolean", "Object",
          "Exception", "List", "Dictionary", "Task", "Action", "Func",
          "IEnumerable", "IDisposable", "EventArgs", "Vector3", "Quaternion",
          "GameObject", "Transform", "Debug"};

      for (const auto &id : identifiers)
      {
        TextEditor::Identifier identifier;
        identifier.mDeclaration = "Built-in type";
        langDef.mIdentifiers.insert(std::make_pair(std::string(id), identifier));
      }

      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "L?\\\"(\\\\.|[^\\\"])*\\\"", TextEditor::PaletteIndex::String));
      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "\\'\\\\?[^\\']\\'", TextEditor::PaletteIndex::CharLiteral));
      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TextEditor::PaletteIndex::Number));
      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fFdDmM]?", TextEditor::PaletteIndex::Number));
      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier));
      langDef.mTokenRegexStrings.push_back(
          std::make_pair<std::string, TextEditor::PaletteIndex>(
              "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]",
              TextEditor::PaletteIndex::Punctuation));

      langDef.mCommentStart = "/*";
      langDef.mCommentEnd = "*/";
      langDef.mSingleLineComment = "//";
      langDef.mAutoIndentation = true;
      langDef.mCaseSensitive = true;
      langDef.mName = "C#";

      return langDef;
    }

    std::unique_ptr<TextEditor> create_script_text_editor(const std::string &contents)
    {
      static const TextEditor::LanguageDefinition csharpLanguageDefinition = create_csharp_language_definition();

      auto editor = std::make_unique<TextEditor>();
      editor->SetLanguageDefinition(csharpLanguageDefinition);
      editor->SetPalette(TextEditor::GetDarkPalette());
      editor->SetTabSize(4);
      editor->SetShowWhitespaces(false);
      editor->SetText(contents);
      return editor;
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

    std::string normalize_generic_path(const std::filesystem::path &path)
    {
      return path.lexically_normal().generic_string();
    }

    std::string normalize_generic_path(const std::string &path)
    {
      return normalize_generic_path(std::filesystem::path(path));
    }

    bool has_path_separator(const std::string &name)
    {
      return name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
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
             "\n"
             "    public override void OnKeyDown(EntityContext context, int keyCode)\n"
             "    {\n"
             "    }\n"
             "\n"
             "    public override void OnKeyUp(EntityContext context, int keyCode)\n"
             "    {\n"
             "    }\n"
             "}\n";
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
      openScriptEditorTabs_.clear();
      activeScriptEditorTabIndex_.reset();
      scriptEditorStatusMessage_.clear();
      scriptEditorStatusIsError_ = false;
      focusScriptEditorWindow_ = false;
      openScriptEditorUnsavedChangesDialog_ = false;
      pendingScriptEditorClosePath_.reset();
      workspaceScriptListDirty_ = false;
      parsedScriptCache_.clear();
      parsedScriptModTimes_.clear();
      lastCompileError_.clear();
      scriptCompileStatus_ = ScriptCompileStatus::Unknown;
      currentCompileRequestId_ = ++nextCompileRequestId_;
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
        const std::filesystem::path deletedPath = pendingWorkspaceDeletePath_;
        pendingWorkspaceDeletePath_.clear();

        std::size_t removedTabCount = 0;
        for (std::size_t index = openScriptEditorTabs_.size(); index > 0; --index)
        {
          if (!path_is_same_or_within(deletedPath, openScriptEditorTabs_[index - 1].path))
          {
            continue;
          }

          close_script_editor_tab(index - 1);
          ++removedTabCount;
        }

        if (pendingScriptEditorClosePath_.has_value() &&
            path_is_same_or_within(deletedPath, *pendingScriptEditorClosePath_))
        {
          pendingScriptEditorClosePath_.reset();
          openScriptEditorUnsavedChangesDialog_ = false;
        }

        if (removedTabCount > 0)
        {
          scriptEditorStatusMessage_ = "Closed " + std::to_string(removedTabCount) +
              (removedTabCount == 1 ? " script tab that was deleted from the workspace." : " script tabs that were deleted from the workspace.");
          scriptEditorStatusIsError_ = false;
        }

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

  void Editor::render_workspace_tree_node(const WorkspaceTreeNode &node)
  {
    const std::string label = path_display_name(node.path);
    const std::string treeNodeId = label + "##" + node.path.string();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!node.directory || node.children.empty())
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    const ScriptEditorTab *activeTab = active_script_editor_tab();
    if (!node.directory &&
        activeTab != nullptr &&
        activeTab->path.lexically_normal() == node.path.lexically_normal())
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open = ImGui::TreeNodeEx(treeNodeId.c_str(), flags);
    if (!node.directory &&
        node.path.extension() == ".cs" &&
        ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
      request_script_editor_open(node.path, relative_workspace_path(activeWorkspacePath_, node.path));
    }

    if (ImGui::BeginPopupContextItem())
    {
      const std::filesystem::path destinationDirectory =
          node.directory ? node.path : node.path.parent_path();
      const bool isWorkspaceRoot = node.path == activeWorkspacePath_;

      if (ImGui::MenuItem("New Folder"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Folder, destinationDirectory);
      }
      if (ImGui::MenuItem("New Script"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Script, destinationDirectory);
      }
      if (ImGui::MenuItem("Import"))
      {
        request_workspace_item_import(destinationDirectory);
      }
      if (!node.directory && node.path.extension() == ".cs")
      {
        if (ImGui::MenuItem("Open in Script Editor"))
        {
          request_script_editor_open(node.path, relative_workspace_path(activeWorkspacePath_, node.path));
        }
      }

      if (!isWorkspaceRoot)
      {
        ImGui::Separator();
        if (ImGui::MenuItem(workspace_delete_button_label(node.path).c_str()))
        {
          request_workspace_item_deletion(node.path);
        }
      }
      ImGui::EndPopup();
    }

    if (!node.directory || !open || node.children.empty())
    {
      return;
    }

    for (const auto &child : node.children)
    {
      render_workspace_tree_node(child);
    }

    ImGui::TreePop();
  }

  bool Editor::is_script_editor_window_open() const
  {
    return openScriptEditorWindow_;
  }

  void Editor::set_script_editor_window_open(bool open)
  {
    openScriptEditorWindow_ = open;
    if (open)
    {
      focusScriptEditorWindow_ = true;
    }
    else
    {
      focusScriptEditorWindow_ = false;
      openScriptEditorUnsavedChangesDialog_ = false;
      pendingScriptEditorClosePath_.reset();
    }
  }

  bool Editor::consume_script_editor_focus_request()
  {
    const bool focusRequested = focusScriptEditorWindow_;
    focusScriptEditorWindow_ = false;
    return focusRequested;
  }

  void Editor::render_script_editor_window(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (!openScriptEditorWindow_)
    {
      return;
    }

    const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->Pos);
    ImGui::SetNextWindowSize(mainViewport->Size);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("Detached Script Editor Host", nullptr, windowFlags))
    {
      ImGui::End();
      ImGui::PopStyleVar(2);
      return;
    }

    render_script_editor(entityManager, componentManager);

    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  std::optional<std::size_t> Editor::find_script_editor_tab_index(const std::filesystem::path &scriptPath) const
  {
    const std::filesystem::path normalizedPath = scriptPath.lexically_normal();
    for (std::size_t index = 0; index < openScriptEditorTabs_.size(); ++index)
    {
      if (openScriptEditorTabs_[index].path.lexically_normal() == normalizedPath)
      {
        return index;
      }
    }

    return std::nullopt;
  }

  Editor::ScriptEditorTab *Editor::active_script_editor_tab()
  {
    if (!activeScriptEditorTabIndex_.has_value() || *activeScriptEditorTabIndex_ >= openScriptEditorTabs_.size())
    {
      return nullptr;
    }

    return &openScriptEditorTabs_[*activeScriptEditorTabIndex_];
  }

  const Editor::ScriptEditorTab *Editor::active_script_editor_tab() const
  {
    if (!activeScriptEditorTabIndex_.has_value() || *activeScriptEditorTabIndex_ >= openScriptEditorTabs_.size())
    {
      return nullptr;
    }

    return &openScriptEditorTabs_[*activeScriptEditorTabIndex_];
  }

  void Editor::activate_script_editor_tab(std::size_t index)
  {
    if (index >= openScriptEditorTabs_.size())
    {
      return;
    }

    activeScriptEditorTabIndex_ = index;
    pendingScriptEditorTabSelectionIndex_ = index;
    openScriptEditorWindow_ = true;
    focusScriptEditorWindow_ = true;
  }

  void Editor::close_script_editor_tab(std::size_t index)
  {
    if (index >= openScriptEditorTabs_.size())
    {
      return;
    }

    const std::filesystem::path closedPath = openScriptEditorTabs_[index].path.lexically_normal();
    openScriptEditorTabs_.erase(openScriptEditorTabs_.begin() + index);

    if (pendingScriptEditorClosePath_.has_value() &&
        pendingScriptEditorClosePath_->lexically_normal() == closedPath)
    {
      pendingScriptEditorClosePath_.reset();
    }

    if (openScriptEditorTabs_.empty())
    {
      activeScriptEditorTabIndex_.reset();
      pendingScriptEditorTabSelectionIndex_.reset();
      return;
    }

    if (!activeScriptEditorTabIndex_.has_value())
    {
      activeScriptEditorTabIndex_ = 0;
      pendingScriptEditorTabSelectionIndex_ = 0;
      return;
    }

    if (*activeScriptEditorTabIndex_ > index)
    {
      --(*activeScriptEditorTabIndex_);
      if (pendingScriptEditorTabSelectionIndex_.has_value() && *pendingScriptEditorTabSelectionIndex_ > index)
      {
        --(*pendingScriptEditorTabSelectionIndex_);
      }
      return;
    }

    if (*activeScriptEditorTabIndex_ >= openScriptEditorTabs_.size())
    {
      activeScriptEditorTabIndex_ = openScriptEditorTabs_.size() - 1;
      pendingScriptEditorTabSelectionIndex_ = activeScriptEditorTabIndex_;
    }
    else if (pendingScriptEditorTabSelectionIndex_.has_value() &&
             *pendingScriptEditorTabSelectionIndex_ >= openScriptEditorTabs_.size())
    {
      pendingScriptEditorTabSelectionIndex_.reset();
    }
  }

  void Editor::request_script_editor_open(
      const std::filesystem::path &scriptPath,
      const std::string &relativePath)
  {
    if (scriptPath.extension() != ".cs")
    {
      return;
    }

    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;

    std::string errorMessage;
    if (!open_script_document(scriptPath.lexically_normal(), relativePath, &errorMessage))
    {
      scriptEditorStatusMessage_ = std::move(errorMessage);
      scriptEditorStatusIsError_ = true;
    }
  }

  bool Editor::open_script_document(
      const std::filesystem::path &scriptPath,
      const std::string &relativePath,
      std::string *errorMessage)
  {
    if (const auto existingIndex = find_script_editor_tab_index(scriptPath); existingIndex.has_value())
    {
      ScriptEditorTab &existingTab = openScriptEditorTabs_[*existingIndex];
      if (existingTab.textEditor == nullptr)
      {
        existingTab.textEditor = create_script_text_editor(existingTab.contents);
      }
      activate_script_editor_tab(*existingIndex);
      return true;
    }

    ScriptDocumentSnapshot snapshot;
    if (!load_script_document(scriptPath, snapshot, errorMessage))
    {
      return false;
    }

    ScriptEditorTab tab;
    tab.path = scriptPath;
    tab.relativePath = relativePath.empty() && !activeWorkspacePath_.empty()
                           ? relative_workspace_path(activeWorkspacePath_, scriptPath)
                           : relativePath;
    tab.contents = std::move(snapshot.contents);
    tab.savedContents = tab.contents;
    tab.textEditor = create_script_text_editor(tab.contents);
    if (snapshot.hasLastWriteTime)
    {
      tab.savedWriteTime = snapshot.lastWriteTime;
    }

    openScriptEditorTabs_.push_back(std::move(tab));
    activate_script_editor_tab(openScriptEditorTabs_.size() - 1);
    return true;
  }

  bool Editor::save_script_document_at_index(
      std::size_t index,
      bool triggerCompile,
      std::string *errorMessage)
  {
    if (index >= openScriptEditorTabs_.size())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "The selected script tab is no longer available.";
      }
      return false;
    }

    ScriptEditorTab &tab = openScriptEditorTabs_[index];
    if (tab.textEditor != nullptr)
    {
      tab.contents = tab.textEditor->GetText();
    }
    ScriptDocumentSnapshot snapshot;
    if (!save_script_document(tab.path, tab.contents, &snapshot, errorMessage))
    {
      return false;
    }

    tab.savedContents = tab.contents;
    tab.dirty = false;
    if (snapshot.hasLastWriteTime)
    {
      tab.savedWriteTime = snapshot.lastWriteTime;
    }
    else
    {
      tab.savedWriteTime.reset();
    }

    scriptEditorStatusMessage_ = "Saved " +
        (tab.relativePath.empty() ? tab.path.filename().string() : tab.relativePath) +
        ".";
    scriptEditorStatusIsError_ = false;

    if (!triggerCompile)
    {
      lastCompileError_.clear();
      scriptCompileStatus_ = ScriptCompileStatus::Unknown;
    }

    if (triggerCompile)
    {
      queue_workspace_script_compile();
    }

    return true;
  }

  bool Editor::save_active_script_document(bool triggerCompile, std::string *errorMessage)
  {
    if (!activeScriptEditorTabIndex_.has_value())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No script is currently open in the editor.";
      }
      return false;
    }

    return save_script_document_at_index(*activeScriptEditorTabIndex_, triggerCompile, errorMessage);
  }

  bool Editor::save_all_script_documents(bool triggerCompile, std::string *errorMessage)
  {
    for (std::size_t index = 0; index < openScriptEditorTabs_.size(); ++index)
    {
      if (!openScriptEditorTabs_[index].dirty)
      {
        continue;
      }

      if (!save_script_document_at_index(index, false, errorMessage))
      {
        return false;
      }
    }

    if (triggerCompile)
    {
      queue_workspace_script_compile();
    }

    return true;
  }

  void Editor::queue_workspace_script_compile()
  {
    if (activeWorkspacePath_.empty())
    {
      return;
    }

    if (workspaceScriptFiles_.empty())
    {
      lastCompileError_.clear();
      scriptCompileStatus_ = ScriptCompileStatus::Unknown;
      workspaceScriptListDirty_ = false;
      return;
    }

    if (backgroundCompileInProgress_)
    {
      workspaceScriptListDirty_ = true;
      return;
    }

    std::vector<std::filesystem::path> sourceFiles;
    sourceFiles.reserve(workspaceScriptFiles_.size());
    for (const auto &relPath : workspaceScriptFiles_)
    {
      sourceFiles.push_back(activeWorkspacePath_ / relPath);
    }

    lastCompileError_.clear();
    scriptCompileStatus_ = ScriptCompileStatus::Unknown;
    backgroundCompileInProgress_ = true;
    workspaceScriptListDirty_ = false;
    const std::uint64_t requestId = ++nextCompileRequestId_;
    currentCompileRequestId_ = requestId;
    backgroundCompileResult_ = std::async(std::launch::async,
        [files = std::move(sourceFiles), requestId]() -> BackgroundCompileTaskResult
        {
          std::string error;
          ScriptRuntime::compile(files, &error);
          return BackgroundCompileTaskResult{requestId, std::move(error)};
        });
  }

  void Editor::render_script_editor(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (openScriptEditorUnsavedChangesDialog_)
    {
      ImGui::OpenPopup(SCRIPT_EDITOR_UNSAVED_POPUP_TITLE);
      openScriptEditorUnsavedChangesDialog_ = false;
    }

    if (ImGui::BeginPopupModal(SCRIPT_EDITOR_UNSAVED_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      std::string label = "this script";
      if (pendingScriptEditorClosePath_.has_value())
      {
        if (const auto closeIndex = find_script_editor_tab_index(*pendingScriptEditorClosePath_); closeIndex.has_value())
        {
          const ScriptEditorTab &tab = openScriptEditorTabs_[*closeIndex];
          label = tab.relativePath.empty() ? tab.path.filename().string() : tab.relativePath;
        }
        else
        {
          label = pendingScriptEditorClosePath_->filename().string();
        }
      }
      ImGui::TextWrapped("Save changes to %s before closing the tab?", label.c_str());

      if (scriptEditorStatusIsError_ && !scriptEditorStatusMessage_.empty())
      {
        ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", scriptEditorStatusMessage_.c_str());
      }

      if (ImGui::Button("Save and Close"))
      {
        std::string errorMessage;
        if (pendingScriptEditorClosePath_.has_value())
        {
          const auto closeIndex = find_script_editor_tab_index(*pendingScriptEditorClosePath_);
          if (closeIndex.has_value() &&
              save_script_document_at_index(*closeIndex, true, &errorMessage))
          {
            close_script_editor_tab(*closeIndex);
            pendingScriptEditorClosePath_.reset();
            ImGui::CloseCurrentPopup();
          }
          else
          {
            scriptEditorStatusMessage_ = std::move(errorMessage);
            scriptEditorStatusIsError_ = true;
          }
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Discard Changes"))
      {
        if (pendingScriptEditorClosePath_.has_value())
        {
          if (const auto closeIndex = find_script_editor_tab_index(*pendingScriptEditorClosePath_); closeIndex.has_value())
          {
            close_script_editor_tab(*closeIndex);
          }
          pendingScriptEditorClosePath_.reset();
          ImGui::CloseCurrentPopup();
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        pendingScriptEditorClosePath_.reset();
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    // --- Menu bar on the host window itself ---
    bool saveRequested = false;
    bool saveAllRequested = false;
    bool compileRequested = false;
    bool revertRequested = false;

    {
      const bool hasActiveTab = activeScriptEditorTabIndex_.has_value() &&
                                *activeScriptEditorTabIndex_ < openScriptEditorTabs_.size();
      const bool canSaveActive = hasActiveTab && openScriptEditorTabs_[*activeScriptEditorTabIndex_].dirty;

      if (ImGui::BeginMenuBar())
      {
        if (ImGui::BeginMenu("Script"))
        {
          saveRequested = ImGui::MenuItem("Save", nullptr, false, canSaveActive);
          saveAllRequested = ImGui::MenuItem("Save All", nullptr, false, !openScriptEditorTabs_.empty());
          compileRequested = ImGui::MenuItem("Compile Workspace", nullptr, false, !activeWorkspacePath_.empty());
          revertRequested = ImGui::MenuItem("Revert", nullptr, false, canSaveActive);
          ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
      }
    }

    // --- Handle menu actions ---
    if (saveRequested)
    {
      std::string errorMessage;
      if (!save_active_script_document(true, &errorMessage))
      {
        scriptEditorStatusMessage_ = std::move(errorMessage);
        scriptEditorStatusIsError_ = true;
      }
    }

    if (saveAllRequested)
    {
      std::string errorMessage;
      if (!save_all_script_documents(false, &errorMessage))
      {
        scriptEditorStatusMessage_ = std::move(errorMessage);
        scriptEditorStatusIsError_ = true;
      }
    }

    if (compileRequested)
    {
      std::string errorMessage;
      if (!save_all_script_documents(false, &errorMessage))
      {
        scriptEditorStatusMessage_ = std::move(errorMessage);
        scriptEditorStatusIsError_ = true;
      }
      else
      {
        queue_workspace_script_compile();
      }
    }

    if (revertRequested)
    {
      ScriptEditorTab *revertTab = active_script_editor_tab();
      if (revertTab != nullptr)
      {
        ScriptDocumentSnapshot snapshot;
        std::string errorMessage;
        if (!load_script_document(revertTab->path, snapshot, &errorMessage))
        {
          scriptEditorStatusMessage_ = std::move(errorMessage);
          scriptEditorStatusIsError_ = true;
        }
        else
        {
          revertTab->contents = std::move(snapshot.contents);
          revertTab->savedContents = revertTab->contents;
          if (revertTab->textEditor == nullptr)
          {
            revertTab->textEditor = create_script_text_editor(revertTab->contents);
          }
          else
          {
            revertTab->textEditor->SetText(revertTab->contents);
          }
          revertTab->savedWriteTime = snapshot.hasLastWriteTime
                                          ? std::optional<std::filesystem::file_time_type>(snapshot.lastWriteTime)
                                          : std::nullopt;
          revertTab->dirty = false;
          scriptEditorStatusMessage_ = "Reverted unsaved changes.";
          scriptEditorStatusIsError_ = false;
        }
      }
    }

    // --- Three-column layout below the menu bar ---
    if (ImGui::BeginTable(
            "ScriptEditorLayout",
            3,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthFixed, 280.0f);
      ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Entities", ImGuiTableColumnFlags_WidthFixed, 300.0f);

      // --- Left column: file tree ---
      ImGui::TableNextColumn();
      if (!workspaceScanError_.empty())
      {
        ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceScanError_.c_str());
        ImGui::Separator();
      }

      ImGui::BeginChild("ScriptEditorTree");
      if (!workspaceTreeRoot_.has_value())
      {
        ImGui::TextDisabled(activeWorkspacePath_.empty() ? "Open a workspace to browse scripts." : "No files.");
      }
      else
      {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        render_workspace_tree_node(*workspaceTreeRoot_);
      }

      if (ImGui::BeginPopupContextWindow("ScriptEditorRootContext", ImGuiPopupFlags_NoOpenOverItems))
      {
        const bool canModifyWorkspace = !activeWorkspacePath_.empty();
        if (!canModifyWorkspace)
        {
          ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("New Folder"))
        {
          request_workspace_item_creation(WorkspaceCreateKind::Folder, activeWorkspacePath_);
        }
        if (ImGui::MenuItem("New Script"))
        {
          request_workspace_item_creation(WorkspaceCreateKind::Script, activeWorkspacePath_);
        }
        if (ImGui::MenuItem("Import"))
        {
          request_workspace_item_import(activeWorkspacePath_);
        }
        if (!canModifyWorkspace)
        {
          ImGui::EndDisabled();
        }
        ImGui::EndPopup();
      }
      ImGui::EndChild();

      // --- Middle column: tabs + code editor ---
      ImGui::TableNextColumn();

      if (openScriptEditorTabs_.empty())
      {
        ImGui::TextDisabled("Open a .cs file from the file tree to start editing.");
        if (!scriptEditorStatusMessage_.empty())
        {
          const ImVec4 color = scriptEditorStatusIsError_
                                   ? ImVec4(0.88f, 0.42f, 0.42f, 1.0f)
                                   : ImVec4(0.42f, 0.88f, 0.42f, 1.0f);
          ImGui::TextColored(color, "%s", scriptEditorStatusMessage_.c_str());
        }

        if (backgroundCompileInProgress_)
        {
          ImGui::TextDisabled("Compiling scripts...");
        }
        else if (scriptCompileStatus_ == ScriptCompileStatus::Failed)
        {
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", lastCompileError_.c_str());
        }
        else if (scriptCompileStatus_ == ScriptCompileStatus::Succeeded &&
                 !workspaceScriptFiles_.empty())
        {
          ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Workspace scripts compiled successfully.");
        }
        else if (!workspaceScriptFiles_.empty())
        {
          ImGui::TextDisabled("Workspace scripts have not been compiled yet.");
        }
      }
      else
      {
        if (!activeScriptEditorTabIndex_.has_value() || *activeScriptEditorTabIndex_ >= openScriptEditorTabs_.size())
        {
          activeScriptEditorTabIndex_ = 0;
        }

        std::optional<std::size_t> closeTabIndex;
        if (ImGui::BeginTabBar("ScriptEditorTabs", ImGuiTabBarFlags_AutoSelectNewTabs))
        {
          for (std::size_t index = 0; index < openScriptEditorTabs_.size(); ++index)
          {
            ScriptEditorTab &tab = openScriptEditorTabs_[index];
            bool keepTabOpen = true;
            ImGuiTabItemFlags itemFlags = tab.dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
            if (pendingScriptEditorTabSelectionIndex_.has_value() &&
                *pendingScriptEditorTabSelectionIndex_ == index)
            {
              itemFlags |= ImGuiTabItemFlags_SetSelected;
            }

            const std::string tabLabel = path_display_name(tab.path) + "##" + tab.path.string();
            if (ImGui::BeginTabItem(tabLabel.c_str(), &keepTabOpen, itemFlags))
            {
              activeScriptEditorTabIndex_ = index;
              ImGui::EndTabItem();
            }

            if (!keepTabOpen)
            {
              closeTabIndex = index;
            }
          }
          ImGui::EndTabBar();
          pendingScriptEditorTabSelectionIndex_.reset();
        }

        if (closeTabIndex.has_value())
        {
          if (openScriptEditorTabs_[*closeTabIndex].dirty)
          {
            pendingScriptEditorClosePath_ = openScriptEditorTabs_[*closeTabIndex].path;
            openScriptEditorUnsavedChangesDialog_ = true;
          }
          else
          {
            close_script_editor_tab(*closeTabIndex);
          }
        }

        ScriptEditorTab *activeTab = active_script_editor_tab();
        if (activeTab != nullptr)
        {
          if (!scriptEditorStatusMessage_.empty())
          {
            const ImVec4 color = scriptEditorStatusIsError_
                                     ? ImVec4(0.88f, 0.42f, 0.42f, 1.0f)
                                     : ImVec4(0.42f, 0.88f, 0.42f, 1.0f);
            ImGui::TextColored(color, "%s", scriptEditorStatusMessage_.c_str());
          }

          if (backgroundCompileInProgress_)
          {
            ImGui::TextDisabled("Compiling scripts...");
          }
          else if (scriptCompileStatus_ == ScriptCompileStatus::Failed)
          {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", lastCompileError_.c_str());
          }
          else if (scriptCompileStatus_ == ScriptCompileStatus::Succeeded &&
                   !workspaceScriptFiles_.empty())
          {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Workspace scripts compiled successfully.");
          }
          else if (!workspaceScriptFiles_.empty())
          {
            ImGui::TextDisabled("Workspace scripts have not been compiled yet.");
          }

          ImVec2 editorSize = ImGui::GetContentRegionAvail();
          editorSize.y = std::max(editorSize.y, 200.0f);
          if (activeTab->textEditor == nullptr)
          {
            activeTab->textEditor = create_script_text_editor(activeTab->contents);
          }
          ImGui::PushID(activeTab->path.string().c_str());
          activeTab->textEditor->Render("##ScriptEditorContents", editorSize, true);
          if (activeTab->textEditor->IsTextChanged())
          {
            activeTab->contents = activeTab->textEditor->GetText();
            activeTab->dirty = activeTab->contents != activeTab->savedContents;
            if (!scriptEditorStatusIsError_)
            {
              scriptEditorStatusMessage_.clear();
            }
          }
          ImGui::PopID();
        }
      }

      // --- Right column: entities using script ---
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Entities Using Script");
      ImGui::Separator();

      ScriptEditorTab *activeTab = active_script_editor_tab();
      if (activeTab != nullptr)
      {
        const std::string activeScriptRelativePath =
            activeWorkspacePath_.empty()
                ? normalize_generic_path(activeTab->path)
                : normalize_generic_path(relative_workspace_path(activeWorkspacePath_, activeTab->path));

        std::vector<Entity::EntityId> matchingEntities;
        for (const Entity::EntityId entity : entityManager.getAllEntities())
        {
          if (!componentManager.hasComponent<ScriptComponent>(entity))
          {
            continue;
          }

          const auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
          const bool usesScript = std::any_of(
              scriptComponent.attachments.begin(),
              scriptComponent.attachments.end(),
              [&activeScriptRelativePath](const ScriptAttachment &attachment)
              {
                return !attachment.scriptPath.empty() &&
                       normalize_generic_path(attachment.scriptPath) == activeScriptRelativePath;
              });
          if (usesScript)
          {
            matchingEntities.push_back(entity);
          }
        }

        ImGui::BeginChild("ScriptEditorEntityUsers");
        if (matchingEntities.empty())
        {
          ImGui::TextDisabled("No entities use this script.");
        }
        else
        {
          ImGui::TextDisabled("%zu matching %s", matchingEntities.size(), matchingEntities.size() == 1 ? "entity" : "entities");
          ImGui::Separator();

          for (const Entity::EntityId entity : matchingEntities)
          {
            const bool selected = state.selectedEntity.has_value() && *state.selectedEntity == entity;
            if (ImGui::Selectable(entity_label(entity, componentManager).c_str(), selected))
            {
              if (const auto world = world_for_entity(entity, componentManager); world.has_value())
              {
                load_world(*world, componentManager);
              }
              state.selectedEntity = entity;
            }
          }
        }
        ImGui::EndChild();
      }
      else
      {
        ImGui::TextDisabled("No script selected.");
      }

      ImGui::EndTable();
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
      if (ImGui::MenuItem("Import"))
      {
        request_workspace_item_import(activeWorkspacePath_);
      }
      ImGui::EndPopup();
    }
    ImGui::End();
  }
}
