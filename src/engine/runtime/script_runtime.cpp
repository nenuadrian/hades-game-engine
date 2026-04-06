#include "script_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
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
#include "../core/ecs/world_utils.hpp"
#include "clr_host.hpp"
#include "dotnet_config.hpp"
#include "subprocess.hpp"

namespace hades
{
  namespace
  {
    constexpr char HOST_ASSEMBLY_NAME[] = "HadesScriptHost.dll";

    // Interop structures shared between C++ and C# via blittable layout.
    // These must match the definitions in the generated HostProgram.cs exactly.
#pragma pack(push, 1)
    struct InteropEntityData
    {
      uint32_t entityId;
      float x;
      float y;
      float z;
      int32_t classNameCount;
      // Class names are passed separately via InteropStringArray.
    };

    struct InteropEntityPosition
    {
      uint32_t entityId;
      float x;
      float y;
      float z;
    };

    struct InteropString
    {
      const char *data;
      int32_t length;
    };

    struct InteropLoadResult
    {
      int32_t success;
      const char *errorMessage;
      int32_t errorLength;
    };

    struct InteropUpdateResult
    {
      int32_t success;
      int32_t entityCount;
      const char *errorMessage;
      int32_t errorLength;
    };

    struct InteropEventResult
    {
      int32_t success;
      const char *errorMessage;
      int32_t errorLength;
    };

    struct InteropObservationResult
    {
      int32_t success;
      const char *jsonData;
      int32_t jsonLength;
      const char *errorMessage;
      int32_t errorLength;
    };
#pragma pack(pop)

    // Function pointer types for managed entry points.
    using LoadSceneFn = void (*)(
        const InteropEntityData *entities, int32_t entityCount,
        const InteropString *names, const InteropString *classNames,
        InteropLoadResult *result);

    using UpdateFrameFn = void (*)(
        float deltaTime,
        const InteropEntityPosition *positionsIn, int32_t entityCount,
        InteropEntityPosition *positionsOut,
        InteropUpdateResult *result);

    using KeyEventFn = void (*)(
        int32_t keyCode,
        InteropEventResult *result);

    using CollectObservationsFn = void (*)(
        InteropObservationResult *result);

    using ShutdownFn = void (*)();

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
      std::filesystem::path runtimeConfigPath;
      std::string targetFramework;
    };

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

    std::string dotnet_executable()
    {
      if (dotnet_config::configured_dotnet_executable[0] != '\0')
      {
        return dotnet_config::configured_dotnet_executable;
      }

      return "dotnet";
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
        std::optional<Entity::EntityId> worldRoot,
        std::vector<ScriptedEntity> &scriptedEntities,
        std::vector<std::filesystem::path> &uniqueSourceFiles,
        std::string *errorMessage)
    {
      scriptedEntities.clear();
      uniqueSourceFiles.clear();

      std::set<std::string> seenPaths;

      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (worldRoot.has_value() && !entity_belongs_to_world(entity, *worldRoot, componentManager))
        {
          continue;
        }

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
      return std::string(R"(using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
        public virtual void OnKeyDown(EntityContext context, int keyCode) { }
        public virtual void OnKeyUp(EntityContext context, int keyCode) { }
    }

    public static class HadesAPI
    {
        private static readonly Dictionary<string, string> _observed = new();

        public static void Observe(string key, int value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, float value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, double value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, bool value) => _observed[key] = value ? "true" : "false";
        public static void Observe(string key, string value) => _observed[key] = "\"" + EscapeJson(value ?? "") + "\"";

        public static void Clear() => _observed.Clear();

        internal static string SerializeJson()
        {
            if (_observed.Count == 0) return "{}";
            var sb = new System.Text.StringBuilder("{");
            bool first = true;
            foreach (var kvp in _observed)
            {
                if (!first) sb.Append(',');
                sb.Append('"').Append(EscapeJson(kvp.Key)).Append("\":");
                sb.Append(kvp.Value);
                first = false;
            }
            sb.Append('}');
            return sb.ToString();
        }

        private static string EscapeJson(string s)
        {
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"")
                    .Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t");
        }
    }

