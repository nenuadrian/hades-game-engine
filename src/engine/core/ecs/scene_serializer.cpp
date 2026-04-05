#include "scene_serializer.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "component_manager.hpp"
#include "component_registry.hpp"
#include "entity_manager.hpp"
#include "../../components/name_component.hpp"
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

      for (const auto &reg : ComponentRegistry::instance().all())
      {
        json compJson;
        if (reg.serialize(entity, componentManager, compJson))
        {
          components[reg.jsonKey] = compJson;
        }
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

      for (const auto &reg : ComponentRegistry::instance().all())
      {
        if (components.contains(reg.jsonKey))
        {
          reg.deserialize(newEntity, componentManager, components[reg.jsonKey], idMap);
        }
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
      componentManager.removeAllComponents(*it);
      entityManager.destroyEntity(*it);
    }
  }

  json snapshot_all_worlds(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    json worlds = json::array();

    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      std::vector<Entity::EntityId> worldEntities;
      collect_entities_recursive(entity, componentManager, worldEntities);

      json worldJson;
      worldJson["version"] = WORLD_FORMAT_VERSION;
      json entities = json::array();
      for (Entity::EntityId e : worldEntities)
      {
        entities.push_back(serialize_entity(e, componentManager));
      }
      worldJson["entities"] = entities;
      worlds.push_back(worldJson);
    }

    return worlds;
  }

  void restore_all_worlds_from_snapshot(
      const json &snapshot,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::unordered_map<Entity::EntityId, Entity::EntityId> *outIdMap)
  {
    // Destroy all current world trees.
    std::vector<Entity::EntityId> currentWorlds;
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (componentManager.hasComponent<WorldComponent>(entity))
      {
        currentWorlds.push_back(entity);
      }
    }
    for (Entity::EntityId world : currentWorlds)
    {
      destroy_world_tree(world, entityManager, componentManager);
    }

    std::unordered_map<Entity::EntityId, Entity::EntityId> combinedIdMap;

    for (const auto &worldJson : snapshot)
    {
      if (!worldJson.contains("entities"))
      {
        continue;
      }

      const auto &entitiesJson = worldJson["entities"];

      // First pass: create entities and build ID map.
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

      combinedIdMap.insert(idMap.begin(), idMap.end());
    }

    if (outIdMap != nullptr)
    {
      *outIdMap = std::move(combinedIdMap);
    }
  }
}
