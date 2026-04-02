#include "clr_host.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hades
{
  namespace
  {
    // hostfxr type definitions matching the official .NET hosting API.
    using char_t = char;

    enum hostfxr_delegate_type
    {
      hdt_com_activation = 0,
      hdt_load_in_memory_assembly = 1,
      hdt_winrt_activation = 2,
      hdt_com_register = 3,
      hdt_com_unregister = 4,
      hdt_load_assembly_and_get_function_pointer = 5,
      hdt_get_function_pointer = 6,
    };

    using hostfxr_handle = void *;
    using hostfxr_initialize_for_runtime_config_fn =
        int32_t (*)(const char_t *runtime_config_path,
                    const void *parameters,
                    hostfxr_handle *host_context_handle);
    using hostfxr_get_runtime_delegate_fn =
        int32_t (*)(const hostfxr_handle host_context_handle,
                    hostfxr_delegate_type type,
                    void **delegate);
    using hostfxr_close_fn =
        int32_t (*)(const hostfxr_handle host_context_handle);

    using load_assembly_and_get_function_pointer_fn =
        int32_t (*)(const char_t *assembly_path,
                    const char_t *type_name,
                    const char_t *method_name,
                    const char_t *delegate_type_name,
                    void *reserved,
                    void **delegate);

    // Platform-specific shared library handle.
    struct LibHandle
    {
#ifdef _WIN32
      HMODULE handle = nullptr;
#else
      void *handle = nullptr;
#endif

      bool valid() const { return handle != nullptr; }

      void close()
      {
        if (handle != nullptr)
        {
#ifdef _WIN32
          FreeLibrary(handle);
#else
          dlclose(handle);
#endif
          handle = nullptr;
        }
      }
    };

    LibHandle load_library(const std::filesystem::path &path)
    {
      LibHandle lib;
#ifdef _WIN32
      lib.handle = LoadLibraryW(path.c_str());
#else
      lib.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
      return lib;
    }

    template <typename T>
    T get_symbol(const LibHandle &lib, const char *name)
    {
#ifdef _WIN32
      return reinterpret_cast<T>(GetProcAddress(lib.handle, name));
#else
      return reinterpret_cast<T>(dlsym(lib.handle, name));
#endif
    }

    std::filesystem::path find_hostfxr_library()
    {
      // Search known .NET install locations for the hostfxr shared library.
      // This covers the standard install layout: <dotnet_root>/host/fxr/<version>/libhostfxr.<ext>

#ifdef _WIN32
      constexpr const char *libName = "hostfxr.dll";
      const std::vector<std::filesystem::path> roots = {
          "C:\\Program Files\\dotnet",
          "C:\\Program Files (x86)\\dotnet",
      };
#elif defined(__APPLE__)
      constexpr const char *libName = "libhostfxr.dylib";
      const std::vector<std::filesystem::path> roots = {
          "/usr/local/share/dotnet",
          "/usr/share/dotnet",
      };
#else
      constexpr const char *libName = "libhostfxr.so";
      const std::vector<std::filesystem::path> roots = {
          "/usr/share/dotnet",
          "/usr/local/share/dotnet",
          "/usr/lib/dotnet",
      };
#endif

      // Also check DOTNET_ROOT environment variable.
      std::vector<std::filesystem::path> searchRoots;
      const char *envRoot = std::getenv("DOTNET_ROOT");
      if (envRoot != nullptr && envRoot[0] != '\0')
      {
        searchRoots.emplace_back(envRoot);
      }
      searchRoots.insert(searchRoots.end(), roots.begin(), roots.end());

      // Find the highest version directory under host/fxr/.
      std::filesystem::path bestPath;
      std::string bestVersion;

      for (const auto &root : searchRoots)
      {
        const auto fxrDir = root / "host" / "fxr";
        std::error_code ec;
        if (!std::filesystem::is_directory(fxrDir, ec))
        {
          continue;
        }

        for (const auto &entry : std::filesystem::directory_iterator(fxrDir, ec))
        {
          if (!entry.is_directory(ec))
          {
            continue;
          }

          const auto candidate = entry.path() / libName;
          if (!std::filesystem::exists(candidate, ec))
          {
            continue;
          }

          const std::string versionDir = entry.path().filename().string();
          if (bestPath.empty() || versionDir > bestVersion)
          {
            bestPath = candidate;
            bestVersion = versionDir;
          }
        }
      }

      return bestPath;
    }
  }

  struct ClrHost::Impl
  {
    LibHandle hostfxrLib;
    hostfxr_handle hostContext = nullptr;
    load_assembly_and_get_function_pointer_fn loadAssemblyFn = nullptr;

    hostfxr_initialize_for_runtime_config_fn initFn = nullptr;
    hostfxr_get_runtime_delegate_fn getDelegateFn = nullptr;
    hostfxr_close_fn closeFn = nullptr;

    bool initialized = false;
  };

  ClrHost::ClrHost() : impl_(std::make_unique<Impl>()) {}

  ClrHost::~ClrHost()
  {
    close();
  }

  ClrHost::ClrHost(ClrHost &&other) noexcept = default;
  ClrHost &ClrHost::operator=(ClrHost &&other) noexcept = default;

  bool ClrHost::initialize(
      const std::filesystem::path &runtimeConfigPath,
      std::string *errorMessage)
  {
    close();

    // Step 1: Find and load hostfxr.
    const auto hostfxrPath = find_hostfxr_library();
    if (hostfxrPath.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Could not locate the .NET runtime (hostfxr). "
                        "Ensure the .NET runtime is installed, or set the DOTNET_ROOT environment variable.";
      }
      return false;
    }

    impl_->hostfxrLib = load_library(hostfxrPath);
    if (!impl_->hostfxrLib.valid())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to load hostfxr from: " + hostfxrPath.string();
      }
      return false;
    }

    // Step 2: Resolve hostfxr entry points.
    impl_->initFn = get_symbol<hostfxr_initialize_for_runtime_config_fn>(
        impl_->hostfxrLib, "hostfxr_initialize_for_runtime_config");
    impl_->getDelegateFn = get_symbol<hostfxr_get_runtime_delegate_fn>(
        impl_->hostfxrLib, "hostfxr_get_runtime_delegate");
    impl_->closeFn = get_symbol<hostfxr_close_fn>(
        impl_->hostfxrLib, "hostfxr_close");

    if (impl_->initFn == nullptr || impl_->getDelegateFn == nullptr || impl_->closeFn == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "The loaded hostfxr library is missing required entry points.";
      }
      impl_->hostfxrLib.close();
      return false;
    }

    // Step 3: Initialize for the runtime config.
    const std::string configPathStr = runtimeConfigPath.string();
    const int32_t initResult = impl_->initFn(configPathStr.c_str(), nullptr, &impl_->hostContext);
    if (initResult != 0 || impl_->hostContext == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to initialize the .NET runtime context (hostfxr error 0x" +
                        ([](int32_t v)
                         {
                           char buf[16];
                           std::snprintf(buf, sizeof(buf), "%08X", static_cast<uint32_t>(v));
                           return std::string(buf);
                         })(initResult) +
                        ").";
      }
      impl_->hostfxrLib.close();
      return false;
    }

    // Step 4: Get the load_assembly_and_get_function_pointer delegate.
    void *loadAssemblyDelegate = nullptr;
    const int32_t delegateResult = impl_->getDelegateFn(
        impl_->hostContext,
        hdt_load_assembly_and_get_function_pointer,
        &loadAssemblyDelegate);

    if (delegateResult != 0 || loadAssemblyDelegate == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to obtain the managed assembly loader delegate from the .NET runtime.";
      }
      impl_->closeFn(impl_->hostContext);
      impl_->hostContext = nullptr;
      impl_->hostfxrLib.close();
      return false;
    }

    impl_->loadAssemblyFn =
        reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyDelegate);
    impl_->initialized = true;
    return true;
  }

  bool ClrHost::get_managed_function(
      const std::string &assemblyPath,
      const std::string &typeName,
      const std::string &methodName,
      void **functionPointer,
      std::string *errorMessage)
  {
    if (!impl_->initialized || impl_->loadAssemblyFn == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "The CLR host has not been initialized.";
      }
      return false;
    }

    const int32_t result = impl_->loadAssemblyFn(
        assemblyPath.c_str(),
        typeName.c_str(),
        methodName.c_str(),
        nullptr, // UNMANAGEDCALLERSONLY_METHOD
        nullptr,
        functionPointer);

    if (result != 0 || *functionPointer == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to locate managed method " + typeName + "." + methodName +
                        " in assembly " + assemblyPath +
                        " (error 0x" +
                        ([](int32_t v)
                         {
                           char buf[16];
                           std::snprintf(buf, sizeof(buf), "%08X", static_cast<uint32_t>(v));
                           return std::string(buf);
                         })(result) +
                        ").";
      }
      return false;
    }

    return true;
  }

  void ClrHost::close()
  {
    if (impl_->hostContext != nullptr && impl_->closeFn != nullptr)
    {
      impl_->closeFn(impl_->hostContext);
      impl_->hostContext = nullptr;
    }

    impl_->loadAssemblyFn = nullptr;
    impl_->initFn = nullptr;
    impl_->getDelegateFn = nullptr;
    impl_->closeFn = nullptr;
    impl_->initialized = false;
    impl_->hostfxrLib.close();
  }

  bool ClrHost::is_initialized() const
  {
    return impl_->initialized;
  }
}
