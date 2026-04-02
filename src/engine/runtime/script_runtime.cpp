#include "script_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../components/name_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../components/script_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "subprocess.hpp"

namespace hades
{
  namespace
  {
    constexpr char HOST_ASSEMBLY_NAME[] = "HadesScriptHost.dll";

    struct ScriptedEntity
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string name;
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      std::vector<std::string> classNames;
    };

    struct BuildArtifacts
    {
      std::filesystem::path workingDirectory;
      std::filesystem::path outputDirectory;
      std::filesystem::path projectPath;
      std::filesystem::path hostPath;
      std::string targetFramework;
    };

    std::string base64_encode(const std::string &value)
    {
      static constexpr char alphabet[] =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

      std::string encoded;
      encoded.reserve(((value.size() + 2) / 3) * 4);

      int valueAccumulator = 0;
      int bitsCollected = -6;
      for (unsigned char ch : value)
      {
        valueAccumulator = (valueAccumulator << 8) + ch;
        bitsCollected += 8;
        while (bitsCollected >= 0)
        {
          encoded.push_back(alphabet[(valueAccumulator >> bitsCollected) & 0x3F]);
          bitsCollected -= 6;
        }
      }

      if (bitsCollected > -6)
      {
        encoded.push_back(alphabet[((valueAccumulator << 8) >> (bitsCollected + 8)) & 0x3F]);
      }

      while (encoded.size() % 4 != 0)
      {
        encoded.push_back('=');
      }

      return encoded;
    }

    std::string base64_decode(const std::string &value)
    {
      static constexpr unsigned char invalid = 0xFF;
      static constexpr unsigned char decodeTable[256] = {
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, 62, invalid, invalid, invalid, 63,
          52, 53, 54, 55, 56, 57, 58, 59, 60, 61, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
          15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, invalid, invalid, invalid, invalid, invalid,
          invalid, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
          41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid,
          invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid};

      std::string decoded;
      int valueAccumulator = 0;
      int bitsCollected = -8;

      for (unsigned char ch : value)
      {
        if (ch == '=')
        {
          break;
        }

        const unsigned char decodedValue = decodeTable[ch];
        if (decodedValue == invalid)
        {
          return std::string();
        }

        valueAccumulator = (valueAccumulator << 6) + decodedValue;
        bitsCollected += 6;
        if (bitsCollected >= 0)
        {
          decoded.push_back(static_cast<char>((valueAccumulator >> bitsCollected) & 0xFF));
          bitsCollected -= 8;
        }
      }

      return decoded;
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
      std::istringstream stream(line);
      stream.imbue(std::locale::classic());

      std::vector<std::string> fields;
      std::string field;
      while (stream >> field)
      {
        fields.push_back(field);
      }
      return fields;
    }

    std::string format_float(float value)
    {
      std::ostringstream stream;
      stream.imbue(std::locale::classic());
      stream << std::setprecision(9) << value;
      return stream.str();
    }

    bool parse_float(const std::string &value, float &parsed)
    {
      std::istringstream stream(value);
      stream.imbue(std::locale::classic());
      stream >> parsed;
      return stream.good() || stream.eof();
    }

