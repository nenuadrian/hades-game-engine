#ifndef HADES_EDITOR_DEBUG_CONSOLE_PANEL_HPP
#define HADES_EDITOR_DEBUG_CONSOLE_PANEL_HPP

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "types.h"

namespace hades
{

  class DebugConsolePanel
  {
  public:
    explicit DebugConsolePanel(std::size_t maxMessages = 500);

    void add_message(DebugMessageLevel level, const std::string &text);
    void add_info(const std::string &text);
    void add_warning(const std::string &text);
    void add_error(const std::string &text);
    void clear();

    void render(const char *imguiId);

    const std::deque<DebugMessage> &messages() const;
    bool empty() const;
    std::size_t size() const;

  private:
    static std::string format_timestamp(const std::chrono::system_clock::time_point &timestamp);
    static const char *level_prefix(DebugMessageLevel level);

    std::size_t maxMessages_;
    std::deque<DebugMessage> messages_;
    std::string text_;
    std::vector<char> buffer_;
    bool bufferDirty_ = true;
    bool autoScrollPending_ = false;
  };

}

#endif
