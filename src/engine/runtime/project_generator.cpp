#include "project_generator.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace hades
{
  namespace
  {
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

    std::string xml_escape(const std::string &input)
    {
      std::string escaped;
      escaped.reserve(input.size());
      for (char ch : input)
      {
        switch (ch)
        {
        case '&':
          escaped += "&amp;";
          break;
        case '<':
          escaped += "&lt;";
          break;
        case '>':
          escaped += "&gt;";
          break;
        case '"':
          escaped += "&quot;";
          break;
        case '\'':
          escaped += "&apos;";
          break;
        default:
          escaped.push_back(ch);
          break;
        }
      }
      return escaped;
    }

    std::string render_workspace_csproj(const std::filesystem::path &sdkDllPath)
    {
      std::ostringstream project;
      project << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
              << "  <PropertyGroup>\n"
              << "    <OutputType>Library</OutputType>\n"
              << "    <TargetFramework>net8.0</TargetFramework>\n"
              << "    <ImplicitUsings>enable</ImplicitUsings>\n"
              << "    <Nullable>enable</Nullable>\n"
              << "    <LangVersion>latest</LangVersion>\n"
              << "    <AssemblyName>HadesScripts</AssemblyName>\n"
              << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
              << "  </PropertyGroup>\n"
              << "  <ItemGroup>\n"
              << "    <Reference Include=\"Hades.Scripting\">\n"
              << "      <HintPath>" << xml_escape(sdkDllPath.generic_string()) << "</HintPath>\n"
              << "    </Reference>\n"
              << "  </ItemGroup>\n"
              << "  <ItemGroup>\n"
              << "    <Compile Include=\"../../**/*.cs\" Exclude=\"../../.hades/**\" />\n"
              << "  </ItemGroup>\n"
              << "</Project>\n";
      return project.str();
    }

    std::string render_solution(const std::string &csprojRelativePath)
    {
      // Minimal .sln file.  The GUID values are arbitrary but stable.
      return "\xEF\xBB\xBF\n"
             "Microsoft Visual Studio Solution File, Format Version 12.00\n"
             "# Visual Studio Version 17\n"
             "VisualStudioVersion = 17.0.31903.59\n"
             "MinimumVisualStudioVersion = 10.0.40219.1\n"
             "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"HadesScripts\", \"" +
             csprojRelativePath +
             "\", \"{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}\"\n"
             "EndProject\n"
             "Global\n"
             "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
             "\t\tDebug|Any CPU = Debug|Any CPU\n"
             "\t\tRelease|Any CPU = Release|Any CPU\n"
             "\tEndGlobalSection\n"
             "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
             "\t\t{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}.Debug|Any CPU.ActiveCfg = Debug|Any CPU\n"
             "\t\t{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}.Debug|Any CPU.Build.0 = Debug|Any CPU\n"
             "\t\t{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}.Release|Any CPU.ActiveCfg = Release|Any CPU\n"
             "\t\t{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}.Release|Any CPU.Build.0 = Release|Any CPU\n"
             "\tEndGlobalSection\n"
             "EndGlobal\n";
    }
  }

  bool generate_workspace_project(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &sdkDllPath,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No workspace is open.";
      }
      return false;
    }

    const std::filesystem::path scriptingDir = workspacePath / ".hades" / "scripting";

    std::error_code dirError;
    std::filesystem::create_directories(scriptingDir, dirError);
    if (dirError)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to create scripting directory: " + scriptingDir.string();
      }
      return false;
    }

    // Write the .csproj inside .hades/scripting/.
    const std::filesystem::path csprojPath = scriptingDir / "HadesScripts.csproj";
    if (!write_text_file(csprojPath, render_workspace_csproj(sdkDllPath), errorMessage))
    {
      return false;
    }

    // Write the .sln at the workspace root so editors discover it.
    const std::filesystem::path slnPath = workspacePath / "HadesScripts.sln";
    const std::string csprojRelative = ".hades/scripting/HadesScripts.csproj";
    if (!write_text_file(slnPath, render_solution(csprojRelative), errorMessage))
    {
      return false;
    }

    return true;
  }
}
