#include "export_packaging.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "engine/blueprint/blueprint_asset.hpp"
#include "engine/core/ecs/scene_serializer.hpp"

namespace hades::exporting
{
  namespace
  {
    void add_unique_directory(
        std::vector<std::filesystem::path> &directories,
        std::filesystem::path directory)
    {
      if (std::find(directories.begin(), directories.end(), directory) == directories.end())
      {
        directories.push_back(std::move(directory));
      }
    }

    // Every directory holding a model file, found by walking the workspace the
    // way the workspace picker does. Collected up front and copied afterwards:
    // writing into `destination` mid-walk would feed the copies back to the
    // iterator when the export directory sits inside the workspace.
    void collect_scanned_model_directories(
        const std::filesystem::path &workspacePath,
        const std::filesystem::path &destination,
        std::vector<std::filesystem::path> &directories)
    {
      std::error_code ec;
      auto it = std::filesystem::recursive_directory_iterator(
          workspacePath,
          std::filesystem::directory_options::skip_permission_denied,
          ec);
      if (ec)
      {
        return;
      }

      for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
      {
        if (ec)
        {
          break;
        }

        const std::filesystem::path &path = it->path();
        std::error_code entryError;
        if (it->is_directory(entryError))
        {
          // Re-exporting into a directory inside the workspace would otherwise
          // copy the previous export's models back into themselves.
          if (is_ignored_scan_component(path.filename().string()) ||
              std::filesystem::equivalent(path, destination, entryError))
          {
            it.disable_recursion_pending();
          }
          continue;
        }

        if (!it->is_regular_file(entryError) || !has_model_file_extension(path))
        {
          continue;
        }

        add_unique_directory(directories, path.parent_path());
      }
    }

    // The two model pickers disagree about what is selectable: the workspace
    // picker applies the ignore list above, but the Animation Editor's picker
    // skips only dot-prefixed entries (animation_editor_plugin.cpp,
    // refresh_model_list) and will happily spawn a real, serialized entity
    // pointing at `out/hero.glb`. A saved world is the authority on what the
    // game needs, so anything it names travels regardless of which picker
    // produced it — and regardless of symlinks, which the sweep above does not
    // follow.
    void collect_referenced_model_directories(
        const std::filesystem::path &workspacePath,
        std::vector<std::filesystem::path> &directories)
    {
      for (const std::string &worldName : hades::list_saved_worlds(workspacePath))
      {
        std::ifstream worldFile(workspacePath / ".hades" / "worlds" / (worldName + ".json"));
        if (!worldFile.is_open())
        {
          continue;
        }

        const nlohmann::json world = nlohmann::json::parse(worldFile, nullptr, false);
        if (!world.is_object())
        {
          continue;
        }

        const auto entities = world.find("entities");
        if (entities == world.end() || !entities->is_array())
        {
          continue;
        }

        for (const auto &entity : *entities)
        {
          if (!entity.is_object())
          {
            continue;
          }

          const auto components = entity.find("components");
          if (components == entity.end() || !components->is_object())
          {
            continue;
          }

          const auto model = components->find("model");
          if (model == components->end() || !model->is_object())
          {
            continue;
          }

          const std::string assetPath = model->value("assetPath", std::string{});
          if (assetPath.empty() || std::filesystem::path(assetPath).is_absolute())
          {
            // An absolute path resolves to the authoring machine, not the
            // bundle; there is no relocation that would make it work on the
            // target.
            continue;
          }

          const std::filesystem::path source = (workspacePath / assetPath).lexically_normal();
          std::error_code ec;
          if (!std::filesystem::is_regular_file(source, ec))
          {
            continue;
          }

          const std::filesystem::path relative = source.lexically_relative(workspacePath);
          if (relative.empty() || *relative.begin() == "..")
          {
            continue;
          }

          add_unique_directory(directories, source.parent_path());
        }
      }
    }
  }

