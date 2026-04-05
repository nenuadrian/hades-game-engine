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

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
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

    struct FileTypeVisual
    {
      const char *icon;
      ImU32 iconColor;
    };

    std::string to_lower(const std::string &str)
    {
      std::string result = str;
      for (auto &c : result)
      {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return result;
    }

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
      pendingCloseAllScriptEditorTabs_ = false;
      pendingCloseScriptEditorWindow_ = false;
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

  void Editor::render_workspace_tree_node(const WorkspaceTreeNode &node, int depth, int &rowIndex, const char *filter)
  {
    const std::string label = path_display_name(node.path);
    const bool isLeaf = !node.directory || node.children.empty();
    const bool hasFilter = filter[0] != '\0';

    // Filter: skip nodes that don't match.
    if (hasFilter)
    {
      if (!node.directory)
      {
        const std::string lowerName = to_lower(label);
        const std::string lowerFilter = to_lower(filter);
        if (lowerName.find(lowerFilter) == std::string::npos)
        {
          return;
        }
      }
      else if (!subtree_matches_filter(node, filter))
      {
        return;
      }
    }

    // Determine selection state.
    const ScriptEditorTab *activeTab = active_script_editor_tab();
    const bool isSelected = !node.directory &&
                            activeTab != nullptr &&
                            activeTab->path.lexically_normal() == node.path.lexically_normal();

    // Get visual properties for this file type.
    // We need to peek at whether the tree node will be open for the folder icon.
    // Use ImGui storage to check the open state from the previous frame.
    const std::string treeNodeId = "##ws_" + node.path.string();
    const ImGuiID nodeId = ImGui::GetID(treeNodeId.c_str());
    const bool wasOpen = ImGui::GetStateStorage()->GetBool(nodeId, false);
    const bool effectiveOpen = hasFilter && node.directory ? true : wasOpen;
    const FileTypeVisual visual = get_file_type_visual(node.path, node.directory, effectiveOpen);

    // --- Draw alternating row background ---
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const float windowLeft = ImGui::GetWindowPos().x;
    const float windowWidth = ImGui::GetWindowSize().x;
    const ImVec2 rowMin = ImVec2(windowLeft, cursorPos.y);
    const ImVec2 rowMax = ImVec2(windowLeft + windowWidth, cursorPos.y + rowHeight);

    if (rowIndex % 2 == 1)
    {
      drawList->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 6));
    }

    // --- Draw indentation guide lines ---
    const float indentSpacing = ImGui::GetStyle().IndentSpacing;
    const float baseX = windowLeft + ImGui::GetStyle().WindowPadding.x;
    for (int d = 1; d <= depth; ++d)
    {
      const float lineX = baseX + (static_cast<float>(d) - 0.5f) * indentSpacing;
      drawList->AddLine(
          ImVec2(lineX, rowMin.y),
          ImVec2(lineX, rowMax.y),
          IM_COL32(255, 255, 255, 16),
          1.0f);
    }

    rowIndex++;

    // --- TreeNodeEx with suppressed built-in backgrounds ---
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding;
    if (isLeaf)
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (isSelected)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Force open when filtering.
    if (hasFilter && node.directory)
    {
      ImGui::SetNextItemOpen(true);
    }

    // Suppress built-in header colors to draw our own.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));

    const bool open = ImGui::TreeNodeEx(treeNodeId.c_str(), flags, "");

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // --- Capture tree node interaction state while it's still the last item ---
    const bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_None);
    const bool isTreeNodeFocused = ImGui::IsItemFocused();
    const bool isRenaming = workspaceRenamePath_ == node.path && !workspaceRenamePath_.empty();

    // --- Drag-and-drop source (must be right after TreeNodeEx, the last interactive item) ---
    if (!isRenaming && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
    {
      const std::string pathStr = node.path.string();
      ImGui::SetDragDropPayload("WS_NODE", pathStr.c_str(), pathStr.size() + 1);
      ImGui::PushStyleColor(ImGuiCol_Text, ImColor(visual.iconColor).Value);
      ImGui::TextUnformatted(visual.icon);
      ImGui::PopStyleColor();
      ImGui::SameLine(0.0f, 6.0f);
      ImGui::TextUnformatted(label.c_str());
      ImGui::EndDragDropSource();
    }

    // --- Drag-and-drop target (directories only) ---
    if (node.directory && ImGui::BeginDragDropTarget())
    {
      drawList->AddRect(rowMin, rowMax, IM_COL32(210, 180, 100, 120), 2.0f, 0, 2.0f);

      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("WS_NODE"))
      {
        const std::filesystem::path sourcePath(static_cast<const char *>(payload->Data));
        const std::filesystem::path destPath = node.path / sourcePath.filename();

        bool isSelfOrDescendant = false;
        if (sourcePath == node.path)
        {
          isSelfOrDescendant = true;
        }
        else
        {
          const auto rel = node.path.lexically_relative(sourcePath);
          if (!rel.empty() && *rel.begin() != "..")
          {
            isSelfOrDescendant = true;
          }
        }

        if (!isSelfOrDescendant && sourcePath.parent_path() != node.path)
        {
          std::error_code ec;
          std::filesystem::rename(sourcePath, destPath, ec);
          if (!ec)
          {
            invalidate_workspace_cache();
          }
        }
      }
      ImGui::EndDragDropTarget();
    }

    // --- Draw hover / selection overlay ---
    if (isSelected)
    {
      drawList->AddRectFilled(rowMin, rowMax, IM_COL32(179, 168, 161, 30));
    }
    else if (isHovered)
    {
      drawList->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 12));
    }

    // --- Draw icon + label on same line ---
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(visual.iconColor).Value);
    ImGui::TextUnformatted(visual.icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);

    // Inline rename mode.
    if (isRenaming)
    {
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      if (workspaceRenameFocusPending_)
      {
        ImGui::SetKeyboardFocusHere();
        workspaceRenameFocusPending_ = false;
      }
      if (ImGui::InputText("##rename", workspaceRenameBuffer_.data(), workspaceRenameBuffer_.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
      {
        // Confirm rename.
        const std::string newName(workspaceRenameBuffer_.data());
        if (!newName.empty() && newName != label)
        {
          std::error_code ec;
          std::filesystem::rename(node.path, node.path.parent_path() / newName, ec);
          if (!ec)
          {
            invalidate_workspace_cache();
          }
        }
        workspaceRenamePath_.clear();
      }
      else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
               (!ImGui::IsItemActive() && !workspaceRenameFocusPending_ && ImGui::IsItemDeactivated()))
      {
        workspaceRenamePath_.clear();
      }
    }
    else
    {
      ImGui::TextUnformatted(label.c_str());
    }

    // --- Click handling ---
    if (!isRenaming && !node.directory &&
        node.path.extension() == ".cs" &&
        isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      request_script_editor_open(node.path, relative_workspace_path(activeWorkspacePath_, node.path));
    }

    // --- F2 to rename ---
    if (!isRenaming && isTreeNodeFocused && ImGui::IsKeyPressed(ImGuiKey_F2))
    {
      const bool isWorkspaceRoot = node.path == activeWorkspacePath_;
      if (!isWorkspaceRoot)
      {
        workspaceRenamePath_ = node.path;
        std::snprintf(workspaceRenameBuffer_.data(), workspaceRenameBuffer_.size(), "%s", label.c_str());
        workspaceRenameFocusPending_ = true;
      }
    }

    // --- Context menu ---
    const std::string contextMenuId = "##ws_ctx_" + node.path.string();
    if (ImGui::BeginPopupContextItem(contextMenuId.c_str()))
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
        if (ImGui::MenuItem("Rename"))
        {
          workspaceRenamePath_ = node.path;
          std::snprintf(workspaceRenameBuffer_.data(), workspaceRenameBuffer_.size(), "%s", label.c_str());
          workspaceRenameFocusPending_ = true;
        }
        if (ImGui::MenuItem(workspace_delete_button_label(node.path).c_str()))
        {
          request_workspace_item_deletion(node.path);
        }
      }
      ImGui::EndPopup();
    }

    // --- Render children ---
    if (!node.directory || !open || node.children.empty())
    {
      return;
    }

    for (const auto &child : node.children)
    {
      render_workspace_tree_node(child, depth + 1, rowIndex, filter);
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
      pendingCloseAllScriptEditorTabs_ = false;
      pendingCloseScriptEditorWindow_ = false;
    }
  }

  void Editor::request_close_script_editor_window()
  {
    if (!openScriptEditorWindow_)
    {
      return;
    }

    pendingCloseScriptEditorWindow_ = true;
    pendingCloseAllScriptEditorTabs_ = true;
    continue_close_all_script_editor_tabs();
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
    scriptAutoComplete_.reset();

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

  void Editor::continue_close_all_script_editor_tabs()
  {
    while (pendingCloseAllScriptEditorTabs_ && !openScriptEditorTabs_.empty())
    {
      const std::size_t closeIndex = openScriptEditorTabs_.size() - 1;
      if (openScriptEditorTabs_[closeIndex].dirty)
      {
        pendingScriptEditorClosePath_ = openScriptEditorTabs_[closeIndex].path;
        openScriptEditorUnsavedChangesDialog_ = true;
        return;
      }

      close_script_editor_tab(closeIndex);
    }

    if (openScriptEditorTabs_.empty())
    {
      pendingCloseAllScriptEditorTabs_ = false;
      if (pendingCloseScriptEditorWindow_)
      {
        pendingCloseScriptEditorWindow_ = false;
        set_script_editor_window_open(false);
      }
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
            continue_close_all_script_editor_tabs();
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
          continue_close_all_script_editor_tabs();
          ImGui::CloseCurrentPopup();
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        pendingScriptEditorClosePath_.reset();
        pendingCloseAllScriptEditorTabs_ = false;
        pendingCloseScriptEditorWindow_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    // --- Menu bar on the host window itself ---
    bool saveRequested = false;
    bool saveAllRequested = false;
    bool compileRequested = false;
    bool revertRequested = false;
    bool closeRequested = false;
    bool closeAllRequested = false;
    bool closeScriptEditorRequested = false;

    {
      const bool hasActiveTab = activeScriptEditorTabIndex_.has_value() &&
                                *activeScriptEditorTabIndex_ < openScriptEditorTabs_.size();
      const bool canSaveActive = hasActiveTab && openScriptEditorTabs_[*activeScriptEditorTabIndex_].dirty;

      if (ImGui::BeginMenuBar())
      {
        if (ImGui::BeginMenu("File"))
        {
          saveRequested = ImGui::MenuItem("Save", nullptr, false, canSaveActive);
          saveAllRequested = ImGui::MenuItem("Save All", nullptr, false, !openScriptEditorTabs_.empty());
          compileRequested = ImGui::MenuItem("Compile Workspace", nullptr, false, !activeWorkspacePath_.empty());
          revertRequested = ImGui::MenuItem("Revert", nullptr, false, canSaveActive);
          ImGui::Separator();
          closeRequested = ImGui::MenuItem("Close", nullptr, false, hasActiveTab);
          closeAllRequested = ImGui::MenuItem("Close All", nullptr, false, !openScriptEditorTabs_.empty());
          ImGui::Separator();
          closeScriptEditorRequested = ImGui::MenuItem("Close Script Editor");
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

    if (closeRequested)
    {
      pendingCloseScriptEditorWindow_ = false;
      pendingCloseAllScriptEditorTabs_ = false;
      if (activeScriptEditorTabIndex_.has_value() && *activeScriptEditorTabIndex_ < openScriptEditorTabs_.size())
      {
        const std::size_t closeIndex = *activeScriptEditorTabIndex_;
        if (openScriptEditorTabs_[closeIndex].dirty)
        {
          pendingScriptEditorClosePath_ = openScriptEditorTabs_[closeIndex].path;
          openScriptEditorUnsavedChangesDialog_ = true;
        }
        else
        {
          close_script_editor_tab(closeIndex);
        }
      }
    }

    if (closeAllRequested)
    {
      pendingCloseScriptEditorWindow_ = false;
      pendingCloseAllScriptEditorTabs_ = true;
      continue_close_all_script_editor_tabs();
    }

    if (closeScriptEditorRequested)
    {
      request_close_script_editor_window();
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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        int scriptTreeRowIndex = 0;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        render_workspace_tree_node(*workspaceTreeRoot_, 0, scriptTreeRowIndex, "");
        ImGui::PopStyleVar(2);
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
          pendingCloseAllScriptEditorTabs_ = false;
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

          // Handle autocomplete keys before TextEditor processes input.
          const bool autocompleteConsumedKey = scriptAutoComplete_.handleKeys(*activeTab->textEditor);
          if (autocompleteConsumedKey)
            activeTab->textEditor->SetHandleKeyboardInputs(false);

          activeTab->textEditor->Render("##ScriptEditorContents", editorSize, true);

          activeTab->textEditor->SetHandleKeyboardInputs(true);

          if (activeTab->textEditor->IsTextChanged())
          {
            activeTab->contents = activeTab->textEditor->GetText();
            activeTab->dirty = activeTab->contents != activeTab->savedContents;
            if (!scriptEditorStatusIsError_)
            {
              scriptEditorStatusMessage_.clear();
            }
          }

          scriptAutoComplete_.update(*activeTab->textEditor, parsedScriptCache_);
          scriptAutoComplete_.renderPopup(*activeTab->textEditor);

          ImGui::PopID();
        }
      }

      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Entities");
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
              select_entity(entity);
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