    std::string xml_escape(const std::string &value)
    {
      std::string escaped;
      escaped.reserve(value.size());
      for (char ch : value)
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

    std::string to_utf8(const std::filesystem::path &path)
    {
      return path.u8string();
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

    std::string default_class_name(const std::string &scriptPath)
    {
      return std::filesystem::path(scriptPath).stem().string();
    }

    std::filesystem::path resolve_script_source_path(
        const std::string &scriptPath,
        const std::filesystem::path &workspaceRoot)
    {
      const std::filesystem::path rawPath(scriptPath);
      if (rawPath.is_absolute())
      {
        return rawPath.lexically_normal();
      }

      if (!workspaceRoot.empty())
      {
        return (workspaceRoot / rawPath).lexically_normal();
      }

      return std::filesystem::absolute(rawPath).lexically_normal();
    }

    bool collect_scripted_entities(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::vector<ScriptedEntity> &scriptedEntities,
        std::vector<std::filesystem::path> &uniqueSourceFiles,
        std::string *errorMessage)
    {
      scriptedEntities.clear();
      uniqueSourceFiles.clear();

      std::set<std::string> seenPaths;

      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (!componentManager.hasComponent<ScriptComponent>(entity))
        {
          continue;
        }

        if (!componentManager.hasComponent<PositionComponent3D>(entity))
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Scripted entities currently require a PositionComponent3D.";
          }
          return false;
        }

        const auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
        if (scriptComponent.attachments.empty())
        {
          continue;
        }

        ScriptedEntity scriptedEntity;
        scriptedEntity.entity = entity;

        if (componentManager.hasComponent<NameComponent>(entity))
        {
          scriptedEntity.name = componentManager.getComponent<NameComponent>(entity).value;
        }
        else
        {
          scriptedEntity.name = "Entity " + std::to_string(entity);
        }

        const auto &position = componentManager.getComponent<PositionComponent3D>(entity);
        scriptedEntity.x = position.x;
        scriptedEntity.y = position.y;
        scriptedEntity.z = position.z;

        for (const auto &attachment : scriptComponent.attachments)
        {
          if (!attachment.enabled)
          {
            continue;
          }

          if (attachment.scriptPath.empty())
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "A script attachment is missing its .cs file path.";
            }
            return false;
          }

