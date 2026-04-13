#include "script_compiler.hpp"

#include <fstream>
#include <regex>
#include <sstream>

#include "build_config.hpp"
#include "subprocess.hpp"

namespace hades
{
  namespace
  {
    std::string shared_lib_extension()
    {
#if defined(_WIN32)
      return ".dll";
#elif defined(__APPLE__)
      return ".dylib";
#else
      return ".so";
#endif
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
          *errorMessage = "Failed to persist file: " + path.string();
        }
        return false;
      }

      return true;
    }

    bool is_msvc_compiler(const std::string &compilerId)
    {
      return compilerId == "MSVC";
    }
  }

  std::vector<std::string> ScriptCompiler::scanRegisteredClasses(
      const std::vector<std::filesystem::path> &sourceFiles)
  {
    std::vector<std::string> classNames;
    const std::regex macroRegex(R"(HADES_REGISTER_SCRIPT\s*\(\s*(\w+)\s*\))");

    for (const auto &filePath : sourceFiles)
    {
      std::ifstream file(filePath);
      if (!file.is_open())
      {
        continue;
      }

      std::string line;
      while (std::getline(file, line))
      {
        std::smatch match;
        if (std::regex_search(line, match, macroRegex))
        {
          classNames.push_back(match[1].str());
        }
      }
    }

    return classNames;
  }

  bool ScriptCompiler::generateRegistry(
      const std::vector<std::string> &classNames,
      const std::filesystem::path &outputDir,
      std::filesystem::path &registryPath,
      std::string *errorMessage)
  {
    std::ostringstream source;
    source << "// Auto-generated script registry. Do not edit.\n";
    source << "#include \"engine/runtime/hades_script_registration.hpp\"\n\n";

    for (const auto &name : classNames)
    {
      source << "extern \"C\" hades::HadesScript *hades_create_" << name << "();\n";
    }

    source << "\nextern \"C\" const hades::ScriptFactoryEntry hades_script_factories[] = {\n";
    for (const auto &name : classNames)
    {
      source << "    {\"" << name << "\", hades_create_" << name << "},\n";
    }
    source << "    {nullptr, nullptr}\n";
    source << "};\n";

    source << "extern \"C\" const int hades_script_factory_count = "
           << classNames.size() << ";\n";

    registryPath = outputDir / "script_registry.cpp";
    return write_text_file(registryPath, source.str(), errorMessage);
  }

  bool ScriptCompiler::compile(
      const std::vector<std::filesystem::path> &sourceFiles,
      const std::filesystem::path &outputDir,
      std::string *errorMessage)
  {
    if (sourceFiles.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No script source files to compile.";
      }
      return false;
    }

    // Create output directory.
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to create output directory: " + outputDir.string();
      }
      return false;
    }

    // Scan for registered classes.
    const auto classNames = scanRegisteredClasses(sourceFiles);
    if (classNames.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No HADES_REGISTER_SCRIPT macros found in any source file.";
      }
      return false;
    }

    // Generate the registry aggregation file.
    std::filesystem::path registryPath;
    if (!generateRegistry(classNames, outputDir, registryPath, errorMessage))
    {
      return false;
    }

    // Build compiler command line.
    const std::string compiler = build_config::cxx_compiler;
    const std::string compilerId = build_config::cxx_compiler_id;
    const std::string sourceDir = build_config::cmake_source_dir;
    const std::string includeArg = sourceDir + "/src";

    const std::string libName = "HadesScripts" + shared_lib_extension();
    const std::filesystem::path outputPath = outputDir / libName;

    std::vector<std::string> args;

    if (is_msvc_compiler(compilerId))
    {
      args.push_back(compiler);
      args.push_back("/std:c++17");
      args.push_back("/LD");
      args.push_back("/EHsc");
      args.push_back("/O2");
      args.push_back("/I" + includeArg);

      for (const auto &src : sourceFiles)
      {
        args.push_back(src.string());
      }
      args.push_back(registryPath.string());

      args.push_back("/Fe:" + outputPath.string());
    }
    else
    {
      args.push_back(compiler);
      args.push_back("-std=c++17");
      args.push_back("-shared");
      args.push_back("-fPIC");
      args.push_back("-O2");
      args.push_back("-I" + includeArg);
#if defined(__APPLE__)
      // Allow scripts to reference engine symbols resolved at dlopen time.
      args.push_back("-undefined");
      args.push_back("dynamic_lookup");
      args.push_back("-flat_namespace");
#endif

      for (const auto &src : sourceFiles)
      {
        args.push_back(src.string());
      }
      args.push_back(registryPath.string());

      args.push_back("-o");
      args.push_back(outputPath.string());
    }

    const auto result = Subprocess::run_capture(args);

    if (!result.launched)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to launch compiler: " + compiler;
      }
      return false;
    }

    if (result.exitCode != 0)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Script compilation failed:\n" + result.output;
      }
      return false;
    }

    libraryPath_ = outputPath;
    return true;
  }
}
