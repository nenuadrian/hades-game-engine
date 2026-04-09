#include "editor.hpp"

#include "script_document.hpp"

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
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
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
    constexpr char SCRIPT_EDITOR_WINDOW_TITLE[] = "Script Editor";
    constexpr char SCRIPT_EDITOR_UNSAVED_POPUP_TITLE[] = "Unsaved Script Changes";
    constexpr char SCRIPT_EDITOR_HOST_WINDOW_TITLE[] = "Detached Script Editor Host";
    constexpr char SCRIPT_EDITOR_DOCKSPACE_ID[] = "DetachedScriptEditorDockspace";
    constexpr char SCRIPT_EDITOR_PANEL_TITLE[] = "Script Editor##DetachedScriptEditorMainPanel";
    constexpr char SCRIPT_EDITOR_FILE_TREE_PANEL_TITLE[] = "File Tree##ScriptEditorFileTreePanel";
    constexpr char SCRIPT_EDITOR_DEBUG_PANEL_TITLE[] = "Debug Console##ScriptEditorDebugConsolePanel";
    constexpr char SCRIPT_EDITOR_ENTITIES_PANEL_TITLE[] = "Entities##ScriptEditorEntitiesPanel";

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

    void script_context_button(TextEditor *editor, const char *text, const char *idSuffix,
                               const char *typeHint, Entity::EntityId entity)
    {
      const std::string buttonId = std::string(text) + "##" + idSuffix + std::to_string(entity);
      if (ImGui::SmallButton(buttonId.c_str()))
      {
        if (editor)
          editor->InsertText(text);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", typeHint);
    }

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
      scriptEditorShowCodePanel_ = true;
      scriptEditorShowFileTreePanel_ = true;
      scriptEditorShowDebugPanel_ = true;
      scriptEditorShowEntitiesPanel_ = true;
      focusScriptEditorWindow_ = true;
    }
    else
    {
      focusScriptEditorWindow_ = false;
      scriptEditorDockLayoutInitialized_ = false;
      scriptEditorShowCodePanel_ = true;
      scriptEditorShowFileTreePanel_ = true;
      scriptEditorShowDebugPanel_ = true;
      scriptEditorShowEntitiesPanel_ = true;
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
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin(SCRIPT_EDITOR_HOST_WINDOW_TITLE, nullptr, windowFlags))
    {
      ImGui::End();
      ImGui::PopStyleVar(2);
      return;
    }

    const ImGuiID dockspaceId = ImGui::GetID(SCRIPT_EDITOR_DOCKSPACE_ID);
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));

    render_script_editor_menu(entityManager, componentManager);

    if (!scriptEditorDockLayoutInitialized_ && dockspaceId != 0)
    {
      const ImVec2 hostSize = ImGui::GetWindowSize();
      if (hostSize.x <= 0.0f || hostSize.y <= 0.0f)
      {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
      }

      ImGui::DockBuilderRemoveNode(dockspaceId);
      ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspaceId, hostSize);

      ImGuiID mainDockId = dockspaceId;
      ImGuiID fileTreeDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.24f, nullptr, &mainDockId);
      const ImGuiID entitiesDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.28f, nullptr, &mainDockId);
      const ImGuiID debugDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.25f, nullptr, &mainDockId);

      ImGui::DockBuilderDockWindow(SCRIPT_EDITOR_FILE_TREE_PANEL_TITLE, fileTreeDockId);
      ImGui::DockBuilderDockWindow(SCRIPT_EDITOR_PANEL_TITLE, mainDockId);
      ImGui::DockBuilderDockWindow(SCRIPT_EDITOR_DEBUG_PANEL_TITLE, debugDockId);
      ImGui::DockBuilderDockWindow(SCRIPT_EDITOR_ENTITIES_PANEL_TITLE, entitiesDockId);
      ImGui::DockBuilderFinish(dockspaceId);
      scriptEditorDockLayoutInitialized_ = true;
    }

    bool scriptFileTreePanelVisible = scriptEditorShowFileTreePanel_;
    if (scriptFileTreePanelVisible)
    {
      if (ImGui::Begin(SCRIPT_EDITOR_FILE_TREE_PANEL_TITLE, &scriptEditorShowFileTreePanel_))
      {
      if (!workspaceScanError_.empty())
      {
        ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceScanError_.c_str());
        ImGui::Separator();
      }

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
        render_workspace_create_menu(activeWorkspacePath_);
        if (!canModifyWorkspace)
        {
          ImGui::EndDisabled();
        }
        ImGui::EndPopup();
      }
      }
      ImGui::End();
    }

    bool scriptCodePanelVisible = scriptEditorShowCodePanel_;
    if (scriptCodePanelVisible)
    {
      if (ImGui::Begin(SCRIPT_EDITOR_PANEL_TITLE, &scriptEditorShowCodePanel_))
      {
        render_script_editor(entityManager, componentManager);
      }
      ImGui::End();
    }

    bool scriptDebugPanelVisible = scriptEditorShowDebugPanel_;
    if (scriptDebugPanelVisible)
    {
      if (ImGui::Begin(SCRIPT_EDITOR_DEBUG_PANEL_TITLE, &scriptEditorShowDebugPanel_))
      {
        scriptEditorDebugConsole_.render("##ScriptEditorDebugConsoleText");
      }
      ImGui::End();
    }

    bool scriptEntitiesPanelVisible = scriptEditorShowEntitiesPanel_;
    if (scriptEntitiesPanelVisible)
    {
      if (ImGui::Begin(SCRIPT_EDITOR_ENTITIES_PANEL_TITLE, &scriptEditorShowEntitiesPanel_))
      {
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
            const std::string entityTitle = entity_label(entity, componentManager) + "##ScriptEntity" + std::to_string(entity);
            if (ImGui::CollapsingHeader(entityTitle.c_str()))
            {
              ImGui::TextDisabled("Click to insert at cursor");
              ImGui::Indent(8.0f);

              TextEditor *editor = activeTab->textEditor.get();
              script_context_button(editor, "context.EntityId", "eid", "uint, read-only", entity);

              const char *nameHint = componentManager.hasComponent<NameComponent>(entity)
                                         ? "string, read-only"
                                         : "string, read-only (fallback)";
              script_context_button(editor, "context.Name", "name", nameHint, entity);

              script_context_button(editor, "context.Position", "pos", "Vector3, read/write", entity);
              ImGui::Indent(12.0f);
              script_context_button(editor, "context.Position.X", "px", "float", entity);
              script_context_button(editor, "context.Position.Y", "py", "float", entity);
              script_context_button(editor, "context.Position.Z", "pz", "float", entity);
              ImGui::Unindent(12.0f);

              ImGui::Unindent(8.0f);
            }

            if (selected)
            {
              ImGui::SameLine();
              ImGui::TextDisabled("(selected)");
            }
          }
        }
      }
      else
      {
        ImGui::TextDisabled("No script selected.");
      }
      }
      ImGui::End();
    }

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

  void Editor::render_script_editor_menu(EntityManager &entityManager, ComponentManager &componentManager)
  {
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
        if (ImGui::BeginMenu(ICON_FA_FILE "  File"))
        {
          saveRequested = ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save", nullptr, false, canSaveActive);
          saveAllRequested = ImGui::MenuItem(ICON_FA_COPY "  Save All", nullptr, false, !openScriptEditorTabs_.empty());
          revertRequested = ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Revert", nullptr, false, canSaveActive);
          ImGui::Separator();
          closeRequested = ImGui::MenuItem(ICON_FA_XMARK "  Close", nullptr, false, hasActiveTab);
          closeAllRequested = ImGui::MenuItem(ICON_FA_CIRCLE_XMARK "  Close All", nullptr, false, !openScriptEditorTabs_.empty());
          ImGui::Separator();
          closeScriptEditorRequested = ImGui::MenuItem(ICON_FA_CIRCLE_XMARK "  Close Script Editor");
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_HAMMER "  Build"))
        {
          compileRequested = ImGui::MenuItem(ICON_FA_HAMMER "  Compile Workspace", nullptr, false, !activeWorkspacePath_.empty());
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_WINDOW_MAXIMIZE "  Windows"))
        {
          ImGui::MenuItem(ICON_FA_CODE "  Script Editor", nullptr, &scriptEditorShowCodePanel_);
          ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  File Tree", nullptr, &scriptEditorShowFileTreePanel_);
          ImGui::MenuItem(ICON_FA_CHART_LINE "  Debug Console", nullptr, &scriptEditorShowDebugPanel_);
          ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Entities", nullptr, &scriptEditorShowEntitiesPanel_);
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
  }

  void Editor::render_script_editor(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;
    (void)componentManager;

    if (!scriptEditorStatusMessage_.empty() &&
        (scriptEditorStatusMessage_ != lastLoggedScriptEditorStatusMessage_ ||
         scriptEditorStatusIsError_ != lastLoggedScriptEditorStatusIsError_))
    {
      scriptEditorDebugConsole_.add_message(
          scriptEditorStatusIsError_ ? DebugMessageLevel::Error : DebugMessageLevel::Info,
          scriptEditorStatusMessage_);
      lastLoggedScriptEditorStatusMessage_ = scriptEditorStatusMessage_;
      lastLoggedScriptEditorStatusIsError_ = scriptEditorStatusIsError_;
    }

    if (backgroundCompileInProgress_ != lastLoggedCompileInProgress_)
    {
      if (backgroundCompileInProgress_)
      {
        scriptEditorDebugConsole_.add_info("Compiling workspace scripts...");
      }
      lastLoggedCompileInProgress_ = backgroundCompileInProgress_;
    }

    if (!backgroundCompileInProgress_)
    {
      if (scriptCompileStatus_ == ScriptCompileStatus::Failed &&
          (scriptCompileStatus_ != lastLoggedCompileStatus_ || lastCompileError_ != lastLoggedCompileError_))
      {
        scriptEditorDebugConsole_.add_error(lastCompileError_.empty()
                                                ? std::string("Workspace script compilation failed.")
                                                : lastCompileError_);
      }
      else if (scriptCompileStatus_ == ScriptCompileStatus::Succeeded &&
               scriptCompileStatus_ != lastLoggedCompileStatus_)
      {
        scriptEditorDebugConsole_.add_info("Workspace scripts compiled successfully.");
      }

      lastLoggedCompileStatus_ = scriptCompileStatus_;
      lastLoggedCompileError_ = lastCompileError_;
    }

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

    if (openScriptEditorTabs_.empty())
    {
      ImGui::TextDisabled("Open a .cs file from the file tree to start editing.");
      return;
    }

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

}