          const std::filesystem::path sourcePath = resolve_script_source_path(attachment.scriptPath, workspaceRoot);
          if (!std::filesystem::exists(sourcePath))
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Script file does not exist: " + sourcePath.string();
            }
            return false;
          }

          if (sourcePath.extension() != ".cs")
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Only .cs files can be attached as scripts.";
            }
            return false;
          }

          const std::string resolvedClassName =
              attachment.className.empty() ? default_class_name(attachment.scriptPath) : attachment.className;
          if (resolvedClassName.empty())
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Unable to infer a C# class name from the attached script path.";
            }
            return false;
          }

          scriptedEntity.classNames.push_back(resolvedClassName);

          const std::string normalizedPath = sourcePath.lexically_normal().string();
          if (seenPaths.insert(normalizedPath).second)
          {
            uniqueSourceFiles.push_back(sourcePath);
          }
        }

        if (!scriptedEntity.classNames.empty())
        {
          scriptedEntities.push_back(std::move(scriptedEntity));
        }
      }

      return true;
    }

    std::string render_host_runtime_source()
    {
      return R"(using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text;

namespace Hades.Scripting
{
    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }
    }

    public sealed class EntityContext
    {
        public uint EntityId { get; }
        public string Name { get; }
        public Vector3 Position { get; set; }

        public EntityContext(uint entityId, string name, Vector3 position)
        {
            EntityId = entityId;
            Name = name;
            Position = position;
        }
    }

    public abstract class HadesScript
    {
        public virtual void OnStart(EntityContext context) { }
        public virtual void OnUpdate(EntityContext context, float deltaTime) { }
    }

    internal sealed class ScriptHost
    {
        private sealed class ScriptInstance
        {
            public HadesScript Script { get; }
            public EntityContext Context { get; }

            public ScriptInstance(HadesScript script, EntityContext context)
            {
                Script = script;
                Context = context;
            }
        }

        private readonly Dictionary<uint, List<ScriptInstance>> _instancesByEntity = new();
        private readonly List<uint> _entityOrder = new();

        public int Run()
        {
            try
            {
                LoadScene();
                WriteLine("READY");

                while (true)
                {
                    var line = Console.ReadLine();
                    if (line is null || line == "STOP")
                    {
                        return 0;
                    }

                    var fields = SplitFields(line);
                    if (fields.Length < 1)
                    {
                        throw new InvalidOperationException("Empty command received by the script host.");
                    }

                    if (fields[0] != "FRAME")
                    {
                        throw new InvalidOperationException($"Unexpected command '{fields[0]}'.");
                    }

                    HandleFrame(fields);
                }
            }
            catch (Exception ex)
            {
                WriteError(ex.Message);
                return 1;
            }
        }

        private void LoadScene()
        {
            var header = RequireLine("LOAD header");
            var fields = SplitFields(header);
            if (fields.Length != 2 || fields[0] != "LOAD")
            {
                throw new InvalidOperationException("Expected a LOAD header from the engine.");
            }

            var entityCount = ParseInt(fields[1], "entity count");
            var assembly = Assembly.GetExecutingAssembly();

            for (var index = 0; index < entityCount; ++index)
            {
                var entityLine = RequireLine("ENTITY definition");
                var entityFields = SplitFields(entityLine);
                if (entityFields.Length != 7 || entityFields[0] != "ENTITY")
                {
                    throw new InvalidOperationException("Malformed ENTITY definition.");
                }

                var entityId = ParseUInt(entityFields[1], "entity id");
                var name = Decode(entityFields[2]);
                var x = ParseFloat(entityFields[3], "position x");
                var y = ParseFloat(entityFields[4], "position y");
                var z = ParseFloat(entityFields[5], "position z");
                var attachmentCount = ParseInt(entityFields[6], "attachment count");

                var context = new EntityContext(entityId, name, new Vector3(x, y, z));
                var instances = new List<ScriptInstance>(attachmentCount);
                for (var attachmentIndex = 0; attachmentIndex < attachmentCount; ++attachmentIndex)
                {
                    var attachmentLine = RequireLine("ATTACH definition");
                    var attachmentFields = SplitFields(attachmentLine);
                    if (attachmentFields.Length != 2 || attachmentFields[0] != "ATTACH")
                    {
                        throw new InvalidOperationException("Malformed ATTACH definition.");
                    }

                    var className = Decode(attachmentFields[1]);
                    var type = ResolveScriptType(assembly, className);
                    if (!typeof(HadesScript).IsAssignableFrom(type))
                    {
                        throw new InvalidOperationException(
                            $"Type '{className}' must derive from Hades.Scripting.HadesScript.");
                    }

                    if (Activator.CreateInstance(type) is not HadesScript script)
                    {
                        throw new InvalidOperationException(
                            $"Type '{className}' must have a public parameterless constructor.");
                    }

                    instances.Add(new ScriptInstance(script, context));
                }

                _instancesByEntity[entityId] = instances;
                _entityOrder.Add(entityId);
            }

            var endLine = RequireLine("END marker");
            if (endLine != "END")
            {
                throw new InvalidOperationException("Expected END after the LOAD block.");
            }

            foreach (var entityId in _entityOrder)
            {
                foreach (var instance in _instancesByEntity[entityId])
                {
                    instance.Script.OnStart(instance.Context);
                }
            }
        }

        private void HandleFrame(string[] headerFields)
        {
            if (headerFields.Length != 3)
            {
                throw new InvalidOperationException("Malformed FRAME header.");
            }

            var deltaTime = ParseFloat(headerFields[1], "delta time");
            var entityCount = ParseInt(headerFields[2], "frame entity count");

            for (var index = 0; index < entityCount; ++index)
            {
                var entityLine = RequireLine("frame ENTITY");
                var entityFields = SplitFields(entityLine);
                if (entityFields.Length != 5 || entityFields[0] != "ENTITY")
                {
                    throw new InvalidOperationException("Malformed frame ENTITY definition.");
                }

                var entityId = ParseUInt(entityFields[1], "entity id");
                if (!_instancesByEntity.TryGetValue(entityId, out var instances))
                {
                    continue;
                }

                var position = new Vector3(
                    ParseFloat(entityFields[2], "position x"),
                    ParseFloat(entityFields[3], "position y"),
                    ParseFloat(entityFields[4], "position z"));

                foreach (var instance in instances)
                {
                    instance.Context.Position = position;
                }
            }

            var endLine = RequireLine("END marker");
            if (endLine != "END")
            {
                throw new InvalidOperationException("Expected END after the FRAME block.");
            }

            foreach (var entityId in _entityOrder)
            {
                foreach (var instance in _instancesByEntity[entityId])
                {
                    instance.Script.OnUpdate(instance.Context, deltaTime);
                }
            }

            WriteLine($"RESULT {_entityOrder.Count.ToString(CultureInfo.InvariantCulture)}");
            foreach (var entityId in _entityOrder)
            {
                var context = _instancesByEntity[entityId][0].Context;
                WriteLine(
                    $"ENTITY {context.EntityId.ToString(CultureInfo.InvariantCulture)} " +
                    $"{context.Position.X.ToString("R", CultureInfo.InvariantCulture)} " +
                    $"{context.Position.Y.ToString("R", CultureInfo.InvariantCulture)} " +
                    $"{context.Position.Z.ToString("R", CultureInfo.InvariantCulture)}");
            }
            WriteLine("END");
        }

        private static Type ResolveScriptType(Assembly assembly, string className)
        {
            var directMatch = assembly.GetType(className, false, false);
            if (directMatch is not null)
            {
                return directMatch;
            }

            var matches = assembly
                .GetTypes()
                .Where(type => string.Equals(type.FullName, className, StringComparison.Ordinal) ||
                               string.Equals(type.Name, className, StringComparison.Ordinal))
                .ToList();

            if (matches.Count == 0)
            {
                throw new InvalidOperationException($"Unable to locate script class '{className}'.");
            }

            if (matches.Count > 1)
            {
                throw new InvalidOperationException(
                    $"Script class name '{className}' is ambiguous. Use the full namespace-qualified name.");
            }

            return matches[0];
        }

        private static string RequireLine(string description)
        {
            return Console.ReadLine() ?? throw new InvalidOperationException($"Unexpected end of input while reading {description}.");
        }

        private static string[] SplitFields(string line)
        {
            return line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        }

        private static string Decode(string value)
        {
            return Encoding.UTF8.GetString(Convert.FromBase64String(value));
        }

        private static int ParseInt(string value, string description)
        {
            if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                throw new InvalidOperationException($"Invalid {description}: '{value}'.");
            }
            return parsed;
        }

        private static uint ParseUInt(string value, string description)
        {
            if (!uint.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                throw new InvalidOperationException($"Invalid {description}: '{value}'.");
            }
            return parsed;
        }

        private static float ParseFloat(string value, string description)
        {
            if (!float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed))
            {
                throw new InvalidOperationException($"Invalid {description}: '{value}'.");
            }
            return parsed;
        }

        private static void WriteLine(string value)
        {
            Console.Out.WriteLine(value);
            Console.Out.Flush();
        }

        private static void WriteError(string value)
        {
            WriteLine($"ERROR {Convert.ToBase64String(Encoding.UTF8.GetBytes(value))}");
        }
    }

    internal static class Program
    {
        private static int Main()
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;
            return new ScriptHost().Run();
        }
    }
}
)";
    }

    std::string render_csproj(
        const std::string &targetFramework,
        const std::filesystem::path &hostSourcePath,
        const std::vector<std::filesystem::path> &sourceFiles)
    {
      std::ostringstream project;
      project.imbue(std::locale::classic());
      project << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
              << "  <PropertyGroup>\n"
              << "    <OutputType>Exe</OutputType>\n"
              << "    <TargetFramework>" << xml_escape(targetFramework) << "</TargetFramework>\n"
              << "    <ImplicitUsings>enable</ImplicitUsings>\n"
              << "    <Nullable>enable</Nullable>\n"
              << "    <LangVersion>latest</LangVersion>\n"
              << "    <AssemblyName>HadesScriptHost</AssemblyName>\n"
              << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
              << "  </PropertyGroup>\n"
              << "  <ItemGroup>\n"
              << "    <Compile Include=\"" << xml_escape(hostSourcePath.generic_string()) << "\" />\n";

      for (std::size_t index = 0; index < sourceFiles.size(); ++index)
      {
        const auto &sourcePath = sourceFiles[index];
        project << "    <Compile Include=\"" << xml_escape(sourcePath.generic_string())
                << "\" Link=\"Scripts/" << index << "_" << xml_escape(sourcePath.filename().string()) << "\" />\n";
      }

      project << "  </ItemGroup>\n"
              << "</Project>\n";
      return project.str();
    }

    bool prepare_build(
        const std::vector<std::filesystem::path> &sourceFiles,
        BuildArtifacts &artifacts,
        std::string *errorMessage)
    {
      const ProcessResult dotnetVersion = Subprocess::run_capture({"dotnet", "--version"});
      if (!dotnetVersion.launched)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "The dotnet SDK is required to compile attached C# scripts.";
        }
        return false;
      }
      if (dotnetVersion.exitCode != 0)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to query the installed dotnet SDK version.\n" + dotnetVersion.output;
        }
        return false;
      }

      const auto majorVersion = parse_dotnet_major_version(dotnetVersion.output);
      if (!majorVersion.has_value() || *majorVersion < 6)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Hades scripting requires dotnet SDK 6.0 or newer.";
        }
        return false;
      }

      static std::atomic<std::uint64_t> buildCounter{0};
      const std::uint64_t uniqueId = ++buildCounter;

      artifacts.targetFramework = "net" + std::to_string(*majorVersion) + ".0";
      artifacts.workingDirectory =
          std::filesystem::temp_directory_path() / ("hades-script-host-" + std::to_string(uniqueId));
      artifacts.outputDirectory = artifacts.workingDirectory / "out";
      artifacts.projectPath = artifacts.workingDirectory / "HadesScriptHost.csproj";
      artifacts.hostPath = artifacts.outputDirectory / HOST_ASSEMBLY_NAME;

      std::error_code directoryError;
      std::filesystem::create_directories(artifacts.outputDirectory, directoryError);
      if (directoryError)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to create the script build directory.";
        }
        return false;
      }

      const std::filesystem::path hostSourcePath = artifacts.workingDirectory / "HostProgram.cs";
      if (!write_text_file(hostSourcePath, render_host_runtime_source(), errorMessage))
      {
        return false;
      }

      if (!write_text_file(
              artifacts.projectPath,
              render_csproj(artifacts.targetFramework, hostSourcePath, sourceFiles),
              errorMessage))
      {
        return false;
      }

      const ProcessResult buildResult = Subprocess::run_capture(
          {
              "dotnet",
              "build",
              to_utf8(artifacts.projectPath),
              "-c",
              "Release",
              "--nologo",
              "-o",
              to_utf8(artifacts.outputDirectory),
          },
          artifacts.workingDirectory);

      if (!buildResult.launched)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to start dotnet build for the managed script host.";
        }
        return false;
      }

      if (buildResult.exitCode != 0)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "C# script compilation failed.\n" + buildResult.output;
        }
        return false;
      }

      if (!std::filesystem::exists(artifacts.hostPath))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "The managed script host compiled successfully, but its output assembly was not found.";
        }
        return false;
      }

      return true;
    }
  }

  struct ScriptRuntime::Impl
  {
    std::vector<ScriptedEntity> trackedEntities;
    BuildArtifacts buildArtifacts;
    Subprocess process;
    std::string lastError;
    bool running = false;
    bool faulted = false;
  };

  ScriptRuntime::ScriptRuntime() : impl_(std::make_unique<Impl>()) {}
  ScriptRuntime::~ScriptRuntime()
  {
    stop();
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, std::filesystem::path(), errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::string *errorMessage)
  {
    stop();

    std::vector<ScriptedEntity> scriptedEntities;
    std::vector<std::filesystem::path> sourceFiles;
    std::string localError;
    if (!collect_scripted_entities(componentManager, entityManager, workspaceRoot, scriptedEntities, sourceFiles, &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    impl_->trackedEntities = std::move(scriptedEntities);
    if (impl_->trackedEntities.empty())
    {
      impl_->lastError.clear();
      impl_->faulted = false;
      impl_->running = false;
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return true;
    }

    if (!prepare_build(sourceFiles, impl_->buildArtifacts, &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    if (!impl_->process.start(
            {
                "dotnet",
                to_utf8(impl_->buildArtifacts.hostPath),
            },
            impl_->buildArtifacts.outputDirectory,
            &localError))
    {
      impl_->lastError = "Failed to launch the managed C# script host.";
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    if (!impl_->process.write_line("LOAD " + std::to_string(impl_->trackedEntities.size()), &localError))
    {
      impl_->process.stop();
      impl_->lastError = "Failed to initialize the managed C# script host.";
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    for (const auto &entity : impl_->trackedEntities)
    {
      if (!impl_->process.write_line(
              "ENTITY " + std::to_string(entity.entity) + " " + base64_encode(entity.name) + " " +
                  format_float(entity.x) + " " + format_float(entity.y) + " " + format_float(entity.z) + " " +
                  std::to_string(entity.classNames.size()),
              &localError))
      {
        impl_->process.stop();
        impl_->lastError = "Failed to stream entity script metadata into the managed host.";
        impl_->faulted = true;
        if (errorMessage != nullptr)
        {
          *errorMessage = impl_->lastError;
        }
        return false;
      }

      for (const auto &className : entity.classNames)
      {
        if (!impl_->process.write_line("ATTACH " + base64_encode(className), &localError))
        {
          impl_->process.stop();
          impl_->lastError = "Failed to stream script attachments into the managed host.";
          impl_->faulted = true;
          if (errorMessage != nullptr)
          {
            *errorMessage = impl_->lastError;
          }
          return false;
        }
      }
    }

    if (!impl_->process.write_line("END", &localError))
    {
      impl_->process.stop();
      impl_->lastError = "Failed to finalize script host initialization.";
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    std::string response;
    if (!impl_->process.read_line(response, &localError))
    {
      impl_->process.stop();
      impl_->lastError = "The managed script host exited before it finished loading attached scripts.";
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    const auto responseFields = split_fields(response);
    if (!responseFields.empty() && responseFields.front() == "ERROR" && responseFields.size() >= 2)
    {
      impl_->process.stop();
      impl_->lastError = base64_decode(responseFields[1]);
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    if (response != "READY")
    {
      impl_->process.stop();
      impl_->lastError = "The managed script host returned an unexpected initialization response.";
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }
      return false;
    }

    impl_->lastError.clear();
    impl_->faulted = false;
    impl_->running = true;
    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return true;
  }

  void ScriptRuntime::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)entityManager;

    if (!impl_->running || impl_->trackedEntities.empty())
    {
      return;
    }

    for (auto &entity : impl_->trackedEntities)
    {
      if (componentManager.hasComponent<PositionComponent3D>(entity.entity))
      {
        const auto &position = componentManager.getComponent<PositionComponent3D>(entity.entity);
        entity.x = position.x;
        entity.y = position.y;
        entity.z = position.z;
      }
    }

    std::string localError;
    if (!impl_->process.write_line(
            "FRAME " + format_float(deltaTime) + " " + std::to_string(impl_->trackedEntities.size()),
            &localError))
    {
      impl_->lastError = "The managed script host stopped accepting frame updates.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }

    for (const auto &entity : impl_->trackedEntities)
    {
      if (!impl_->process.write_line(
              "ENTITY " + std::to_string(entity.entity) + " " + format_float(entity.x) + " " +
                  format_float(entity.y) + " " + format_float(entity.z),
              &localError))
      {
        impl_->lastError = "Failed to stream frame state into the managed script host.";
        impl_->faulted = true;
        impl_->running = false;
        impl_->process.stop();
        return;
      }
    }

    if (!impl_->process.write_line("END", &localError))
    {
      impl_->lastError = "Failed to finalize the frame state sent to the managed script host.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }

    std::string response;
    if (!impl_->process.read_line(response, &localError))
    {
      impl_->lastError = "The managed script host terminated during play mode.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }

    const auto headerFields = split_fields(response);
    if (!headerFields.empty() && headerFields.front() == "ERROR" && headerFields.size() >= 2)
    {
      impl_->lastError = base64_decode(headerFields[1]);
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }

    if (headerFields.size() != 2 || headerFields[0] != "RESULT")
    {
      impl_->lastError = "The managed script host returned an invalid frame response header.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }

    std::unordered_map<Entity::EntityId, ScriptedEntity *> entitiesById;
    for (auto &entity : impl_->trackedEntities)
    {
      entitiesById[entity.entity] = &entity;
    }

    std::size_t resultCount = 0;
    try
    {
      resultCount = static_cast<std::size_t>(std::stoul(headerFields[1]));
    }
    catch (...)
    {
      impl_->lastError = "The managed script host returned an invalid entity count.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }
    for (std::size_t index = 0; index < resultCount; ++index)
    {
      if (!impl_->process.read_line(response, &localError))
      {
        impl_->lastError = "The managed script host returned an incomplete frame payload.";
        impl_->faulted = true;
        impl_->running = false;
        impl_->process.stop();
        return;
      }

      const auto fields = split_fields(response);
      if (fields.size() != 5 || fields[0] != "ENTITY")
      {
        impl_->lastError = "The managed script host returned malformed entity output.";
        impl_->faulted = true;
        impl_->running = false;
        impl_->process.stop();
        return;
      }

      const Entity::EntityId entityId = static_cast<Entity::EntityId>(std::stoul(fields[1]));
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      if (!parse_float(fields[2], x) || !parse_float(fields[3], y) || !parse_float(fields[4], z))
      {
        impl_->lastError = "The managed script host returned invalid numeric output.";
        impl_->faulted = true;
        impl_->running = false;
        impl_->process.stop();
        return;
      }

      const auto trackedEntityIt = entitiesById.find(entityId);
      if (trackedEntityIt == entitiesById.end())
      {
        continue;
      }

      ScriptedEntity *trackedEntity = trackedEntityIt->second;
      trackedEntity->x = x;
      trackedEntity->y = y;
      trackedEntity->z = z;

      if (componentManager.hasComponent<PositionComponent3D>(entityId))
      {
        auto &position = componentManager.getComponent<PositionComponent3D>(entityId);
        position.x = x;
        position.y = y;
        position.z = z;
      }
    }

    if (!impl_->process.read_line(response, &localError) || response != "END")
    {
      impl_->lastError = "The managed script host frame response was not terminated correctly.";
      impl_->faulted = true;
      impl_->running = false;
      impl_->process.stop();
      return;
    }
  }

  void ScriptRuntime::stop()
  {
    if (impl_->process.is_running())
    {
      std::string ignored;
      impl_->process.write_line("STOP", &ignored);
    }
    impl_->process.stop();
    impl_->trackedEntities.clear();
    impl_->running = false;
    impl_->faulted = false;
    impl_->lastError.clear();
  }

  bool ScriptRuntime::is_running() const
  {
    return impl_->running;
  }

  bool ScriptRuntime::faulted() const
  {
    return impl_->faulted;
  }

  const std::string &ScriptRuntime::last_error() const
  {
    return impl_->lastError;
  }
}
