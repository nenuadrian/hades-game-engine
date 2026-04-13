#ifndef HADES_ENGINE_RUNTIME_SCRIPT_LOADER_HPP
#define HADES_ENGINE_RUNTIME_SCRIPT_LOADER_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hades_script.hpp"
#include "hades_script_registration.hpp"

namespace hades
{
  class ScriptLoader
  {
  public:
    ScriptLoader();
    ~ScriptLoader();

    ScriptLoader(ScriptLoader &&other) noexcept;
    ScriptLoader &operator=(ScriptLoader &&other) noexcept;

    ScriptLoader(const ScriptLoader &) = delete;
    ScriptLoader &operator=(const ScriptLoader &) = delete;

    bool load(const std::filesystem::path &libraryPath, std::string *errorMessage = nullptr);
    void unload();
    bool isLoaded() const;

    /// Create an instance of the named script class. Returns nullptr if not found.
    HadesScript *createScript(const std::string &className) const;

    /// List all available script class names from the loaded library.
    std::vector<std::string> availableScripts() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
