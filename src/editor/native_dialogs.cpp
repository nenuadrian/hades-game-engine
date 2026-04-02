#include "native_dialogs.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#else
#include <sys/wait.h>
#endif

namespace hades
{
  namespace
  {
    void set_error_message(std::string *errorMessage, const std::string &message)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = message;
      }
    }

    void clear_error_message(std::string *errorMessage)
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
    }

    std::string trim_copy(std::string_view value)
    {
      std::size_t first = 0;
      while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0)
      {
        ++first;
      }

      std::size_t last = value.size();
      while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
      {
        --last;
      }

      return std::string(value.substr(first, last - first));
    }

#ifdef _WIN32
    std::wstring widen_ascii(const std::string &value)
    {
      return std::wstring(value.begin(), value.end());
    }

    std::optional<std::filesystem::path> pick_path_with_windows_dialog(
        const std::string &prompt,
        const bool pickFolders,
        std::string *errorMessage)
    {
      HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
      const bool shouldUninitialize = SUCCEEDED(initializeResult);
      if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE)
      {
        set_error_message(
            errorMessage,
            pickFolders ? "Unable to initialize the Windows folder picker."
                        : "Unable to initialize the Windows file picker.");
        return std::nullopt;
      }

      IFileDialog *dialog = nullptr;
      const HRESULT createResult = CoCreateInstance(
          CLSID_FileOpenDialog,
          nullptr,
          CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&dialog));
      if (FAILED(createResult) || dialog == nullptr)
      {
        if (shouldUninitialize)
        {
          CoUninitialize();
        }
        set_error_message(
            errorMessage,
            pickFolders ? "Unable to create the Windows folder picker dialog."
                        : "Unable to create the Windows file picker dialog.");
        return std::nullopt;
      }

      DWORD dialogOptions = 0;
      dialog->GetOptions(&dialogOptions);
      dialog->SetOptions(dialogOptions | FOS_FORCEFILESYSTEM | (pickFolders ? FOS_PICKFOLDERS : 0));

      if (!prompt.empty())
      {
        const std::wstring title = widen_ascii(prompt);
        dialog->SetTitle(title.c_str());
      }

      const HRESULT showResult = dialog->Show(nullptr);
      if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED))
      {
        dialog->Release();
        if (shouldUninitialize)
        {
          CoUninitialize();
        }
        clear_error_message(errorMessage);
        return std::nullopt;
      }

      if (FAILED(showResult))
      {
        dialog->Release();
        if (shouldUninitialize)
        {
          CoUninitialize();
        }
        set_error_message(
            errorMessage,
            pickFolders ? "The Windows folder picker failed to open."
                        : "The Windows file picker failed to open.");
        return std::nullopt;
      }

      IShellItem *item = nullptr;
      const HRESULT resultItemStatus = dialog->GetResult(&item);
      dialog->Release();
      if (FAILED(resultItemStatus) || item == nullptr)
      {
        if (shouldUninitialize)
        {
          CoUninitialize();
        }
        set_error_message(
            errorMessage,
            pickFolders ? "The Windows folder picker did not return a folder."
                        : "The Windows file picker did not return a file.");
        return std::nullopt;
      }

      PWSTR rawPath = nullptr;
      const HRESULT pathStatus = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
      item->Release();

      std::optional<std::filesystem::path> selectedPath;
      if (SUCCEEDED(pathStatus) && rawPath != nullptr)
      {
        selectedPath = std::filesystem::path(rawPath);
        CoTaskMemFree(rawPath);
      }

      if (shouldUninitialize)
      {
        CoUninitialize();
      }

      if (!selectedPath.has_value())
      {
        set_error_message(
            errorMessage,
            pickFolders ? "The Windows folder picker did not return a valid filesystem path."
                        : "The Windows file picker did not return a valid filesystem path.");
        return std::nullopt;
      }

      clear_error_message(errorMessage);
      return selectedPath;
    }
