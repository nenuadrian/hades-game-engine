#ifndef HADES_ENGINE_RUNTIME_SCRIPT_COMPILER_HPP
#define HADES_ENGINE_RUNTIME_SCRIPT_COMPILER_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace hades
{
  class ScriptCompiler
  {
  public:
    /// Compile the given user .cpp source files into a shared library.
    /// The generated library is written to `outputDir`.
    /// Returns true on success. On failure, sets errorMessage with compiler output.
    bool compile(
        const std::vector<std::filesystem::path> &sourceFiles,
        const std::filesystem::path &outputDir,
        std::string *errorMessage = nullptr);

    /// Returns the path to the last successfully compiled library.
    const std::filesystem::path &libraryPath() const { return libraryPath_; }

  private:
    /// Scan source files for HADES_REGISTER_SCRIPT macros and return class names.
    std::vector<std::string> scanRegisteredClasses(
        const std::vector<std::filesystem::path> &sourceFiles);

    /// Generate the script_registry.cpp aggregation file.
    bool generateRegistry(
        const std::vector<std::string> &classNames,
        const std::filesystem::path &outputDir,
        std::filesystem::path &registryPath,
        std::string *errorMessage);

    std::filesystem::path libraryPath_;
  };
}

#endif