  const char *platform_label(ExportPlatform platform)
  {
    switch (platform)
    {
    case ExportPlatform::macOS:
      return "macOS";
    case ExportPlatform::Linux:
      return "Linux";
    case ExportPlatform::Windows:
      return "Windows";
    }
    return "Unknown";
  }

  bool is_current_platform(ExportPlatform platform)
  {
#ifdef __APPLE__
    return platform == ExportPlatform::macOS;
#elif defined(__linux__)
    return platform == ExportPlatform::Linux;
#elif defined(_WIN32)
    return platform == ExportPlatform::Windows;
#else
    (void)platform;
    return false;
#endif
  }

  const char *runtime_binary_name(ExportPlatform platform)
  {
    return platform == ExportPlatform::Windows ? "HadesRuntime.exe" : "HadesRuntime";
  }

  std::string runtime_arguments(LauncherMode mode)
  {
    switch (mode)
    {
    case LauncherMode::Windowed:
      return "--project .";
    case LauncherMode::Headless:
      return "--project . --headless";
    case LauncherMode::Api:
      return "--project . --api --api-port 7777";
    }
    return "--project .";
  }

  std::string launcher_file_name(
      ExportPlatform platform,
      LauncherMode mode,
      const std::string &projectName)
  {
    if (platform == ExportPlatform::macOS)
    {
      // `.command` scripts double-click into Terminal, so stderr is visible —
      // useful both for debug builds and for diagnosing startup failures.
      switch (mode)
      {
      case LauncherMode::Windowed:
        return "Run " + projectName + ".command";
      case LauncherMode::Headless:
        return "Run " + projectName + " Headless.command";
      case LauncherMode::Api:
        return "Run " + projectName + " API.command";
      }
    }

    if (platform == ExportPlatform::Windows)
    {
      switch (mode)
      {
      case LauncherMode::Windowed:
        return projectName + ".bat";
      case LauncherMode::Headless:
        return projectName + "_headless.bat";
      case LauncherMode::Api:
        return projectName + "_api.bat";
      }
    }

    switch (mode)
    {
    case LauncherMode::Windowed:
      return "run_" + projectName + ".sh";
    case LauncherMode::Headless:
      return "run_" + projectName + "_headless.sh";
    case LauncherMode::Api:
      return "run_" + projectName + "_api.sh";
    }

    return "run_" + projectName + ".sh";
  }

  std::string launcher_script(
      ExportPlatform platform,
      LauncherMode mode,
      const std::string &projectName,
      bool debugBuild)
  {
    const std::string binaryName = runtime_binary_name(platform);
    const std::string arguments = runtime_arguments(mode);

    if (platform == ExportPlatform::Windows)
    {
      std::string script = "@echo off\r\n";
      if (debugBuild)
      {
        script += "set HADES_DEBUG=1\r\n";
      }
      script += "cd /d \"%~dp0\"\r\n";
      script += binaryName + " " + arguments + "\r\n";
      return script;
    }

    std::string script = "#!/bin/bash\n";
    if (debugBuild)
    {
      script += "export HADES_DEBUG=1\n";
    }

    if (platform == ExportPlatform::macOS)
    {
      // Regular double-clicks on the .app still work: the runtime detects the
      // bundled .hades directory next to the executable on its own.
      script += "cd \"$(dirname \"$0\")/" + projectName + ".app/Contents/MacOS\"\n";
    }
    else
    {
      script += "cd \"$(dirname \"$0\")\"\n";
    }

    script += "./" + binaryName + " " + arguments + "\n";
    return script;
  }

