#ifndef HADES_EDITOR_SCRIPT_AUTOCOMPLETE_HPP
#define HADES_EDITOR_SCRIPT_AUTOCOMPLETE_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "script_analysis.hpp"

class TextEditor;

namespace hades
{
  class ScriptAutoComplete
  {
  public:
    ScriptAutoComplete() = default;

    // Called before TextEditor::Render. Returns true if a key was consumed
    // (caller should disable TextEditor keyboard input for that frame).
    bool handleKeys(TextEditor &editor);

    // Called after TextEditor::Render. Updates candidates based on cursor position.
    void update(
        const TextEditor &editor,
        const std::unordered_map<std::string, std::vector<ParsedScriptClass>> &parsedScriptCache);

    // Called after update. Renders the autocomplete popup overlay.
    void renderPopup(TextEditor &editor);

    // Reset all state (call when switching tabs, closing editor, or switching workspace).
    void reset();

    bool isOpen() const { return open_; }

  private:
    static constexpr int kTabSize = 4;
    static constexpr std::size_t kMaxCandidates = 12;
    static constexpr int kMinPrefixLength = 2;
    static constexpr float kPopupWidth = 250.0f;
    static constexpr float kPopupMaxHeight = 200.0f;
    static constexpr float kItemPadX = 6.0f;
    static constexpr float kItemPadY = 4.0f;

    bool open_ = false;
    int selectedIndex_ = 0;
    std::string prefix_;
    std::vector<std::string> candidates_;
    int wordStartColumn_ = 0;
    bool justAccepted_ = false;

    void acceptCompletion(TextEditor &editor, int candidateIndex);
  };
}

#endif