    // Interop structures matching the C++ side (packed, blittable).
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropEntityData
    {
        public uint EntityId;
        public float X;
        public float Y;
        public float Z;
        public int ClassNameCount;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropEntityPosition
    {
        public uint EntityId;
        public float X;
        public float Y;
        public float Z;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropString
    {
        public IntPtr Data;
        public int Length;

        public string ToManaged()
        {
            if (Data == IntPtr.Zero || Length <= 0)
                return string.Empty;
            return Marshal.PtrToStringUTF8(Data, Length) ?? string.Empty;
        }
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropLoadResult
    {
        public int Success;
        public IntPtr ErrorMessage;
        public int ErrorLength;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropUpdateResult
    {
        public int Success;
        public int EntityCount;
        public IntPtr ErrorMessage;
        public int ErrorLength;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropEventResult
    {
        public int Success;
        public IntPtr ErrorMessage;
        public int ErrorLength;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct InteropObservationResult
    {
        public int Success;
        public IntPtr JsonData;
        public int JsonLength;
        public IntPtr ErrorMessage;
        public int ErrorLength;
    }

    public static class ScriptHost
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

        private static readonly Dictionary<uint, List<ScriptInstance>> InstancesByEntity = new();
        private static readonly List<uint> EntityOrder = new();
        private static IntPtr _lastErrorPtr = IntPtr.Zero;

        private static void SetError(ref InteropLoadResult result, string message)
        {
            FreeLastError();
            _lastErrorPtr = Marshal.StringToCoTaskMemUTF8(message);
            result.ErrorMessage = _lastErrorPtr;
            result.ErrorLength = System.Text.Encoding.UTF8.GetByteCount(message);
            result.Success = 0;
        }

        private static void SetError(ref InteropUpdateResult result, string message)
        {
            FreeLastError();
            _lastErrorPtr = Marshal.StringToCoTaskMemUTF8(message);
            result.ErrorMessage = _lastErrorPtr;
            result.ErrorLength = System.Text.Encoding.UTF8.GetByteCount(message);
            result.Success = 0;
        }

        private static void SetError(ref InteropEventResult result, string message)
        {
            FreeLastError();
            _lastErrorPtr = Marshal.StringToCoTaskMemUTF8(message);
            result.ErrorMessage = _lastErrorPtr;
            result.ErrorLength = System.Text.Encoding.UTF8.GetByteCount(message);
            result.Success = 0;
        }

        private static void FreeLastError()
        {
            if (_lastErrorPtr != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(_lastErrorPtr);
                _lastErrorPtr = IntPtr.Zero;
            }
        }

        private static Type ResolveScriptType(Assembly assembly, string className)
        {
            var directMatch = assembly.GetType(className, false, false);
            if (directMatch is not null)
                return directMatch;

            var matches = assembly
                .GetTypes()
                .Where(type => string.Equals(type.FullName, className, StringComparison.Ordinal) ||
                               string.Equals(type.Name, className, StringComparison.Ordinal))
                .ToList();

            if (matches.Count == 0)
                throw new InvalidOperationException($"Unable to locate script class '{className}'.");

            if (matches.Count > 1)
                throw new InvalidOperationException(
                    $"Script class name '{className}' is ambiguous. Use the full namespace-qualified name.");

            return matches[0];
        }

        [UnmanagedCallersOnly]
        public static unsafe void LoadScene(
            IntPtr entitiesPtr, int entityCount,
            IntPtr namesPtr, IntPtr classNamesPtr,
            IntPtr resultPtr)
        {
            ref var result = ref Unsafe.AsRef<InteropLoadResult>((void*)resultPtr);
            result.Success = 1;
            result.ErrorMessage = IntPtr.Zero;
            result.ErrorLength = 0;

            try
            {
                InstancesByEntity.Clear();
                EntityOrder.Clear();

                var assembly = Assembly.GetExecutingAssembly();
                int classNameOffset = 0;

                for (int i = 0; i < entityCount; i++)
                {
                    var entityData = Marshal.PtrToStructure<InteropEntityData>(
                        entitiesPtr + i * Marshal.SizeOf<InteropEntityData>());

                    var nameInterop = Marshal.PtrToStructure<InteropString>(
                        namesPtr + i * Marshal.SizeOf<InteropString>());
                    var name = nameInterop.ToManaged();

                    var context = new EntityContext(
                        entityData.EntityId, name,
                        new Vector3(entityData.X, entityData.Y, entityData.Z));

                    var instances = new List<ScriptInstance>(entityData.ClassNameCount);

                    for (int j = 0; j < entityData.ClassNameCount; j++)
                    {
                        var classNameInterop = Marshal.PtrToStructure<InteropString>(
                            classNamesPtr + (classNameOffset + j) * Marshal.SizeOf<InteropString>());
                        var className = classNameInterop.ToManaged();

                        var type = ResolveScriptType(assembly, className);
                        if (!typeof(HadesScript).IsAssignableFrom(type))
                            throw new InvalidOperationException(
                                $"Type '{className}' must derive from Hades.Scripting.HadesScript.");

                        if (Activator.CreateInstance(type) is not HadesScript script)
                            throw new InvalidOperationException(
                                $"Type '{className}' must have a public parameterless constructor.");

                        instances.Add(new ScriptInstance(script, context));
                    }

                    classNameOffset += entityData.ClassNameCount;
                    InstancesByEntity[entityData.EntityId] = instances;
                    EntityOrder.Add(entityData.EntityId);
                }

                // Call OnStart for all script instances.
                foreach (var entityId in EntityOrder)
                {
                    foreach (var instance in InstancesByEntity[entityId])
                    {
                        instance.Script.OnStart(instance.Context);
                    }
                }
            }
            catch (Exception ex)
            {
                SetError(ref result, ex.Message);
            }
        }
      )") + R"(

        [UnmanagedCallersOnly]
        public static unsafe void UpdateFrame(
            float deltaTime,
            IntPtr positionsInPtr, int entityCount,
            IntPtr positionsOutPtr,
            IntPtr resultPtr)
        {
            ref var result = ref Unsafe.AsRef<InteropUpdateResult>((void*)resultPtr);
            result.Success = 1;
            result.EntityCount = 0;
            result.ErrorMessage = IntPtr.Zero;
            result.ErrorLength = 0;

            try
            {
                // Update positions from engine.
                for (int i = 0; i < entityCount; i++)
                {
                    var pos = Marshal.PtrToStructure<InteropEntityPosition>(
                        positionsInPtr + i * Marshal.SizeOf<InteropEntityPosition>());

                    if (InstancesByEntity.TryGetValue(pos.EntityId, out var instances))
                    {
                        var newPos = new Vector3(pos.X, pos.Y, pos.Z);
                        foreach (var instance in instances)
                        {
                            instance.Context.Position = newPos;
                        }
                    }
                }

                // Call OnUpdate for all script instances.
                foreach (var entityId in EntityOrder)
                {
                    if (InstancesByEntity.TryGetValue(entityId, out var instances))
                    {
                        foreach (var instance in instances)
                        {
                            instance.Script.OnUpdate(instance.Context, deltaTime);
                        }
                    }
                }

                // Write back updated positions.
                int outIndex = 0;
                foreach (var entityId in EntityOrder)
                {
                    if (!InstancesByEntity.TryGetValue(entityId, out var instances) || instances.Count == 0)
                        continue;

                    var context = instances[0].Context;
                    var outPos = new InteropEntityPosition
                    {
                        EntityId = context.EntityId,
                        X = context.Position.X,
                        Y = context.Position.Y,
                        Z = context.Position.Z,
                    };
                    Marshal.StructureToPtr(outPos,
                        positionsOutPtr + outIndex * Marshal.SizeOf<InteropEntityPosition>(), false);
                    outIndex++;
                }

                result.EntityCount = outIndex;
            }
            catch (Exception ex)
            {
                SetError(ref result, ex.Message);
            }
        }

        [UnmanagedCallersOnly]
        public static unsafe void OnKeyDown(
            int keyCode,
            IntPtr resultPtr)
        {
            ref var result = ref Unsafe.AsRef<InteropEventResult>((void*)resultPtr);
            result.Success = 1;
            result.ErrorMessage = IntPtr.Zero;
            result.ErrorLength = 0;

            try
            {
                foreach (var entityId in EntityOrder)
                {
                    if (InstancesByEntity.TryGetValue(entityId, out var instances))
                    {
                        foreach (var instance in instances)
                        {
                            instance.Script.OnKeyDown(instance.Context, keyCode);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                SetError(ref result, ex.Message);
            }
        }

        [UnmanagedCallersOnly]
        public static unsafe void OnKeyUp(
            int keyCode,
            IntPtr resultPtr)
        {
            ref var result = ref Unsafe.AsRef<InteropEventResult>((void*)resultPtr);
            result.Success = 1;
            result.ErrorMessage = IntPtr.Zero;
            result.ErrorLength = 0;

            try
            {
                foreach (var entityId in EntityOrder)
                {
                    if (InstancesByEntity.TryGetValue(entityId, out var instances))
                    {
                        foreach (var instance in instances)
                        {
                            instance.Script.OnKeyUp(instance.Context, keyCode);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                SetError(ref result, ex.Message);
            }
        }

        private static IntPtr _lastObsPtr = IntPtr.Zero;

        [UnmanagedCallersOnly]
        public static unsafe void CollectObservations(IntPtr resultPtr)
        {
            ref var result = ref Unsafe.AsRef<InteropObservationResult>((void*)resultPtr);
            result.Success = 1;
            result.JsonData = IntPtr.Zero;
            result.JsonLength = 0;
            result.ErrorMessage = IntPtr.Zero;
            result.ErrorLength = 0;

            try
            {
                if (_lastObsPtr != IntPtr.Zero)
                {
                    Marshal.FreeCoTaskMem(_lastObsPtr);
                    _lastObsPtr = IntPtr.Zero;
                }

                string json = HadesAPI.SerializeJson();
                _lastObsPtr = Marshal.StringToCoTaskMemUTF8(json);
                result.JsonData = _lastObsPtr;
                result.JsonLength = System.Text.Encoding.UTF8.GetByteCount(json);
            }
            catch (Exception ex)
            {
                SetError(ref result, ex.Message);
            }
        }

        private static void SetError(ref InteropObservationResult result, string message)
        {
            FreeLastError();
            _lastErrorPtr = Marshal.StringToCoTaskMemUTF8(message);
            result.ErrorMessage = _lastErrorPtr;
            result.ErrorLength = System.Text.Encoding.UTF8.GetByteCount(message);
            result.Success = 0;
        }

        [UnmanagedCallersOnly]
        public static void Shutdown()
        {
            InstancesByEntity.Clear();
            EntityOrder.Clear();
            FreeLastError();
            if (_lastObsPtr != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(_lastObsPtr);
                _lastObsPtr = IntPtr.Zero;
            }
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
              << "    <OutputType>Library</OutputType>\n"
              << "    <TargetFramework>" << xml_escape(targetFramework) << "</TargetFramework>\n"
              << "    <ImplicitUsings>enable</ImplicitUsings>\n"
              << "    <Nullable>enable</Nullable>\n"
              << "    <LangVersion>latest</LangVersion>\n"
              << "    <AssemblyName>HadesScriptHost</AssemblyName>\n"
              << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
              << "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n"
              << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
              << "    <Deterministic>false</Deterministic>\n"
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

    std::string render_runtime_config(const std::string &targetFramework)
    {
      // Extract version from target framework (e.g., "net8.0" -> "8.0.0").
      std::string version = "8.0.0";
      if (targetFramework.size() > 3 && targetFramework.substr(0, 3) == "net")
      {
        version = targetFramework.substr(3);
        // Ensure it has a patch component.
        if (version.find('.') != std::string::npos &&
            version.rfind('.') == version.find('.'))
        {
          version += ".0";
        }
      }

      return "{\n"
             "  \"runtimeOptions\": {\n"
             "    \"tfm\": \"" +
             targetFramework +
             "\",\n"
             "    \"framework\": {\n"
             "      \"name\": \"Microsoft.NETCore.App\",\n"
             "      \"version\": \"" +
             version +
             "\"\n"
             "    }\n"
             "  }\n"
             "}\n";
    }

    std::string common_compile_hints(const std::vector<std::filesystem::path> &sourceFiles)
    {
      for (const auto &sourceFile : sourceFiles)
      {
        std::ifstream input(sourceFile, std::ios::binary);
        if (!input)
        {
          continue;
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        const std::string sourceText = contents.str();
        if (sourceText.find("System.out.println") != std::string::npos)
        {
          return "\nHint: Hades scripts use C#. Replace System.out.println(...) with System.Console.WriteLine(...).";
        }
      }

      return {};
    }

    bool prepare_build(
        const std::vector<std::filesystem::path> &sourceFiles,
        BuildArtifacts &artifacts,
        std::string *errorMessage)
    {
      const std::string dotnetCommand = dotnet_executable();
      const ProcessResult dotnetVersion = Subprocess::run_capture({dotnetCommand, "--version"});
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
          if (dotnetVersion.exitCode == 127)
          {
            *errorMessage = "Could not launch the configured dotnet SDK command: " + dotnetCommand +
                            ". Re-run CMake after installing .NET SDK 7.0+ or make dotnet available on PATH.";
          }
          else
          {
            *errorMessage = "Failed to query the installed dotnet SDK version.\n" + dotnetVersion.output;
          }
        }
        return false;
      }

      const auto majorVersion = parse_dotnet_major_version(dotnetVersion.output);
      if (!majorVersion.has_value() || *majorVersion < 7)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Hades scripting requires dotnet SDK 7.0 or newer (for [UnmanagedCallersOnly] support).";
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
      artifacts.runtimeConfigPath = artifacts.outputDirectory / "HadesScriptHost.runtimeconfig.json";

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
              dotnetCommand,
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
          *errorMessage = "C# script compilation failed.\n" + buildResult.output +
                          common_compile_hints(sourceFiles);
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

      // Write the runtimeconfig.json if dotnet build did not produce one.
      if (!std::filesystem::exists(artifacts.runtimeConfigPath))
      {
        if (!write_text_file(
                artifacts.runtimeConfigPath,
                render_runtime_config(artifacts.targetFramework),
                errorMessage))
        {
          return false;
        }
      }

      return true;
    }
  }

  struct ScriptRuntime::Impl
  {
    std::vector<ScriptedEntity> trackedEntities;
    BuildArtifacts buildArtifacts;
    ClrHost clrHost;
    std::string lastError;
    bool running = false;
    bool faulted = false;

    LoadSceneFn loadSceneFn = nullptr;
    UpdateFrameFn updateFrameFn = nullptr;
    KeyEventFn keyDownFn = nullptr;
    KeyEventFn keyUpFn = nullptr;
    CollectObservationsFn collectObservationsFn = nullptr;
    ShutdownFn shutdownFn = nullptr;
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
    return start(componentManager, entityManager, std::filesystem::path(), std::nullopt, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, workspaceRoot, std::nullopt, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::optional<Entity::EntityId> worldRoot,
      std::string *errorMessage)
  {
    stop();

    std::vector<ScriptedEntity> scriptedEntities;
    std::vector<std::filesystem::path> sourceFiles;
    std::string localError;
    if (!collect_scripted_entities(
            componentManager,
            entityManager,
            workspaceRoot,
            worldRoot,
            scriptedEntities,
            sourceFiles,
            &localError))
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

    // Initialize the CLR host with the runtime config.
    if (!impl_->clrHost.initialize(impl_->buildArtifacts.runtimeConfigPath, &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    // Get managed function pointers.
    const std::string assemblyPath = impl_->buildArtifacts.hostPath.string();
    const std::string typeName = "Hades.Scripting.ScriptHost, HadesScriptHost";

    void *loadScenePtr = nullptr;
    void *updateFramePtr = nullptr;
    void *keyDownPtr = nullptr;
    void *keyUpPtr = nullptr;
    void *collectObsPtr = nullptr;
    void *shutdownPtr = nullptr;

    if (!impl_->clrHost.get_managed_function(assemblyPath, typeName, "LoadScene", &loadScenePtr, &localError) ||
        !impl_->clrHost.get_managed_function(assemblyPath, typeName, "UpdateFrame", &updateFramePtr, &localError) ||
        !impl_->clrHost.get_managed_function(assemblyPath, typeName, "OnKeyDown", &keyDownPtr, &localError) ||
        !impl_->clrHost.get_managed_function(assemblyPath, typeName, "OnKeyUp", &keyUpPtr, &localError) ||
        !impl_->clrHost.get_managed_function(assemblyPath, typeName, "CollectObservations", &collectObsPtr, &localError) ||
        !impl_->clrHost.get_managed_function(assemblyPath, typeName, "Shutdown", &shutdownPtr, &localError))
    {
      impl_->clrHost.close();
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    impl_->loadSceneFn = reinterpret_cast<LoadSceneFn>(loadScenePtr);
    impl_->updateFrameFn = reinterpret_cast<UpdateFrameFn>(updateFramePtr);
    impl_->keyDownFn = reinterpret_cast<KeyEventFn>(keyDownPtr);
    impl_->keyUpFn = reinterpret_cast<KeyEventFn>(keyUpPtr);
    impl_->collectObservationsFn = reinterpret_cast<CollectObservationsFn>(collectObsPtr);
    impl_->shutdownFn = reinterpret_cast<ShutdownFn>(shutdownPtr);

    // Prepare interop data for LoadScene.
    const std::size_t entityCount = impl_->trackedEntities.size();
    std::vector<InteropEntityData> entityDataVec(entityCount);
    std::vector<InteropString> nameVec(entityCount);
    std::vector<InteropString> classNameVec;

    // Count total class names.
    std::size_t totalClassNames = 0;
    for (const auto &entity : impl_->trackedEntities)
    {
      totalClassNames += entity.classNames.size();
    }
    classNameVec.resize(totalClassNames);

    std::size_t classNameOffset = 0;
    for (std::size_t i = 0; i < entityCount; ++i)
    {
      const auto &entity = impl_->trackedEntities[i];
      entityDataVec[i].entityId = entity.entity;
      entityDataVec[i].x = entity.x;
      entityDataVec[i].y = entity.y;
      entityDataVec[i].z = entity.z;
      entityDataVec[i].classNameCount = static_cast<int32_t>(entity.classNames.size());

      nameVec[i].data = entity.name.c_str();
      nameVec[i].length = static_cast<int32_t>(entity.name.size());

      for (std::size_t j = 0; j < entity.classNames.size(); ++j)
      {
        classNameVec[classNameOffset + j].data = entity.classNames[j].c_str();
        classNameVec[classNameOffset + j].length = static_cast<int32_t>(entity.classNames[j].size());
      }
      classNameOffset += entity.classNames.size();
    }

    InteropLoadResult loadResult{};
    impl_->loadSceneFn(
        entityDataVec.data(), static_cast<int32_t>(entityCount),
        nameVec.data(), classNameVec.data(),
        &loadResult);

    if (loadResult.success == 0)
    {
      std::string managedError;
      if (loadResult.errorMessage != nullptr && loadResult.errorLength > 0)
      {
        managedError.assign(
            reinterpret_cast<const char *>(loadResult.errorMessage),
            static_cast<std::size_t>(loadResult.errorLength));
      }
      else
      {
        managedError = "The managed script host failed to load the scene.";
      }

      impl_->lastError = managedError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = managedError;
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

    if (!impl_->running || impl_->trackedEntities.empty() || impl_->updateFrameFn == nullptr)
    {
      return;
    }

    const std::size_t entityCount = impl_->trackedEntities.size();

    // Build input positions.
    std::vector<InteropEntityPosition> positionsIn(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i)
    {
      auto &entity = impl_->trackedEntities[i];
      if (componentManager.hasComponent<PositionComponent3D>(entity.entity))
      {
        const auto &position = componentManager.getComponent<PositionComponent3D>(entity.entity);
        entity.x = position.x;
        entity.y = position.y;
        entity.z = position.z;
      }

      positionsIn[i].entityId = entity.entity;
      positionsIn[i].x = entity.x;
      positionsIn[i].y = entity.y;
      positionsIn[i].z = entity.z;
    }

    // Allocate output positions.
    std::vector<InteropEntityPosition> positionsOut(entityCount);

    InteropUpdateResult updateResult{};
    impl_->updateFrameFn(
        deltaTime,
        positionsIn.data(), static_cast<int32_t>(entityCount),
        positionsOut.data(),
        &updateResult);

    if (updateResult.success == 0)
    {
      std::string managedError;
      if (updateResult.errorMessage != nullptr && updateResult.errorLength > 0)
      {
        managedError.assign(
            reinterpret_cast<const char *>(updateResult.errorMessage),
            static_cast<std::size_t>(updateResult.errorLength));
      }
      else
      {
        managedError = "A managed script threw an exception during OnUpdate.";
      }

      impl_->lastError = managedError;
      impl_->faulted = true;
      impl_->running = false;
      return;
    }

    // Write back positions from managed code.
    std::unordered_map<Entity::EntityId, ScriptedEntity *> entitiesById;
    for (auto &entity : impl_->trackedEntities)
    {
      entitiesById[entity.entity] = &entity;
    }

    const int32_t resultCount = updateResult.entityCount;
    for (int32_t i = 0; i < resultCount; ++i)
    {
      const auto &out = positionsOut[static_cast<std::size_t>(i)];
      const auto it = entitiesById.find(out.entityId);
      if (it == entitiesById.end())
      {
        continue;
      }

      ScriptedEntity *tracked = it->second;
      tracked->x = out.x;
      tracked->y = out.y;
      tracked->z = out.z;

      if (componentManager.hasComponent<PositionComponent3D>(out.entityId))
      {
        auto &position = componentManager.getComponent<PositionComponent3D>(out.entityId);
        position.x = out.x;
        position.y = out.y;
        position.z = out.z;
      }
    }
  }

  void ScriptRuntime::stop()
  {
    if (impl_->shutdownFn != nullptr)
    {
      impl_->shutdownFn();
    }
    impl_->clrHost.close();
    impl_->loadSceneFn = nullptr;
    impl_->updateFrameFn = nullptr;
    impl_->keyDownFn = nullptr;
    impl_->keyUpFn = nullptr;
    impl_->collectObservationsFn = nullptr;
    impl_->shutdownFn = nullptr;
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

  void ScriptRuntime::on_key_down(int keyCode)
  {
    if (!impl_->running || impl_->trackedEntities.empty() || impl_->keyDownFn == nullptr)
    {
      return;
    }

    InteropEventResult result{};
    impl_->keyDownFn(static_cast<int32_t>(keyCode), &result);

    if (result.success != 0)
    {
      return;
    }

    if (result.errorMessage != nullptr && result.errorLength > 0)
    {
      impl_->lastError.assign(
          reinterpret_cast<const char *>(result.errorMessage),
          static_cast<std::size_t>(result.errorLength));
    }
    else
    {
      impl_->lastError = "A managed script threw an exception during OnKeyDown.";
    }

    impl_->faulted = true;
    impl_->running = false;
  }

  void ScriptRuntime::on_key_up(int keyCode)
  {
    if (!impl_->running || impl_->trackedEntities.empty() || impl_->keyUpFn == nullptr)
    {
      return;
    }

    InteropEventResult result{};
    impl_->keyUpFn(static_cast<int32_t>(keyCode), &result);

    if (result.success != 0)
    {
      return;
    }

    if (result.errorMessage != nullptr && result.errorLength > 0)
    {
      impl_->lastError.assign(
          reinterpret_cast<const char *>(result.errorMessage),
          static_cast<std::size_t>(result.errorLength));
    }
    else
    {
      impl_->lastError = "A managed script threw an exception during OnKeyUp.";
    }

    impl_->faulted = true;
    impl_->running = false;
  }

  std::string ScriptRuntime::collect_observations() const
  {
    if (!impl_->running || impl_->collectObservationsFn == nullptr)
    {
      return "{}";
    }

    InteropObservationResult result{};
    impl_->collectObservationsFn(&result);

    if (result.success == 0 || result.jsonData == nullptr || result.jsonLength <= 0)
    {
      return "{}";
    }

    return std::string(
        reinterpret_cast<const char *>(result.jsonData),
        static_cast<std::size_t>(result.jsonLength));
  }

  bool ScriptRuntime::compile(
      const std::vector<std::filesystem::path> &sourceFiles,
      std::string *errorMessage)
  {
    if (sourceFiles.empty())
    {
      return true;
    }

    BuildArtifacts artifacts;
    return prepare_build(sourceFiles, artifacts, errorMessage);
  }
}
