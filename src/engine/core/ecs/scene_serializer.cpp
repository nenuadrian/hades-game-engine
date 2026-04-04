#include "scene_serializer.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "component_manager.hpp"
#include "entity_manager.hpp"
#include "../../assets/model_importer.hpp"
#include "../../components/audio_listener_component.hpp"
#include "../../components/audio_source_component.hpp"
#include "../../components/camera_component.hpp"
#include "../../components/collider_component.hpp"
#include "../../components/light_component.hpp"
#include "../../components/model_component.hpp"
#include "../../components/name_component.hpp"
#include "../../components/position_component_3d.hpp"
#include "../../components/primitive_component.hpp"
#include "../../components/render_component.hpp"
#include "../../components/rigid_body_component.hpp"
#include "../../components/rotation_component_3d.hpp"
#include "../../components/scale_component_3d.hpp"
#include "../../components/script_component.hpp"
#include "../../components/text_component.hpp"
#include "../../components/transform_hierarchy_component.hpp"
#include "../../components/world_component.hpp"

using json = nlohmann::json;

namespace hades
{
  namespace
  {
    constexpr int WORLD_FORMAT_VERSION = 1;

    std::filesystem::path worlds_directory(const std::filesystem::path &workspacePath)
    {
      return workspacePath / ".hades" / "worlds";
    }

    std::string sanitize_filename(const std::string &name)
    {
      std::string result;
      result.reserve(name.size());
      for (char ch : name)
      {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_')
        {
          result.push_back(ch);
        }
        else
        {
          result.push_back('_');
        }
      }
      if (result.empty())
      {
        result = "Untitled";
      }
      return result;
    }

    void collect_entities_recursive(
        Entity::EntityId entity,
        ComponentManager &componentManager,
        std::vector<Entity::EntityId> &out)
    {
      out.push_back(entity);
      if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        const auto &h = componentManager.getComponent<TransformHierarchyComponent>(entity);
        for (Entity::EntityId child : h.children)
        {
          collect_entities_recursive(child, componentManager, out);
        }
      }
    }

    json serialize_entity(Entity::EntityId entity, ComponentManager &componentManager)
    {
      json j;
      j["id"] = entity;
      json components = json::object();

      if (componentManager.hasComponent<NameComponent>(entity))
      {
        const auto &c = componentManager.getComponent<NameComponent>(entity);
        components["name"] = {{"value", c.value}};
      }

      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        const auto &c = componentManager.getComponent<PositionComponent3D>(entity);
        components["position3d"] = {{"x", c.x}, {"y", c.y}, {"z", c.z}};
      }

