#include "subprocess.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace hades
{
  namespace
  {
#ifdef _WIN32
    std::wstring utf8_to_wide(const std::string &value)
    {
      if (value.empty())
      {
        return std::wstring();
      }

      const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
      if (required <= 0)
      {
        return std::wstring();
      }

      std::wstring converted(static_cast<std::size_t>(required), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), required);
      converted.pop_back();
      return converted;
    }

    std::wstring quote_windows_argument(const std::string &argument)
    {
      const std::wstring wideArgument = utf8_to_wide(argument);
      if (wideArgument.empty())
      {
        return L"\"\"";
      }

      bool needsQuotes = false;
      for (wchar_t ch : wideArgument)
      {
        if (ch == L' ' || ch == L'\t' || ch == L'"')
        {
          needsQuotes = true;
          break;
        }
      }

      if (!needsQuotes)
      {
        return wideArgument;
      }

      std::wstring quoted = L"\"";
      int consecutiveBackslashes = 0;
      for (wchar_t ch : wideArgument)
      {
        if (ch == L'\\')
        {
          ++consecutiveBackslashes;
          continue;
        }

        if (ch == L'"')
        {
          quoted.append(static_cast<std::size_t>((consecutiveBackslashes * 2) + 1), L'\\');
          quoted.push_back(L'"');
          consecutiveBackslashes = 0;
          continue;
        }

        if (consecutiveBackslashes > 0)
        {
          quoted.append(static_cast<std::size_t>(consecutiveBackslashes), L'\\');
          consecutiveBackslashes = 0;
        }

        quoted.push_back(ch);
      }

      if (consecutiveBackslashes > 0)
      {
        quoted.append(static_cast<std::size_t>(consecutiveBackslashes * 2), L'\\');
      }

      quoted.push_back(L'"');
      return quoted;
    }

    std::wstring build_windows_command_line(const std::vector<std::string> &args)
    {
      std::wstring commandLine;
      for (std::size_t index = 0; index < args.size(); ++index)
      {
        if (index > 0)
        {
          commandLine.push_back(L' ');
        }
        commandLine += quote_windows_argument(args[index]);
      }

      return commandLine;
    }
#endif
  }

  struct Subprocess::Impl
  {
#ifdef _WIN32
    HANDLE processHandle = nullptr;
    HANDLE threadHandle = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
#else
    pid_t pid = -1;
    int stdinWrite = -1;
    int stdoutRead = -1;
#endif
    bool running = false;
    std::string readBuffer;
  };

  Subprocess::Subprocess() : impl_(std::make_unique<Impl>()) {}

  Subprocess::~Subprocess()
  {
    stop();
  }

  Subprocess::Subprocess(Subprocess &&other) noexcept = default;
  Subprocess &Subprocess::operator=(Subprocess &&other) noexcept = default;

  ProcessResult Subprocess::run_capture(
      const std::vector<std::string> &args,
      const std::filesystem::path &workingDirectory)
  {
    ProcessResult result;
    std::string errorMessage;

    Subprocess process;
    result.launched = process.start(args, workingDirectory, &errorMessage);
    if (!result.launched)
    {
      result.output = errorMessage;
      return result;
    }

    std::string line;
    while (process.read_line(line, nullptr))
    {
      result.output += line;
      result.output.push_back('\n');
    }

#ifdef _WIN32
    DWORD exitCode = 0;
    if (process.impl_->processHandle != nullptr)
    {
      WaitForSingleObject(process.impl_->processHandle, INFINITE);
      GetExitCodeProcess(process.impl_->processHandle, &exitCode);
    }
    result.exitCode = static_cast<int>(exitCode);
#else
    int status = 0;
    if (process.impl_->pid >= 0)
    {
      waitpid(process.impl_->pid, &status, 0);
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

    process.impl_->running = false;
    process.stop();
    return result;
  }

  bool Subprocess::start(
      const std::vector<std::string> &args,
      const std::filesystem::path &workingDirectory,
      std::string *errorMessage)
  {
    stop();

    if (args.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Cannot start a process without a command.";
      }
      return false;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;

    if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to create child process pipes.";
      }

      if (stdoutRead != nullptr)
      {
        CloseHandle(stdoutRead);
      }
      if (stdoutWrite != nullptr)
      {
        CloseHandle(stdoutWrite);
      }
      if (stdinRead != nullptr)
      {
        CloseHandle(stdinRead);
      }
      if (stdinWrite != nullptr)
      {
        CloseHandle(stdinWrite);
      }
      return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = stdoutWrite;

    PROCESS_INFORMATION processInformation{};
    std::wstring commandLine = build_windows_command_line(args);
    std::wstring workingDirectoryWide =
        workingDirectory.empty() ? std::wstring() : utf8_to_wide(workingDirectory.u8string());

    BOOL created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectoryWide.c_str(),
        &startupInfo,
        &processInformation);

    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);

    if (!created)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to launch child process.";
      }
      CloseHandle(stdoutRead);
      CloseHandle(stdinWrite);
      return false;
    }

    impl_->processHandle = processInformation.hProcess;
    impl_->threadHandle = processInformation.hThread;
    impl_->stdinWrite = stdinWrite;
    impl_->stdoutRead = stdoutRead;
