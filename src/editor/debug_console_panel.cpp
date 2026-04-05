#include "debug_console_panel.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "imgui.h"

namespace hades
{

  DebugConsolePanel::DebugConsolePanel(std::size_t maxMessages)
      : maxMessages_(maxMessages)
  {
  }

  void DebugConsolePanel::add_message(DebugMessageLevel level, const std::string &text)
  {
    if (text.empty())
    {
      return;
    }

    messages_.push_back(
        DebugMessage{level, text, std::chrono::steady_clock::now(), std::chrono::system_clock::now()});

    while (messages_.size() > maxMessages_)
    {
      messages_.pop_front();
    }

    bufferDirty_ = true;
    autoScrollPending_ = true;
  }

  void DebugConsolePanel::add_info(const std::string &text)
  {
    add_message(DebugMessageLevel::Info, text);
  }

  void DebugConsolePanel::add_warning(const std::string &text)
  {
    add_message(DebugMessageLevel::Warning, text);
  }

  void DebugConsolePanel::add_error(const std::string &text)
  {
    add_message(DebugMessageLevel::Error, text);
  }

  void DebugConsolePanel::clear()
  {
    messages_.clear();
    text_.clear();
    buffer_.clear();
    bufferDirty_ = true;
  }

  void DebugConsolePanel::render(const char *imguiId)
  {
    if (ImGui::Button("Clear"))
    {
      clear();
    }

    ImGui::Separator();

    if (bufferDirty_)
    {
      std::ostringstream stream;
      for (const auto &message : messages_)
      {
        stream << '[' << format_timestamp(message.wallClockTimestamp) << "] "
               << '[' << level_prefix(message.level) << "] "
               << message.text
               << '\n';
      }

      text_ = stream.str();
      buffer_.assign(text_.begin(), text_.end());
      buffer_.push_back('\0');
      bufferDirty_ = false;
    }

    if (buffer_.empty() || (buffer_.size() == 1 && buffer_[0] == '\0'))
    {
      ImGui::TextDisabled("No messages yet.");
    }
    else
    {
      ImGui::InputTextMultiline(
          imguiId,
          buffer_.data(),
          buffer_.size(),
          ImVec2(-1.0f, -1.0f),
          ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);

      if (autoScrollPending_)
      {
        ImGui::SetScrollHereY(1.0f);
        autoScrollPending_ = false;
      }
    }
  }

  const std::deque<DebugMessage> &DebugConsolePanel::messages() const
  {
    return messages_;
  }

  bool DebugConsolePanel::empty() const
  {
    return messages_.empty();
  }

  std::size_t DebugConsolePanel::size() const
  {
    return messages_.size();
  }

  std::string DebugConsolePanel::format_timestamp(const std::chrono::system_clock::time_point &timestamp)
  {
    const std::time_t clockTime = std::chrono::system_clock::to_time_t(timestamp);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &clockTime);
#else
    localtime_r(&clockTime, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
  }

  const char *DebugConsolePanel::level_prefix(DebugMessageLevel level)
  {
    switch (level)
    {
    case DebugMessageLevel::Warning:
      return "WARNING";
    case DebugMessageLevel::Error:
      return "ERROR";
    default:
      return "INFO";
    }
  }

}
