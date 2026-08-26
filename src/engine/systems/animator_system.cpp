#include "animator_system.hpp"

#include "../animation/animation_clip_cache.hpp"
#include "../animation/animation_runtime.hpp"
#include "../animation/animator_instance.hpp"
#include "../assets/model_asset.hpp"
#include "../assets/model_asset_cache.hpp"
#include "../components/animator_component.hpp"
#include "../components/model_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../core/events/event_bus.hpp"
#include "../core/events/events.hpp"

namespace hades
{
  namespace
  {
    void apply_parameter_override(AnimatorInstance &instance, const AnimatorParamOverride &authored)
    {
      if (authored.name.empty())
      {
        return;
      }

      // Tolerant on the type string: it is authored text, and an unknown value
      // should land somewhere sane rather than drop the override.
      if (authored.type == "int")
      {
        instance.set_int(authored.name, authored.intValue);
      }
      else if (authored.type == "bool")
      {
        instance.set_bool(authored.name, authored.boolValue);
      }
      else if (authored.type == "trigger")
      {
        // Triggers are not meant to be authored (see AnimatorParamOverride),
        // but a scene that carries one should still start latched.
        if (authored.boolValue)
        {
          instance.set_trigger(authored.name);
        }
      }
      else
      {
        instance.set_float(authored.name, authored.floatValue);
      }
    }
  }

  void AnimatorSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    // Legacy 3-argument path: no event bus, so animation events are dropped
    // after they have had their frame in the polling APIs.
    run(deltaTime, componentManager, entityManager, nullptr);
  }

  void AnimatorSystem::update(float deltaTime, SystemContext &context)
  {
    run(deltaTime, context.componentManager, context.entityManager, &context.eventBus);
  }

  void AnimatorSystem::reset()
  {
    started_.clear();
    authored_.clear();
  }

  void AnimatorSystem::run(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager,
                           EventBus *eventBus)
  {
    auto &models = ModelAssetCache::instance();
    auto &clips = AnimationClipCache::instance();
    auto &runtime = AnimationRuntime::instance();

    for (Entity::EntityId entity : query<ModelComponent, AnimatorComponent>(entityManager))
    {
      const auto &model = componentManager.getComponent<ModelComponent>(entity);
      const ModelAsset *asset = models.get(model.assetPath);
      if (asset == nullptr)
      {
        // Missing or broken model (already logged by the cache): there is no
        // skeleton to pose, and the animator has nothing to evaluate against.
        continue;
      }

      const auto &component = componentManager.getComponent<AnimatorComponent>(entity);

      // "First sight" is not only the first frame this entity is seen: it is
      // "this entity has no configured animator yet". AnimationRuntime::clear()
      // (play mode stopping, the workspace changing) drops every instance while
      // this system object keeps living, and an entity whose instance is gone
      // has to be configured from scratch or it starts the next play session
      // with a default animator that nothing ever plays. The has() test has to
      // happen before instanceFor, which would create the instance we are
      // asking about; it is only reached for entities already in started_,
      // because insert() short-circuits the rest on the genuine first frame.
      const bool firstSight = started_.insert(entity).second || !runtime.has(entity);
      AnimatorInstance &instance = runtime.instanceFor(entity);

      // Pushed unconditionally, and deliberately not through the
      // AuthoredPlayback change-tracking below: that exists because the
      // scripting API co-owns speed and playing, and nothing in it writes the
      // default blend, so there is no script decision to stamp over. Pushing
      // every frame is also what makes an inspector edit take effect while
      // play mode runs. It has to land before the firstSight play_clip.
      instance.set_default_blend(component.defaultBlendSeconds);

      if (firstSight)
      {
        instance.set_graph_reference(component.graphPath);
        instance.set_speed(component.speed);
        instance.set_playing(component.playOnStart);
        for (const auto &parameter : component.parameters)
        {
          apply_parameter_override(instance, parameter);
        }

        if (component.graphPath.empty() && !component.defaultClip.empty() &&
            instance.layer_count() == 0)
        {
          // Clip mode, and nothing has played on this instance yet. Snap
          // rather than blend: there is no previous pose to blend out of on
          // the first frame.
          //
          // The layer check is what keeps a script from being clobbered on the
          // frame it starts: script and Blueprint updates run before the system
          // phase, so the documented `Animation::play(ctx.entityId, "attack")`
          // in onStart has already pushed a source onto this instance by the
          // time we get here, and starting defaultClip over it would silently
          // discard the clip the script just asked for. A layer count of zero
          // means nobody has played anything (a fresh instance, or one that
          // AnimationRuntime::reset_all dropped playback state on), so the
          // authored clip is what should start.
          instance.play_clip(component.defaultClip, 0.0f, component.looping, 0);
        }

        authored_[entity] = AuthoredPlayback{component.speed, component.playOnStart};
      }
      else
      {
        // Re-binding the same reference is a documented no-op, so this is the
        // cheapest way to pick up a graph swapped in the inspector.
        instance.set_graph_reference(component.graphPath);

        // Speed and playing are shared with the scripting API, so only an
        // actual edit of the authored value is pushed down.
        AuthoredPlayback &previous = authored_[entity];
        if (previous.speed != component.speed)
        {
          previous.speed = component.speed;
          instance.set_speed(component.speed);
        }
        if (previous.playing != component.playOnStart)
        {
          previous.playing = component.playOnStart;
          instance.set_playing(component.playOnStart);
        }
      }

      // Events from the previous update were published then; drop them now,
      // not after this update, so that hades::Animation::eventFired and the
      // pure Blueprint "Animation Event Fired" node — both of which run in the
      // script phase, ahead of the system phase — get one frame to observe
      // them. Clearing here is what stops events accumulating, with or
      // without an event bus.
      instance.clear_events();
      instance.update(deltaTime, *asset, clips);

      if (eventBus != nullptr)
      {
        for (const auto &fired : instance.pending_events())
        {
          AnimationEvent event;
          event.entity = entity;
          event.name = fired.name;
          event.stringValue = fired.stringValue;
          event.floatValue = fired.floatValue;
          event.clip = fired.clip;
          event.time = fired.time;
          eventBus->publish(event);
        }
      }
    }

    // Forget entities whose AnimatorComponent is gone (removed, or the entity
    // destroyed) so that re-adding it applies the authored overrides again.
    // Losing only the ModelComponent does not count: the animator is still
    // configured, it just has nothing to pose this frame.
    for (auto it = started_.begin(); it != started_.end();)
    {
      if (componentManager.hasComponent<AnimatorComponent>(*it))
      {
        ++it;
        continue;
      }
      // The animator instance goes with it. Nothing else sweeps
      // AnimationRuntime, and entity ids are recycled: a leftover instance is
      // still found by palette_for for whatever entity inherits the id next, so
      // the scene renderer would paint a dead entity's frozen last pose onto
      // its successor (its own guard only catches a different joint count).
      // remove() drops the editor preview for the id as well.
      runtime.remove(*it);
      authored_.erase(*it);
      it = started_.erase(it);
    }
  }
}
