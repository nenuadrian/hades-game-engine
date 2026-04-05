#include "script_autocomplete.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "TextEditor.h"

namespace hades
{
  namespace
  {
    bool is_identifier_char(char c)
    {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    // Expand tab characters to spaces matching tab-stop alignment.
    // This makes string indices correspond 1:1 with visual columns.
    std::string expand_tabs(const std::string &text, int tabSize)
    {
      std::string result;
      result.reserve(text.size());
      int col = 0;
      for (char c : text)
      {
        if (c == '\t')
        {
          int spaces = tabSize - (col % tabSize);
          result.append(spaces, ' ');
          col += spaces;
        }
        else
        {
          result += c;
          ++col;
        }
      }
      return result;
    }

    std::string extract_partial_word(const std::string &expandedLineText, int cursorColumn)
    {
      if (cursorColumn <= 0 || cursorColumn > static_cast<int>(expandedLineText.size()))
        return {};

      int start = cursorColumn;
      while (start > 0 && is_identifier_char(expandedLineText[start - 1]))
        --start;

      return expandedLineText.substr(start, cursorColumn - start);
    }

    int find_word_start_column(const std::string &expandedLineText, int cursorColumn)
    {
      if (cursorColumn <= 0 || cursorColumn > static_cast<int>(expandedLineText.size()))
        return cursorColumn;

      int start = cursorColumn;
      while (start > 0 && is_identifier_char(expandedLineText[start - 1]))
        --start;

      return start;
    }

    bool case_insensitive_prefix(const std::string &candidate, const std::string &prefix)
    {
      if (prefix.size() > candidate.size())
        return false;
      for (std::size_t i = 0; i < prefix.size(); ++i)
      {
        if (std::tolower(static_cast<unsigned char>(candidate[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
          return false;
      }
      return true;
    }

    const std::vector<std::string> &get_static_autocomplete_words()
    {
      static const std::vector<std::string> words = {
          // C# keywords
          "abstract", "as", "async", "await", "base", "bool", "break", "byte",
          "case", "catch", "char", "checked", "class", "const", "continue",
          "decimal", "default", "delegate", "do", "double", "dynamic", "else",
          "enum", "event", "explicit", "extern", "false", "finally", "fixed",
          "float", "for", "foreach", "get", "goto", "if", "implicit", "in",
          "int", "interface", "internal", "is", "lock", "long", "nameof",
          "namespace", "new", "null", "object", "operator", "out", "override",
          "params", "partial", "private", "protected", "public", "readonly",
          "ref", "return", "sbyte", "sealed", "set", "short", "sizeof",
          "stackalloc", "static", "string", "struct", "switch", "this", "throw",
          "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe",
          "ushort", "using", "var", "virtual", "void", "volatile", "where",
          "while", "yield",
          // .NET built-in types
          "Console", "Math", "String", "Int32", "Int64", "Boolean", "Object",
          "Exception", "List", "Dictionary", "Task", "Action", "Func",
          "IEnumerable", "IDisposable", "EventArgs",
          // Hades engine API
          "HadesScript", "EntityContext", "Vector3", "Quaternion",
          "GameObject", "Transform", "Debug",
          "OnStart", "OnUpdate", "OnKeyDown", "OnKeyUp",
          "EntityId", "Name", "Position",
      };
      return words;
    }

    std::vector<std::string> filter_autocomplete_candidates(
        const std::string &prefix,
        const std::vector<std::string> &staticWords,
        const std::unordered_map<std::string, std::vector<ParsedScriptClass>> &parsedScriptCache,
        std::size_t maxCandidates)
    {
      std::vector<std::string> results;
      std::unordered_set<std::string> seen;

      auto try_add = [&](const std::string &word)
      {
        if (word == prefix)
          return;
        if (!case_insensitive_prefix(word, prefix))
          return;
        if (seen.insert(word).second)
          results.push_back(word);
      };

      for (const auto &word : staticWords)
        try_add(word);

      for (const auto &[path, classes] : parsedScriptCache)
      {
        for (const auto &cls : classes)
        {
          try_add(cls.simpleName);
          for (const auto &[type, name] : cls.publicFields)
          {
            try_add(type);
            try_add(name);
          }
        }
      }

      std::sort(results.begin(), results.end(), [&prefix](const std::string &a, const std::string &b)
                {
        bool aExact = (a.substr(0, prefix.size()) == prefix);
        bool bExact = (b.substr(0, prefix.size()) == prefix);
        if (aExact != bExact) return aExact;
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b; });

      if (results.size() > maxCandidates)
        results.resize(maxCandidates);

      return results;
    }
  } // anonymous namespace

  // ── ScriptAutoComplete implementation ────────────────────────────────

  void ScriptAutoComplete::reset()
  {
    open_ = false;
    selectedIndex_ = 0;
    prefix_.clear();
    candidates_.clear();
    wordStartColumn_ = 0;
    justAccepted_ = false;
  }

  void ScriptAutoComplete::acceptCompletion(TextEditor &editor, int candidateIndex)
  {
    if (candidateIndex < 0 || candidateIndex >= static_cast<int>(candidates_.size()))
      return;

    const auto pos = editor.GetCursorPosition();
    const auto start = TextEditor::Coordinates(pos.mLine, wordStartColumn_);
    editor.SetSelection(start, pos);
    editor.Delete();
    editor.InsertText(candidates_[candidateIndex]);

    open_ = false;
    selectedIndex_ = 0;
    justAccepted_ = true;
  }

  bool ScriptAutoComplete::handleKeys(TextEditor &editor)
  {
    if (!open_ || candidates_.empty())
      return false;

    if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
      acceptCompletion(editor, selectedIndex_);
      return true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      open_ = false;
      return true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
      selectedIndex_ = std::max(selectedIndex_ - 1, 0);
      return true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
      selectedIndex_ = std::min(
          selectedIndex_,
          static_cast<int>(candidates_.size()) - 1);
      ++selectedIndex_;
      selectedIndex_ = std::min(
          selectedIndex_,
          static_cast<int>(candidates_.size()) - 1);
      return true;
    }

    return false;
  }

  void ScriptAutoComplete::update(
      const TextEditor &editor,
      const std::unordered_map<std::string, std::vector<ParsedScriptClass>> &parsedScriptCache)
  {
    // Suppress reopening for one frame after accepting a completion.
    if (justAccepted_)
    {
      justAccepted_ = false;
      return;
    }

    const auto cursorPos = editor.GetCursorPosition();
    const std::string lineText = editor.GetCurrentLineText();
    const std::string expanded = expand_tabs(lineText, kTabSize);
    const std::string partial = extract_partial_word(expanded, cursorPos.mColumn);
    const int wordStartCol = find_word_start_column(expanded, cursorPos.mColumn);

    if (static_cast<int>(partial.size()) >= kMinPrefixLength)
    {
      const auto &staticWords = get_static_autocomplete_words();
      auto newCandidates = filter_autocomplete_candidates(partial, staticWords, parsedScriptCache, kMaxCandidates);

      if (!newCandidates.empty())
      {
        open_ = true;
        prefix_ = partial;
        candidates_ = std::move(newCandidates);
        wordStartColumn_ = wordStartCol;

        if (selectedIndex_ >= static_cast<int>(candidates_.size()))
          selectedIndex_ = 0;
      }
      else
      {
        open_ = false;
      }
    }
    else
    {
      open_ = false;
    }
  }

  void ScriptAutoComplete::renderPopup(TextEditor &editor)
  {
    if (!open_ || candidates_.empty())
      return;

    const auto cursorPos = editor.GetCursorPosition();

    // Compute cursor screen position using the editor widget bounds.
    const ImVec2 editorWidgetMin = ImGui::GetItemRectMin();
    const ImVec2 editorWidgetMax = ImGui::GetItemRectMax();

    const float fontSize = ImGui::GetFontSize();
    const float charWidth = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, " ").x;
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();

    // Compute the left margin (line numbers gutter) the same way TextEditor does.
    char lineCountBuf[16];
    snprintf(lineCountBuf, sizeof(lineCountBuf), " %d ", editor.GetTotalLines());
    const float textStart = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, lineCountBuf).x + 10.0f;

    // Read scroll offset from the editor's child window.
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    ImGuiWindow *parentWindow = ImGui::GetCurrentWindow();
    const ImGuiID childId = parentWindow->GetID("##ScriptEditorContents");
    char childWindowName[256];
    snprintf(childWindowName, sizeof(childWindowName), "%s/%08X", parentWindow->Name, childId);
    ImGuiWindow *childWindow = ImGui::FindWindowByName(childWindowName);
    if (childWindow)
    {
      scrollX = childWindow->Scroll.x;
      scrollY = childWindow->Scroll.y;
    }

    const float cursorScreenX = editorWidgetMin.x + textStart + cursorPos.mColumn * charWidth - scrollX;
    const float cursorScreenY = editorWidgetMin.y + cursorPos.mLine * lineHeight - scrollY + lineHeight;

    // Clamp popup position within the editor bounds.
    float popupX = std::max(editorWidgetMin.x, std::min(cursorScreenX, editorWidgetMax.x - kPopupWidth));
    float popupY = cursorScreenY;

    // If popup would go below the editor, show it above the cursor instead.
    if (popupY + kPopupMaxHeight > editorWidgetMax.y)
      popupY = cursorScreenY - lineHeight - kPopupMaxHeight;

    // Draw the popup using the foreground draw list (always on top).
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.18f, 0.20f, 0.96f));
    const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.45f, 0.45f, 0.50f, 0.80f));
    const ImU32 selectedBgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.26f, 0.42f, 0.65f, 0.80f));
    const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.90f, 0.90f, 0.90f, 1.0f));

    const float itemHeight = lineHeight;
    const int candidateCount = static_cast<int>(candidates_.size());
    const float contentHeight = candidateCount * itemHeight + kItemPadY * 2.0f;
    const float boxHeight = std::min(contentHeight, kPopupMaxHeight);

    const ImVec2 boxMin(popupX, popupY);
    const ImVec2 boxMax(popupX + kPopupWidth, popupY + boxHeight);

    // Background and border.
    drawList->AddRectFilled(boxMin, boxMax, bgColor, 4.0f);
    drawList->AddRect(boxMin, boxMax, borderColor, 4.0f);

    // Draw each candidate.
    const ImVec2 mousePos = ImGui::GetMousePos();
    int clickedIndex = -1;

    for (int i = 0; i < candidateCount; ++i)
    {
      const float itemY = popupY + kItemPadY + i * itemHeight;
      if (itemY + itemHeight < boxMin.y || itemY > boxMax.y)
        continue;

      const ImVec2 itemMin(popupX, itemY);
      const ImVec2 itemMax(popupX + kPopupWidth, itemY + itemHeight);

      // Hover detection.
      const bool hovered = mousePos.x >= itemMin.x && mousePos.x < itemMax.x &&
                            mousePos.y >= itemMin.y && mousePos.y < itemMax.y;
      if (hovered)
        selectedIndex_ = i;

      if (i == selectedIndex_)
        drawList->AddRectFilled(itemMin, itemMax, selectedBgColor, 2.0f);

      drawList->AddText(ImVec2(popupX + kItemPadX, itemY + 1.0f), textColor,
                        candidates_[i].c_str());

      if (hovered && ImGui::IsMouseClicked(0))
        clickedIndex = i;
    }

    // Handle click to accept.
    if (clickedIndex >= 0 && clickedIndex < candidateCount)
    {
      acceptCompletion(editor, clickedIndex);
    }
  }

} // namespace hades