      if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        const auto &c = componentManager.getComponent<TransformHierarchyComponent>(entity);
        json h;
        h["parent"] = c.parent.has_value() ? json(*c.parent) : json(nullptr);
        h["children"] = c.children;
        components["hierarchy"] = h;
      }

      if (componentManager.hasComponent<WorldComponent>(entity))
      {
        const auto &c = componentManager.getComponent<WorldComponent>(entity);
        components["world"] = {{"isDefault", c.isDefault}};
      }

      if (componentManager.hasComponent<CameraComponent>(entity))
      {
        const auto &c = componentManager.getComponent<CameraComponent>(entity);
        components["camera"] = {
            {"isMainCamera", c.isMainCamera},
            {"fovY", c.fovY},
            {"nearClip", c.nearClip},
            {"farClip", c.farClip}};
      }

      if (componentManager.hasComponent<PrimitiveComponent>(entity))
      {
        const auto &c = componentManager.getComponent<PrimitiveComponent>(entity);
        components["primitive"] = {{"type", static_cast<int>(c.type)}};
      }

      if (componentManager.hasComponent<TextComponent>(entity))
      {
        const auto &c = componentManager.getComponent<TextComponent>(entity);
        components["text"] = {
            {"content", c.content},
            {"fontSize", c.fontSize},
            {"wrapWidth", c.wrapWidth},
            {"lineSpacing", c.lineSpacing},
            {"yawDegrees", c.yawDegrees},
            {"pitchDegrees", c.pitchDegrees},
            {"rollDegrees", c.rollDegrees}};
      }

      if (componentManager.hasComponent<AudioListenerComponent>(entity))
      {
        const auto &c = componentManager.getComponent<AudioListenerComponent>(entity);
        components["audioListener"] = {
            {"enabled", c.enabled},
            {"forwardX", c.forwardX},
            {"forwardY", c.forwardY},
            {"forwardZ", c.forwardZ},
            {"upX", c.upX},
            {"upY", c.upY},
            {"upZ", c.upZ}};
      }

      if (componentManager.hasComponent<AudioSourceComponent>(entity))
      {
        const auto &c = componentManager.getComponent<AudioSourceComponent>(entity);
        components["audioSource"] = {
            {"assetPath", c.assetPath},
            {"bus", static_cast<int>(c.bus)},
            {"playOnStart", c.playOnStart},
            {"looping", c.looping},
            {"streaming", c.streaming},
            {"spatialized", c.spatialized},
            {"volume", c.volume},
            {"pitch", c.pitch},
            {"minDistance", c.minDistance},
            {"maxDistance", c.maxDistance},
            {"rolloff", c.rolloff}};
      }

      if (componentManager.hasComponent<ScriptComponent>(entity))
      {
        const auto &c = componentManager.getComponent<ScriptComponent>(entity);
        json attachments = json::array();
        for (const auto &a : c.attachments)
        {
          json attachment;
          attachment["scriptPath"] = a.scriptPath;
          attachment["className"] = a.className;
          attachment["enabled"] = a.enabled;
          attachment["publicFieldValues"] = a.publicFieldValues;
          attachments.push_back(attachment);
        }
        components["script"] = {{"attachments", attachments}};
      }

      if (componentManager.hasComponent<ModelComponent>(entity))
      {
        const auto &c = componentManager.getComponent<ModelComponent>(entity);
        components["model"] = {
            {"sourcePath", c.model.sourcePath},
            {"formatHint", c.model.formatHint}};
      }

      if (componentManager.hasComponent<RotationComponent3D>(entity))
      {
        const auto &c = componentManager.getComponent<RotationComponent3D>(entity);
        components["rotation3d"] = {{"qx", c.qx}, {"qy", c.qy}, {"qz", c.qz}, {"qw", c.qw}};
      }

      if (componentManager.hasComponent<ScaleComponent3D>(entity))
      {
        const auto &c = componentManager.getComponent<ScaleComponent3D>(entity);
        components["scale3d"] = {{"x", c.x}, {"y", c.y}, {"z", c.z}};
      }

      if (componentManager.hasComponent<RigidBodyComponent>(entity))
      {
        const auto &c = componentManager.getComponent<RigidBodyComponent>(entity);
        components["rigidBody"] = {
            {"type", static_cast<int>(c.type)},
            {"mass", c.mass},
            {"linearDamping", c.linearDamping},
            {"angularDamping", c.angularDamping},
            {"friction", c.friction},
            {"restitution", c.restitution},
            {"gravityScale", c.gravityScale}};
      }

      if (componentManager.hasComponent<ColliderComponent>(entity))
      {
        const auto &c = componentManager.getComponent<ColliderComponent>(entity);
        components["collider"] = {
            {"shape", static_cast<int>(c.shape)},
            {"halfExtentX", c.halfExtentX},
            {"halfExtentY", c.halfExtentY},
            {"halfExtentZ", c.halfExtentZ},
            {"radius", c.radius},
            {"capsuleHalfHeight", c.capsuleHalfHeight},
            {"capsuleRadius", c.capsuleRadius}};
      }

      if (componentManager.hasComponent<LightComponent>(entity))
      {
        const auto &c = componentManager.getComponent<LightComponent>(entity);
        components["light"] = {
            {"type", static_cast<int>(c.type)},
            {"colorR", c.colorR},
            {"colorG", c.colorG},
            {"colorB", c.colorB},
            {"intensity", c.intensity},
            {"range", c.range},
            {"directionX", c.directionX},
            {"directionY", c.directionY},
            {"directionZ", c.directionZ},
            {"innerConeAngle", c.innerConeAngle},
            {"outerConeAngle", c.outerConeAngle},
            {"ambientContribution", c.ambientContribution},
            {"castShadows", c.castShadows},
            {"enabled", c.enabled}};
      }

      j["components"] = components;
      return j;
    }

    void deserialize_entity(
        const json &j,
        Entity::EntityId newEntity,
        ComponentManager &componentManager,
        const std::unordered_map<Entity::EntityId, Entity::EntityId> &idMap)
    {
      const auto &components = j["components"];

      if (components.contains("name"))
      {
        NameComponent c;
        c.value = components["name"]["value"].get<std::string>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("position3d"))
      {
        const auto &p = components["position3d"];
        PositionComponent3D c(
            p["x"].get<float>(),
            p["y"].get<float>(),
            p["z"].get<float>());
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("hierarchy"))
      {
        const auto &h = components["hierarchy"];
        TransformHierarchyComponent c;

        if (!h["parent"].is_null())
        {
          Entity::EntityId oldParent = h["parent"].get<Entity::EntityId>();
          auto it = idMap.find(oldParent);
          if (it != idMap.end())
          {
            c.parent = it->second;
          }
        }

        for (const auto &childId : h["children"])
        {
          Entity::EntityId oldChild = childId.get<Entity::EntityId>();
          auto it = idMap.find(oldChild);
          if (it != idMap.end())
          {
            c.children.push_back(it->second);
          }
        }

        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("world"))
      {
        WorldComponent c;
        c.isDefault = components["world"]["isDefault"].get<bool>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("camera"))
      {
        const auto &cam = components["camera"];
        CameraComponent c;
        c.isMainCamera = cam["isMainCamera"].get<bool>();
        c.fovY = cam["fovY"].get<float>();
        c.nearClip = cam["nearClip"].get<float>();
        c.farClip = cam["farClip"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("primitive"))
      {
        PrimitiveComponent c;
        c.type = static_cast<PrimitiveType>(components["primitive"]["type"].get<int>());
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("text"))
      {
        const auto &t = components["text"];
        TextComponent c;
        c.content = t["content"].get<std::string>();
        c.fontSize = t["fontSize"].get<float>();
        c.wrapWidth = t["wrapWidth"].get<float>();
        c.lineSpacing = t["lineSpacing"].get<float>();
        c.yawDegrees = t["yawDegrees"].get<float>();
        c.pitchDegrees = t["pitchDegrees"].get<float>();
        c.rollDegrees = t["rollDegrees"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("audioListener"))
      {
        const auto &al = components["audioListener"];
        AudioListenerComponent c;
        c.enabled = al["enabled"].get<bool>();
        c.forwardX = al["forwardX"].get<float>();
        c.forwardY = al["forwardY"].get<float>();
        c.forwardZ = al["forwardZ"].get<float>();
        c.upX = al["upX"].get<float>();
        c.upY = al["upY"].get<float>();
        c.upZ = al["upZ"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("audioSource"))
      {
        const auto &as = components["audioSource"];
        AudioSourceComponent c;
        c.assetPath = as["assetPath"].get<std::string>();
        c.bus = static_cast<AudioBus>(as["bus"].get<int>());
        c.playOnStart = as["playOnStart"].get<bool>();
        c.looping = as["looping"].get<bool>();
        c.streaming = as["streaming"].get<bool>();
        c.spatialized = as["spatialized"].get<bool>();
        c.volume = as["volume"].get<float>();
        c.pitch = as["pitch"].get<float>();
        c.minDistance = as["minDistance"].get<float>();
        c.maxDistance = as["maxDistance"].get<float>();
        c.rolloff = as["rolloff"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("script"))
      {
        ScriptComponent c;
        for (const auto &a : components["script"]["attachments"])
        {
          ScriptAttachment attachment;
          attachment.scriptPath = a["scriptPath"].get<std::string>();
          attachment.className = a["className"].get<std::string>();
          attachment.enabled = a["enabled"].get<bool>();
          attachment.publicFieldValues = a["publicFieldValues"].get<std::map<std::string, std::string>>();
          c.attachments.push_back(std::move(attachment));
        }
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("model"))
      {
        const auto &m = components["model"];
        const std::string sourcePath = m["sourcePath"].get<std::string>();
        auto imported = ModelImporter::importFromFile(sourcePath);
        if (imported.has_value())
        {
          ModelComponent c;
          c.model = std::move(*imported);
          componentManager.addComponent(newEntity, c);
        }
        else
        {
          std::fprintf(stderr, "Warning: failed to re-import model from '%s'\n", sourcePath.c_str());
        }
      }

      if (components.contains("rotation3d"))
      {
        const auto &r = components["rotation3d"];
        RotationComponent3D c;
        c.qx = r["qx"].get<float>();
        c.qy = r["qy"].get<float>();
        c.qz = r["qz"].get<float>();
        c.qw = r["qw"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("scale3d"))
      {
        const auto &s = components["scale3d"];
        ScaleComponent3D c;
        c.x = s["x"].get<float>();
        c.y = s["y"].get<float>();
        c.z = s["z"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("rigidBody"))
      {
        const auto &rb = components["rigidBody"];
        RigidBodyComponent c;
        c.type = static_cast<RigidBodyType>(rb["type"].get<int>());
        c.mass = rb["mass"].get<float>();
        c.linearDamping = rb["linearDamping"].get<float>();
        c.angularDamping = rb["angularDamping"].get<float>();
        c.friction = rb["friction"].get<float>();
        c.restitution = rb["restitution"].get<float>();
        c.gravityScale = rb["gravityScale"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("collider"))
      {
        const auto &col = components["collider"];
        ColliderComponent c;
        c.shape = static_cast<ColliderShape>(col["shape"].get<int>());
        c.halfExtentX = col["halfExtentX"].get<float>();
        c.halfExtentY = col["halfExtentY"].get<float>();
        c.halfExtentZ = col["halfExtentZ"].get<float>();
        c.radius = col["radius"].get<float>();
        c.capsuleHalfHeight = col["capsuleHalfHeight"].get<float>();
        c.capsuleRadius = col["capsuleRadius"].get<float>();
        componentManager.addComponent(newEntity, c);
      }

      if (components.contains("light"))
      {
        const auto &l = components["light"];
        LightComponent c;
        c.type = static_cast<LightType>(l["type"].get<int>());
        c.colorR = l["colorR"].get<float>();
        c.colorG = l["colorG"].get<float>();
        c.colorB = l["colorB"].get<float>();
        c.intensity = l["intensity"].get<float>();
        c.range = l["range"].get<float>();
        c.directionX = l["directionX"].get<float>();
        c.directionY = l["directionY"].get<float>();
        c.directionZ = l["directionZ"].get<float>();
        c.innerConeAngle = l["innerConeAngle"].get<float>();
        c.outerConeAngle = l["outerConeAngle"].get<float>();
        c.ambientContribution = l["ambientContribution"].get<float>();
        c.castShadows = l["castShadows"].get<bool>();
        c.enabled = l["enabled"].get<bool>();
        componentManager.addComponent(newEntity, c);
      }
    }

    bool write_json_to_file(const std::filesystem::path &filePath, const json &root, std::string *errorMessage)
    {
      std::error_code ec;
      const auto parentDir = filePath.parent_path();
      if (!parentDir.empty())
      {
        std::filesystem::create_directories(parentDir, ec);
      }

      std::ofstream file(filePath);
      if (!file.is_open())
      {
        if (errorMessage)
        {
          *errorMessage = "Failed to open file for writing: " + filePath.string();
        }
        return false;
      }

      file << root.dump(2);
      if (file.fail())
      {
        if (errorMessage)
        {
          *errorMessage = "Failed to write data to: " + filePath.string();
        }
        return false;
      }

      return true;
    }

    bool read_json_from_file(const std::filesystem::path &filePath, json &root, std::string *errorMessage)
    {
      std::ifstream file(filePath);
      if (!file.is_open())
      {
        if (errorMessage)
        {
          *errorMessage = "Failed to open file: " + filePath.string();
        }
        return false;
      }

      try
      {
        file >> root;
      }
      catch (const json::parse_error &e)
      {
        if (errorMessage)
        {
          *errorMessage = std::string("JSON parse error: ") + e.what();
        }
        return false;
      }

      return true;
    }
  }

  bool save_world(
      const std::filesystem::path &filePath,
      Entity::EntityId worldEntity,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage)
  {
    std::vector<Entity::EntityId> worldEntities;
    collect_entities_recursive(worldEntity, componentManager, worldEntities);

    json root;
    root["version"] = WORLD_FORMAT_VERSION;

    json entities = json::array();
    for (Entity::EntityId entity : worldEntities)
    {
      entities.push_back(serialize_entity(entity, componentManager));
    }
    root["entities"] = entities;

    return write_json_to_file(filePath, root, errorMessage);
  }

  std::optional<Entity::EntityId> load_world_from_file(
      const std::filesystem::path &filePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage)
  {
    json root;
    if (!read_json_from_file(filePath, root, errorMessage))
    {
      return std::nullopt;
    }

    if (!root.contains("version") || !root.contains("entities"))
    {
      if (errorMessage)
      {
        *errorMessage = "Invalid world file format.";
      }
      return std::nullopt;
    }

    const auto &entitiesJson = root["entities"];
    if (entitiesJson.empty())
    {
      if (errorMessage)
      {
        *errorMessage = "World file contains no entities.";
      }
      return std::nullopt;
    }

    // First pass: create all entities and build old->new ID map.
    std::unordered_map<Entity::EntityId, Entity::EntityId> idMap;
    std::vector<std::pair<Entity::EntityId, json>> entitiesToLoad;

    for (const auto &entityJson : entitiesJson)
    {
      Entity::EntityId oldId = entityJson["id"].get<Entity::EntityId>();
      Entity::EntityId newId = entityManager.createEntity();
      idMap[oldId] = newId;
      entitiesToLoad.emplace_back(newId, entityJson);
    }

    // Second pass: deserialize components with remapped IDs.
    for (const auto &[newId, entityJson] : entitiesToLoad)
    {
      deserialize_entity(entityJson, newId, componentManager, idMap);
    }

    // The first entity in the file is the world root.
    return entitiesToLoad.front().first;
  }

  std::vector<Entity::EntityId> load_all_worlds(
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage)
  {
    std::vector<Entity::EntityId> loadedWorlds;
    std::string combinedErrors;

    for (const auto &worldName : list_saved_worlds(workspacePath))
    {
      std::string worldError;
      const auto filePath = worlds_directory(workspacePath) / (worldName + ".json");
      auto worldEntity = load_world_from_file(filePath, entityManager, componentManager, &worldError);
      if (worldEntity.has_value())
      {
        loadedWorlds.push_back(*worldEntity);
        continue;
      }

      if (!combinedErrors.empty())
      {
        combinedErrors += '\n';
      }
      combinedErrors += "Failed to load world '" + worldName + "': " + worldError;
    }

    if (errorMessage != nullptr)
    {
      *errorMessage = std::move(combinedErrors);
    }

    return loadedWorlds;
  }

  bool save_all_worlds(
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage)
  {
    const auto dir = worlds_directory(workspacePath);
    std::error_code errorCode;
    std::filesystem::create_directories(dir, errorCode);
    if (errorCode)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to create world save directory '" + dir.string() + "': " + errorCode.message();
      }
      return false;
    }

    std::unordered_set<std::string> savedWorldFiles;

    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      std::string worldName = "Untitled";
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        worldName = componentManager.getComponent<NameComponent>(entity).value;
      }

      const std::string fileName = sanitize_filename(worldName) + ".json";
      savedWorldFiles.insert(fileName);
      const auto filePath = dir / fileName;
      if (!save_world(filePath, entity, entityManager, componentManager, errorMessage))
      {
        return false;
      }
    }

    errorCode.clear();
    for (std::filesystem::directory_iterator iterator(dir, errorCode);
         !errorCode && iterator != std::filesystem::directory_iterator();
         iterator.increment(errorCode))
    {
      if (errorCode)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to inspect saved worlds in '" + dir.string() + "': " + errorCode.message();
        }
        return false;
      }

      std::error_code entryError;
      const bool isRegularFile = iterator->is_regular_file(entryError);
      if (entryError)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to inspect saved world entry in '" + dir.string() + "': " + entryError.message();
        }
        return false;
      }

      if (!isRegularFile || iterator->path().extension() != ".json")
      {
        continue;
      }

      if (savedWorldFiles.find(iterator->path().filename().string()) != savedWorldFiles.end())
      {
        continue;
      }

      std::filesystem::remove(iterator->path(), errorCode);
      if (errorCode)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Failed to remove stale world file '" + iterator->path().string() + "': " + errorCode.message();
        }
        return false;
      }
    }

    return true;
  }

  std::vector<std::string> list_saved_worlds(const std::filesystem::path &workspacePath)
  {
    std::vector<std::string> names;
    const auto dir = worlds_directory(workspacePath);

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
      return names;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    {
      if (entry.is_regular_file() && entry.path().extension() == ".json")
      {
        names.push_back(entry.path().stem().string());
      }
    }

    std::sort(names.begin(), names.end());
    return names;
  }

  void destroy_world_tree(
      Entity::EntityId worldEntity,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    // Collect all descendants first (avoids mutating while iterating).
    std::vector<Entity::EntityId> toDestroy;
    collect_entities_recursive(worldEntity, componentManager, toDestroy);

    // Destroy in reverse order (children before parents).
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it)
    {
      entityManager.destroyEntity(*it);
    }
  }
}
