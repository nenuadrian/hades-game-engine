#ifndef HADES_ENGINE_RUNTIME_CLR_HOST_HPP
#define HADES_ENGINE_RUNTIME_CLR_HOST_HPP

#include <filesystem>
#include <memory>
#include <string>

namespace hades
{
  class ClrHost
  {
  public:
    ClrHost();
    ~ClrHost();

    ClrHost(ClrHost &&other) noexcept;
    ClrHost &operator=(ClrHost &&other) noexcept;

    ClrHost(const ClrHost &) = delete;
    ClrHost &operator=(const ClrHost &) = delete;

    bool initialize(
        const std::filesystem::path &runtimeConfigPath,
        std::string *errorMessage = nullptr);

    bool get_managed_function(
        const std::string &assemblyPath,
        const std::string &typeName,
        const std::string &methodName,
        void **functionPointer,
        std::string *errorMessage = nullptr);

    void close();
    bool is_initialized() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
