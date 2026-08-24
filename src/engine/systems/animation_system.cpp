#include "animation_system.hpp"

#include <algorithm>
#include <cmath>

#include "../assets/model_asset.hpp"
#include "../assets/model_asset_cache.hpp"
#include "../components/animation_component.hpp"
#include "../components/model_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"

namespace hades
{
  void AnimationSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    auto &cache = ModelAssetCache::instance();

    for (Entity::EntityId entity : query<ModelComponent, AnimationComponent>(entityManager))
    {
      auto &anim = componentManager.getComponent<AnimationComponent>(entity);
      if (!anim.playing)
      {
        continue;
      }

      const auto &model = componentManager.getComponent<ModelComponent>(entity);
      const ModelAsset *asset = cache.get(model.assetPath);
      if (asset == nullptr || asset->clips.empty())
      {
        continue;
      }

      anim.clipIndex = std::clamp(anim.clipIndex, 0, static_cast<int>(asset->clips.size()) - 1);
      const float duration = asset->clips[anim.clipIndex].duration;
      if (duration <= 0.0f)
      {
        anim.time = 0.0f;
        continue;
      }

      anim.time += deltaTime * anim.speed;

      if (anim.looping)
      {
        anim.time = std::fmod(anim.time, duration);
        if (anim.time < 0.0f)
        {
          anim.time += duration;
        }
      }
      else if (anim.time >= duration)
      {
        anim.time = duration;
        anim.playing = false;
      }
      else if (anim.time <= 0.0f && anim.speed < 0.0f)
      {
        anim.time = 0.0f;
        anim.playing = false;
      }
    }
  }
}