#else
    std::signal(SIGPIPE, SIG_IGN);

    int stdoutPipe[2] = {-1, -1};
    int stdinPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0 || pipe(stdinPipe) != 0)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::string("Failed to create child process pipes: ") + std::strerror(errno);
      }

      if (stdoutPipe[0] >= 0)
      {
        close(stdoutPipe[0]);
      }
      if (stdoutPipe[1] >= 0)
      {
        close(stdoutPipe[1]);
      }
      if (stdinPipe[0] >= 0)
      {
        close(stdinPipe[0]);
      }
      if (stdinPipe[1] >= 0)
      {
        close(stdinPipe[1]);
      }
      return false;
    }

    const pid_t child = fork();
    if (child < 0)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::string("Failed to fork child process: ") + std::strerror(errno);
      }

      close(stdoutPipe[0]);
      close(stdoutPipe[1]);
      close(stdinPipe[0]);
      close(stdinPipe[1]);
      return false;
    }

    if (child == 0)
    {
      if (!workingDirectory.empty())
      {
        chdir(workingDirectory.c_str());
      }

      dup2(stdinPipe[0], STDIN_FILENO);
      dup2(stdoutPipe[1], STDOUT_FILENO);
      dup2(stdoutPipe[1], STDERR_FILENO);

      close(stdinPipe[0]);
      close(stdinPipe[1]);
      close(stdoutPipe[0]);
      close(stdoutPipe[1]);

      std::vector<char *> argv;
      argv.reserve(args.size() + 1);
      for (const auto &argument : args)
      {
        argv.push_back(const_cast<char *>(argument.c_str()));
      }
      argv.push_back(nullptr);

      execvp(argv[0], argv.data());
      _exit(127);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    impl_->pid = child;
    impl_->stdinWrite = stdinPipe[1];
    impl_->stdoutRead = stdoutPipe[0];
#endif

    impl_->running = true;
    impl_->readBuffer.clear();
    return true;
  }

  bool Subprocess::write_line(const std::string &line, std::string *errorMessage)
  {
    if (!impl_->running)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Cannot write to a stopped process.";
      }
      return false;
    }

    std::string payload = line;
    payload.push_back('\n');

#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(impl_->stdinWrite, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) ||
        written != payload.size())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to write to child process stdin.";
      }
      return false;
    }
#else
    std::size_t totalWritten = 0;
    while (totalWritten < payload.size())
    {
      const ssize_t bytesWritten = write(
          impl_->stdinWrite,
          payload.data() + totalWritten,
          payload.size() - totalWritten);
      if (bytesWritten <= 0)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to write to child process stdin.";
        }
        return false;
      }
      totalWritten += static_cast<std::size_t>(bytesWritten);
    }
