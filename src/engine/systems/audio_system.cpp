#include "audio_system.hpp"

#include <optional>
#include <unordered_set>

#include "../audio/audio_engine.hpp"
#include "../components/audio_listener_component.hpp"
#include "../components/audio_source_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/world_utils.hpp"
#include "../runtime/main_camera_selection.hpp"

namespace hades
{
  namespace
  {
    std::optional<Entity::EntityId> find_fallback_listener(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> activeWorld)
    {
      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (activeWorld.has_value() && !entity_belongs_to_world(entity, *activeWorld, componentManager))
        {
          continue;
        }

        if (!componentManager.hasComponent<AudioListenerComponent>(entity) ||
            !componentManager.hasComponent<PositionComponent3D>(entity))
        {
          continue;
        }

        const auto &listener = componentManager.getComponent<AudioListenerComponent>(entity);
        if (listener.enabled)
        {
          return entity;
        }
      }

      return std::nullopt;
    }
  }

  void AudioSystem::setAudioEngine(AudioEngine *audioEngine)
  {
    audioEngine_ = audioEngine;
  }

  void AudioSystem::set_active_world(std::optional<Entity::EntityId> activeWorld)
  {
    activeWorld_ = activeWorld;
  }

  void AudioSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)deltaTime;

    if (audioEngine_ == nullptr || !audioEngine_->is_initialized())
    {
      return;
    }

    std::optional<Entity::EntityId> listenerEntity;
    const auto mainCamera = select_main_camera(entityManager, componentManager, activeWorld_);
    if (mainCamera.status == MainCameraSelectionStatus::Ready && mainCamera.entity.has_value())
    {
      const Entity::EntityId entity = *mainCamera.entity;
      if (componentManager.hasComponent<AudioListenerComponent>(entity) &&
          componentManager.hasComponent<PositionComponent3D>(entity) &&
          componentManager.getComponent<AudioListenerComponent>(entity).enabled)
      {
        listenerEntity = entity;
      }
    }

    if (!listenerEntity.has_value())
    {
      listenerEntity = find_fallback_listener(entityManager, componentManager, activeWorld_);
    }

    if (listenerEntity.has_value())
    {
      const auto &listener = componentManager.getComponent<AudioListenerComponent>(*listenerEntity);
      const auto &position = componentManager.getComponent<PositionComponent3D>(*listenerEntity);
      audioEngine_->set_listener(listener, position);
    }

    std::unordered_set<Entity::EntityId> activeSources;
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (activeWorld_.has_value() && !entity_belongs_to_world(entity, *activeWorld_, componentManager))
      {
        continue;
      }

      if (!componentManager.hasComponent<AudioSourceComponent>(entity))
      {
        continue;
      }

      activeSources.insert(entity);
      const auto &source = componentManager.getComponent<AudioSourceComponent>(entity);
      const PositionComponent3D *position = nullptr;
      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        position = &componentManager.getComponent<PositionComponent3D>(entity);
      }

      audioEngine_->sync_source(entity, source, position);
    }

    audioEngine_->prune_sources(activeSources);
  }
}
