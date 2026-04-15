#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace hades::test_support
{
  struct ScopedDirectoryCleanup
  {
    explicit ScopedDirectoryCleanup(std::filesystem::path directoryPath)
        : directory(std::move(directoryPath)) {}

    ~ScopedDirectoryCleanup()
    {
      std::error_code errorCode;
      std::filesystem::remove_all(directory, errorCode);
    }

    std::filesystem::path directory;
  };

  inline std::filesystem::path unique_test_directory(const char *prefix)
  {
    const auto uniqueSuffix = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / (std::string(prefix) + "-" + uniqueSuffix);
  }
}