#endif

    return true;
  }

  bool Subprocess::read_line(std::string &line, std::string *errorMessage)
  {
    line.clear();

    if (!impl_->running)
    {
      return false;
    }

    while (true)
    {
      const std::size_t newlinePos = impl_->readBuffer.find('\n');
      if (newlinePos != std::string::npos)
      {
        line = impl_->readBuffer.substr(0, newlinePos);
        if (!line.empty() && line.back() == '\r')
        {
          line.pop_back();
        }
        impl_->readBuffer.erase(0, newlinePos + 1);
        return true;
      }

      char buffer[1024];
      bool readFailed = false;
      std::size_t bytesRead = 0;

#ifdef _WIN32
      DWORD chunkBytesRead = 0;
      if (!ReadFile(impl_->stdoutRead, buffer, static_cast<DWORD>(sizeof(buffer)), &chunkBytesRead, nullptr))
      {
        const DWORD lastError = GetLastError();
        if (lastError == ERROR_BROKEN_PIPE)
        {
          chunkBytesRead = 0;
        }
        else
        {
          readFailed = true;
        }
      }
      bytesRead = static_cast<std::size_t>(chunkBytesRead);
#else
      const ssize_t chunkBytesRead = read(impl_->stdoutRead, buffer, sizeof(buffer));
      if (chunkBytesRead < 0)
      {
        readFailed = true;
      }
      else
      {
        bytesRead = static_cast<std::size_t>(chunkBytesRead);
      }
#endif

      if (readFailed)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to read from child process stdout.";
        }
        impl_->running = false;
        return false;
      }

      if (bytesRead == 0)
      {
        impl_->running = false;
        if (!impl_->readBuffer.empty())
        {
          line = impl_->readBuffer;
          impl_->readBuffer.clear();
          return true;
        }
        return false;
      }

      impl_->readBuffer.append(buffer, bytesRead);
    }
  }

  void Subprocess::stop()
  {
#ifdef _WIN32
    if (impl_->stdinWrite != nullptr)
    {
      CloseHandle(impl_->stdinWrite);
      impl_->stdinWrite = nullptr;
    }
    if (impl_->stdoutRead != nullptr)
    {
      CloseHandle(impl_->stdoutRead);
      impl_->stdoutRead = nullptr;
    }
    if (impl_->processHandle != nullptr)
    {
      DWORD exitCode = STILL_ACTIVE;
      if (GetExitCodeProcess(impl_->processHandle, &exitCode) && exitCode == STILL_ACTIVE)
      {
        TerminateProcess(impl_->processHandle, 1);
      }
      WaitForSingleObject(impl_->processHandle, INFINITE);
      CloseHandle(impl_->processHandle);
      impl_->processHandle = nullptr;
    }
    if (impl_->threadHandle != nullptr)
    {
      CloseHandle(impl_->threadHandle);
      impl_->threadHandle = nullptr;
    }
#else
    if (impl_->stdinWrite >= 0)
    {
      close(impl_->stdinWrite);
      impl_->stdinWrite = -1;
    }
    if (impl_->stdoutRead >= 0)
    {
      close(impl_->stdoutRead);
      impl_->stdoutRead = -1;
    }
    if (impl_->pid >= 0)
    {
      int status = 0;
      const pid_t waited = waitpid(impl_->pid, &status, WNOHANG);
      if (waited == 0)
      {
        kill(impl_->pid, SIGTERM);
        waitpid(impl_->pid, &status, 0);
      }
      impl_->pid = -1;
    }
#endif

    impl_->running = false;
    impl_->readBuffer.clear();
  }

  bool Subprocess::is_running() const
  {
    return impl_->running;
  }

  int Subprocess::wait_for_exit()
  {
    // Drain any remaining output so the pipe doesn't block the child.
    std::string line;
    while (read_line(line))
    {
    }

    int exitCode = -1;
#ifdef _WIN32
    if (impl_->processHandle != nullptr)
    {
      WaitForSingleObject(impl_->processHandle, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(impl_->processHandle, &code);
      exitCode = static_cast<int>(code);
    }
#else
    if (impl_->pid >= 0)
    {
      int status = 0;
      waitpid(impl_->pid, &status, 0);
      exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif

    impl_->running = false;
    return exitCode;
  }
}