  std::string info_plist(const std::string &projectName, const std::string &binaryName)
  {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n"
           "<dict>\n"
           "  <key>CFBundleName</key>\n"
           "  <string>" + projectName + "</string>\n"
           "  <key>CFBundleExecutable</key>\n"
           "  <string>" + binaryName + "</string>\n"
           "  <key>CFBundleIdentifier</key>\n"
           "  <string>com.hades." + projectName + "</string>\n"
           "  <key>CFBundleVersion</key>\n"
           "  <string>1.0</string>\n"
           "  <key>CFBundlePackageType</key>\n"
           "  <string>APPL</string>\n"
           "</dict>\n"
           "</plist>\n";
  }

  std::string lowercase_extension(const std::filesystem::path &path)
  {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
  }

  bool has_model_file_extension(const std::filesystem::path &path)
  {
    const std::string extension = lowercase_extension(path);

    return extension == ".fbx" || extension == ".obj" || extension == ".gltf" ||
           extension == ".glb" || extension == ".dae";
  }

  // Mirrors the workspace scan's ignore list (editor_workspace.cpp) so the bulk
  // sweep never rummages through VCS metadata or build trees — inside a build
  // directory a `.obj` is a compiled object file rather than a mesh, and the
  // export's own CMake tree lives at `<output>/build` by construction. Models
  // the user actually referenced still travel even from these directories: see
  // collect_referenced_model_directories.
  bool is_ignored_scan_component(std::string_view component)
  {
    if (component == ".git" ||
        component == ".hades" ||
        component == ".vs" ||
        component == ".idea" ||
        component == ".vscode" ||
        component == "bin" ||
        component == "obj" ||
        component == "out" ||
        component == "_deps" ||
        component == "node_modules")
    {
      return true;
    }

    return component == "build" ||
           component.rfind("build-", 0) == 0 ||
           component.rfind("cmake-build-", 0) == 0;
  }

  std::string decode_uri_escapes(const std::string &uri)
  {
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i)
    {
      if (uri[i] == '%' && i + 2 < uri.size() &&
          std::isxdigit(static_cast<unsigned char>(uri[i + 1])) != 0 &&
          std::isxdigit(static_cast<unsigned char>(uri[i + 2])) != 0)
      {
        decoded.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
        i += 2;
        continue;
      }

      decoded.push_back(uri[i]);
    }

