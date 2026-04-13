#ifndef HADES_ENGINE_RUNTIME_SUBPROCESS_HPP
#define HADES_ENGINE_RUNTIME_SUBPROCESS_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hades
{
  struct ProcessResult
  {
    bool launched = false;
    int exitCode = -1;
    std::string output;
  };

  class Subprocess
  {
  public:
    Subprocess();
    ~Subprocess();

    Subprocess(Subprocess &&other) noexcept;
    Subprocess &operator=(Subprocess &&other) noexcept;

    Subprocess(const Subprocess &) = delete;
    Subprocess &operator=(const Subprocess &) = delete;

    static ProcessResult run_capture(
        const std::vector<std::string> &args,
        const std::filesystem::path &workingDirectory = {});

    bool start(
        const std::vector<std::string> &args,
        const std::filesystem::path &workingDirectory = {},
        std::string *errorMessage = nullptr);

    bool write_line(const std::string &line, std::string *errorMessage = nullptr);
    bool read_line(std::string &line, std::string *errorMessage = nullptr);
    void stop();
    bool is_running() const;

    // Wait for the process to exit and return the exit code.
    // Returns -1 if the process was never started or already stopped.
    int wait_for_exit();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
