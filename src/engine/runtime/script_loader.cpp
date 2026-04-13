#include "script_loader.hpp"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hades
{
  struct ScriptLoader::Impl
  {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void *handle = nullptr;
#endif
    std::unordered_map<std::string, HadesScript *(*) ()> factories;
  };

  ScriptLoader::ScriptLoader() : impl_(std::make_unique<Impl>()) {}

  ScriptLoader::~ScriptLoader()
  {
    unload();
  }

  ScriptLoader::ScriptLoader(ScriptLoader &&other) noexcept = default;
  ScriptLoader &ScriptLoader::operator=(ScriptLoader &&other) noexcept = default;

  bool ScriptLoader::load(const std::filesystem::path &libraryPath, std::string *errorMessage)
  {
    unload();

#ifdef _WIN32
    impl_->handle = LoadLibraryW(libraryPath.wstring().c_str());
    if (impl_->handle == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to load script library: " + libraryPath.string() +
                        " (error code " + std::to_string(GetLastError()) + ")";
      }
      return false;
    }

    auto *factories = reinterpret_cast<const ScriptFactoryEntry *>(
        GetProcAddress(impl_->handle, "hades_script_factories"));
    auto *countPtr = reinterpret_cast<const int *>(
        GetProcAddress(impl_->handle, "hades_script_factory_count"));
#else
    impl_->handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (impl_->handle == nullptr)
    {
      if (errorMessage != nullptr)
      {
        const char *err = dlerror();
        *errorMessage = "Failed to load script library: " + libraryPath.string();
        if (err != nullptr)
        {
          *errorMessage += "\n";
          *errorMessage += err;
        }
      }
      return false;
    }

    auto *factories = reinterpret_cast<const ScriptFactoryEntry *>(
        dlsym(impl_->handle, "hades_script_factories"));
    auto *countPtr = reinterpret_cast<const int *>(
        dlsym(impl_->handle, "hades_script_factory_count"));
#endif

    if (factories == nullptr || countPtr == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Script library is missing the factory table. "
                        "Ensure all scripts use HADES_REGISTER_SCRIPT.";
      }
      unload();
      return false;
    }

    const int count = *countPtr;
    for (int i = 0; i < count; ++i)
    {
      if (factories[i].className != nullptr && factories[i].createFn != nullptr)
      {
        impl_->factories[factories[i].className] = factories[i].createFn;
      }
    }

    return true;
  }

  void ScriptLoader::unload()
  {
    impl_->factories.clear();

    if (impl_->handle != nullptr)
    {
#ifdef _WIN32
      FreeLibrary(impl_->handle);
#else
      dlclose(impl_->handle);
#endif
      impl_->handle = nullptr;
    }
  }

  bool ScriptLoader::isLoaded() const
  {
    return impl_->handle != nullptr;
  }

  HadesScript *ScriptLoader::createScript(const std::string &className) const
  {
    const auto it = impl_->factories.find(className);
    if (it == impl_->factories.end())
    {
      return nullptr;
    }

    return it->second();
  }

  std::vector<std::string> ScriptLoader::availableScripts() const
  {
    std::vector<std::string> names;
    names.reserve(impl_->factories.size());
    for (const auto &[name, fn] : impl_->factories)
    {
      names.push_back(name);
    }
    return names;
  }
}
