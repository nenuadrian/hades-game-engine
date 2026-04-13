#include "scripting_sdk.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "dotnet_config.hpp"
#include "hades_scripting_api_source.hpp"
#include "subprocess.hpp"

namespace hades
{
  namespace
  {
    std::string dotnet_executable()
    {
      if (dotnet_config::configured_dotnet_executable[0] != '\0')
      {
        return dotnet_config::configured_dotnet_executable;
      }

      return "dotnet";
    }

    std::optional<int> parse_dotnet_major_version(const std::string &versionOutput)
    {
      std::istringstream stream(versionOutput);
      std::string versionLine;
      if (!(stream >> versionLine))
      {
        return std::nullopt;
      }

      const std::size_t firstDot = versionLine.find('.');
      if (firstDot == std::string::npos)
      {
        return std::nullopt;
      }

      try
      {
        return std::stoi(versionLine.substr(0, firstDot));
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    bool write_text_file(
        const std::filesystem::path &path,
        const std::string &contents,
        std::string *errorMessage)
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      if (!file)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to write file: " + path.string();
        }
        return false;
      }

      file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      if (!file.good())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Write failed for file: " + path.string();
        }
        return false;
      }

      return true;
    }

    std::filesystem::path sdk_root()
    {
      const char *home = std::getenv("HOME");
      if (home == nullptr)
      {
        home = std::getenv("USERPROFILE");
      }
      if (home == nullptr)
      {
        return {};
      }

      return std::filesystem::path(home) / ".hades" / "sdk";
    }

    std::string render_sdk_csproj(const std::string &targetFramework)
    {
      return "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
             "  <PropertyGroup>\n"
             "    <OutputType>Library</OutputType>\n"
             "    <TargetFramework>" +
             targetFramework +
             "</TargetFramework>\n"
             "    <AssemblyName>Hades.Scripting</AssemblyName>\n"
             "    <RootNamespace>Hades.Scripting</RootNamespace>\n"
             "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n"
             "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
             "  </PropertyGroup>\n"
             "  <ItemGroup>\n"
             "    <Compile Include=\"HadesScriptingAPI.cs\" />\n"
             "  </ItemGroup>\n"
             "</Project>\n";
    }
  }

  const char *scripting_api_source()
  {
    return HADES_SCRIPTING_API_SOURCE;
  }

  std::filesystem::path ensure_scripting_sdk(std::string *errorMessage)
  {
    const std::filesystem::path root = sdk_root();
    if (root.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Unable to determine home directory for SDK cache.";
      }
      return {};
    }

    const std::filesystem::path dllPath = root / "Hades.Scripting.dll";

    // If the DLL already exists, return it immediately.
    std::error_code existsError;
    if (std::filesystem::exists(dllPath, existsError))
    {
      return dllPath;
    }

    // Detect dotnet SDK.
    const std::string dotnetCommand = dotnet_executable();
    const ProcessResult versionResult = Subprocess::run_capture({dotnetCommand, "--version"});
    if (!versionResult.launched || versionResult.exitCode != 0)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "dotnet SDK is required to build the scripting reference assembly. "
                        "Install .NET SDK 7.0+ and ensure 'dotnet' is on PATH.";
      }
      return {};
    }

    const auto majorVersion = parse_dotnet_major_version(versionResult.output);
    if (!majorVersion.has_value() || *majorVersion < 7)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Hades scripting requires dotnet SDK 7.0 or newer.";
      }
      return {};
    }

    const std::string targetFramework = "net" + std::to_string(*majorVersion) + ".0";

    // Prepare the SDK build directory.
    const std::filesystem::path srcDir = root / "src";
    std::error_code dirError;
    std::filesystem::create_directories(srcDir, dirError);
    if (dirError)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to create SDK build directory: " + srcDir.string();
      }
      return {};
    }

    // Write the API source and project file.
    if (!write_text_file(srcDir / "HadesScriptingAPI.cs", HADES_SCRIPTING_API_SOURCE, errorMessage))
    {
      return {};
    }

    if (!write_text_file(srcDir / "Hades.Scripting.csproj", render_sdk_csproj(targetFramework), errorMessage))
    {
      return {};
    }

    // Build the reference assembly.
    const ProcessResult buildResult = Subprocess::run_capture(
        {
            dotnetCommand,
            "build",
            "Hades.Scripting.csproj",
            "-c",
            "Release",
            "--nologo",
            "-o",
            root.string(),
        },
        srcDir);

    if (!buildResult.launched)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to launch dotnet build for the scripting SDK.";
      }
      return {};
    }

    if (buildResult.exitCode != 0)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to build Hades.Scripting reference assembly.\n" + buildResult.output;
      }
      return {};
    }

    if (!std::filesystem::exists(dllPath, existsError))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Hades.Scripting.dll was not produced by the build.";
      }
      return {};
    }

    return dllPath;
  }
}