#else
    std::optional<std::string> capture_command_output(const std::string &command, int *exitCode = nullptr)
    {
      FILE *pipe = popen(command.c_str(), "r");
      if (pipe == nullptr)
      {
        if (exitCode != nullptr)
        {
          *exitCode = -1;
        }
        return std::nullopt;
      }

      std::string output;
      std::array<char, 256> buffer{};
      while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
      {
        output += buffer.data();
      }

      const int commandStatus = pclose(pipe);
      int commandExitCode = commandStatus;
#if defined(WIFEXITED) && defined(WEXITSTATUS)
      if (commandStatus >= 0 && WIFEXITED(commandStatus))
      {
        commandExitCode = WEXITSTATUS(commandStatus);
      }
#endif
      if (exitCode != nullptr)
      {
        *exitCode = commandExitCode;
      }

      return output;
    }

    std::string shell_single_quote(std::string_view value)
    {
      std::string quoted = "'";
      for (const char character : value)
      {
        if (character == '\'')
        {
          quoted += "'\"'\"'";
        }
        else
        {
          quoted.push_back(character);
        }
      }
      quoted.push_back('\'');
      return quoted;
    }

    std::string applescript_string_literal(std::string_view value)
    {
      std::string escaped;
      escaped.reserve(value.size());
      for (const char character : value)
      {
        if (character == '\\' || character == '"')
        {
          escaped.push_back('\\');
        }
        escaped.push_back(character);
      }
      return escaped;
    }

    std::optional<std::filesystem::path> pick_path_from_command(
        const std::string &command,
        const std::string &launchError,
        const std::string &missingPickerError,
        std::string *errorMessage)
    {
      int exitCode = 0;
      const auto output = capture_command_output(command, &exitCode);
      if (exitCode == 127)
      {
        set_error_message(errorMessage, missingPickerError);
        return std::nullopt;
      }

      if (!output.has_value())
      {
        set_error_message(errorMessage, launchError);
        return std::nullopt;
      }

      const std::string trimmedOutput = trim_copy(*output);
      if (exitCode != 0 || trimmedOutput.empty())
      {
        clear_error_message(errorMessage);
        return std::nullopt;
      }

      clear_error_message(errorMessage);
      return std::filesystem::path(trimmedOutput);
    }
#endif
  }

  std::optional<std::filesystem::path> pick_folder_with_native_dialog(
      const std::string &prompt,
      std::string *errorMessage)
  {
#ifdef _WIN32
    return pick_path_with_windows_dialog(prompt, true, errorMessage);
#elif defined(__APPLE__)
    const std::string script =
        "POSIX path of (choose folder with prompt \"" + applescript_string_literal(prompt) + "\")";
    return pick_path_from_command(
        "osascript -e " + shell_single_quote(script),
        "Unable to launch the macOS folder picker.",
        "Unable to launch the macOS folder picker.",
        errorMessage);
#elif defined(__linux__)
    const std::string script =
        "if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --directory --title=" +
        shell_single_quote(prompt) +
        "; elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getexistingdirectory . --title " +
        shell_single_quote(prompt) +
        "; else exit 127; fi";
    return pick_path_from_command(
        "sh -c " + shell_single_quote(script),
        "Unable to launch the native folder picker.",
        "No native folder picker is available. Enter a folder path manually.",
        errorMessage);
#else
    set_error_message(errorMessage, "This platform does not provide a native folder picker in this build.");
    return std::nullopt;
#endif
  }

  std::optional<std::filesystem::path> pick_file_with_native_dialog(
      const std::string &prompt,
      std::string *errorMessage)
  {
#ifdef _WIN32
    return pick_path_with_windows_dialog(prompt, false, errorMessage);
#elif defined(__APPLE__)
    const std::string script =
        "POSIX path of (choose file with prompt \"" + applescript_string_literal(prompt) + "\")";
    return pick_path_from_command(
        "osascript -e " + shell_single_quote(script),
        "Unable to launch the macOS file picker.",
        "Unable to launch the macOS file picker.",
        errorMessage);
#elif defined(__linux__)
    const std::string script =
        "if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --title=" +
        shell_single_quote(prompt) +
        "; elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getopenfilename . --title " +
        shell_single_quote(prompt) +
        "; else exit 127; fi";
    return pick_path_from_command(
        "sh -c " + shell_single_quote(script),
        "Unable to launch the native file picker.",
        "No native file picker is available. Enter a file path manually.",
        errorMessage);
#else
    set_error_message(errorMessage, "This platform does not provide a native file picker in this build.");
    return std::nullopt;
#endif
  }
}