    return decoded;
  }

  std::vector<std::filesystem::path> collect_model_directories(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination)
  {
    std::vector<std::filesystem::path> directories;

    std::error_code ec;
    if (workspacePath.empty() || !std::filesystem::is_directory(workspacePath, ec))
    {
      return directories;
    }

    collect_scanned_model_directories(workspacePath, destination, directories);
    collect_referenced_model_directories(workspacePath, directories);
    return directories;
  }

  void copy_directory_recursive(
      const std::filesystem::path &source,
      const std::filesystem::path &destination,
      std::string *errorMessage)
  {
    std::error_code ec;
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec && errorMessage != nullptr)
    {
      *errorMessage = "Failed to copy " + source.string() + ": " + ec.message();
    }
  }

  int copy_blueprint_assets(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination)
  {
    int copied = 0;
    for (const auto &relative : hades::list_blueprint_assets(workspacePath))
    {
      const std::filesystem::path target = destination / relative;

      std::error_code ec;
      if (target.has_parent_path())
      {
        std::filesystem::create_directories(target.parent_path(), ec);
      }

      std::filesystem::copy_file(
          workspacePath / relative,
          target,
          std::filesystem::copy_options::overwrite_existing,
          ec);
      if (!ec)
      {
        ++copied;
      }
    }

    return copied;
  }

  // A `.gltf` keeps its vertex data in a separate buffer file and is free to
  // point at it — and at its textures — through a subdirectory or a sibling
  // directory, which copying the model's own directory would miss. assimp fails
  // the whole import when the buffer is absent, and a failed import is invisible
  // at runtime, so those referenced files have to travel too, at the same
  // workspace-relative offsets or the URIs inside the copied `.gltf` break.
  // Same-directory references are already covered by the directory copy.
  int copy_gltf_referenced_files(
      const std::filesystem::path &gltfFile,
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination,
      std::vector<std::filesystem::path> &copiedTargets)
  {
    std::ifstream document(gltfFile);
    if (!document.is_open())
    {
      return 0;
    }

    const nlohmann::json gltf = nlohmann::json::parse(document, nullptr, false);
    if (!gltf.is_object())
    {
      return 0;
    }

    int copied = 0;
    for (const char *section : {"buffers", "images"})
    {
      const auto entries = gltf.find(section);
      if (entries == gltf.end() || !entries->is_array())
      {
        continue;
      }

      for (const auto &entry : *entries)
      {
        if (!entry.is_object())
        {
          continue;
        }

        const std::string uri = entry.value("uri", std::string{});
        // `data:` payloads are embedded, and a remote or absolute URI cannot be
        // relocated into the bundle.
        if (uri.empty() || uri.rfind("data:", 0) == 0 || uri.find("://") != std::string::npos)
        {
          continue;
        }

        const std::filesystem::path reference(decode_uri_escapes(uri));
        if (reference.is_absolute())
        {
          continue;
        }

        const std::filesystem::path source =
            (gltfFile.parent_path() / reference).lexically_normal();
        if (source.parent_path() == gltfFile.parent_path())
        {
          continue;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec))
        {
          continue;
        }

        const std::filesystem::path relative = source.lexically_relative(workspacePath);
        if (relative.empty() || *relative.begin() == "..")
        {
          continue;
        }

        const std::filesystem::path target = destination / relative;
        if (std::find(copiedTargets.begin(), copiedTargets.end(), target) != copiedTargets.end())
        {
          continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(
            source,
            target,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (!ec)
        {
          copiedTargets.push_back(target);
          ++copied;
        }
      }
    }

    return copied;
  }

  // Model files live at arbitrary workspace-relative paths outside `.hades` —
  // the workspace scan skips `.hades` before collecting them — so neither the
  // `.hades` copy nor the engine `assets` copy carries them, and an exported
  // game silently renders nothing (the model cache logs, then the renderer and
  // the animator system both `continue` past the entity).
  //
  // The whole containing directory travels rather than the matched file alone:
  // glTF-separate keeps its vertex data in a sidecar `.bin` and Wavefront its
  // materials in a `.mtl`, and assimp fails the load outright when the buffer
  // is missing.
  int copy_model_assets(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination)
  {
    // Scanned from disk rather than read off Editor::workspaceModelFiles_:
    // this runs on the export worker thread, which owns no editor state.
    const std::vector<std::filesystem::path> modelDirectories =
        collect_model_directories(workspacePath, destination);

    int copied = 0;
    std::vector<std::filesystem::path> gltfFiles;
    for (const std::filesystem::path &directory : modelDirectories)
    {
      const std::filesystem::path relative = directory.lexically_relative(workspacePath);
      const std::filesystem::path target =
          (relative.empty() || relative == ".") ? destination : destination / relative;

      std::error_code directoryError;
      std::filesystem::create_directories(target, directoryError);

      for (std::filesystem::directory_iterator file(directory, directoryError);
           !directoryError && file != std::filesystem::directory_iterator();
           file.increment(directoryError))
      {
        std::error_code fileError;
        if (!file->is_regular_file(fileError))
        {
          continue;
        }

        std::filesystem::copy_file(
            file->path(),
            target / file->path().filename(),
            std::filesystem::copy_options::overwrite_existing,
            fileError);
        if (fileError)
        {
          continue;
        }

        ++copied;
        if (lowercase_extension(file->path()) == ".gltf")
        {
          gltfFiles.push_back(file->path());
        }
      }
    }

    std::vector<std::filesystem::path> copiedSidecars;
    for (const std::filesystem::path &gltfFile : gltfFiles)
    {
      copied += copy_gltf_referenced_files(gltfFile, workspacePath, destination, copiedSidecars);
    }

    return copied;
  }
}
