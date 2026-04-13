#ifndef HADES_ENGINE_RUNTIME_SCRIPTING_SDK_HPP
#define HADES_ENGINE_RUNTIME_SCRIPTING_SDK_HPP

#include <filesystem>
#include <string>

namespace hades
{
  /// Ensure that the Hades.Scripting reference assembly (DLL) has been built
  /// and is available for IntelliSense and compilation.  On success the
  /// returned path points to Hades.Scripting.dll.  On failure the returned
  /// path is empty and errorMessage (when non-null) explains why.
  std::filesystem::path ensure_scripting_sdk(std::string *errorMessage = nullptr);

  /// Return the canonical C# source for the Hades scripting API types
  /// (Vector3, EntityContext, HadesScript, HadesAPI).
  const char *scripting_api_source();
}

#endif
