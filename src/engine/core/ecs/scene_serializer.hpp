#ifndef HADES_ENGINE_CORE_ECS_SCENE_SERIALIZER_HPP
#define HADES_ENGINE_CORE_ECS_SCENE_SERIALIZER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "entity.hpp"

namespace hades
{
  class EntityManager;
  class ComponentManager;

  bool save_world(
      const std::filesystem::path &filePath,
      Entity::EntityId worldEntity,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage = nullptr);

  std::optional<Entity::EntityId> load_world_from_file(
      const std::filesystem::path &filePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage = nullptr);

  std::vector<Entity::EntityId> load_all_worlds(
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage = nullptr);

  bool save_all_worlds(
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::string *errorMessage = nullptr);

  std::vector<std::string> list_saved_worlds(
      const std::filesystem::path &workspacePath);

  void destroy_world_tree(
      Entity::EntityId worldEntity,
      EntityManager &entityManager,
      ComponentManager &componentManager);

  nlohmann::json snapshot_all_worlds(
      EntityManager &entityManager,
      ComponentManager &componentManager);

  void restore_all_worlds_from_snapshot(
      const nlohmann::json &snapshot,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::unordered_map<Entity::EntityId, Entity::EntityId> *outIdMap = nullptr);
}

#endif
