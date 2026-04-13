#ifndef HADES_ENGINE_RUNTIME_PROJECT_GENERATOR_HPP
#define HADES_ENGINE_RUNTIME_PROJECT_GENERATOR_HPP

#include <filesystem>
#include <string>

namespace hades
{
  /// Generate (or regenerate) a .csproj and .sln in the workspace so that
  /// external editors can provide IntelliSense for user C# scripts.
  ///
  /// Files produced:
  ///   <workspace>/.hades/scripting/HadesScripts.csproj
  ///   <workspace>/HadesScripts.sln
  ///
  /// The csproj uses a glob pattern to discover all .cs files in the workspace,
  /// so it does not need to be regenerated when scripts are added or removed.
  ///
  /// \param workspacePath  Absolute path to the workspace root.
  /// \param sdkDllPath     Path to Hades.Scripting.dll (from ensure_scripting_sdk).
  /// \param errorMessage   Optional error output.
  /// \return true on success.
  bool generate_workspace_project(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &sdkDllPath,
      std::string *errorMessage = nullptr);
}

#endif
