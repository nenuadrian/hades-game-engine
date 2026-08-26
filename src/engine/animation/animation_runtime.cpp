#include "animation_runtime.hpp"

#include <utility>

namespace hades
{
  AnimationRuntime &AnimationRuntime::instance()
  {
    static AnimationRuntime runtime;
    return runtime;
  }

  // Locking discipline for the whole file:
  //
  // `mutex_` is only ever held across plain container operations. It is never
  // held while an AnimatorInstance method runs, because those reach back into
  // the runtime (a script called from an animation event can call
  // hades::Animation::play, which lands in instanceFor) and would deadlock on
  // this non-recursive mutex. The accessors therefore hand out a pointer under
  // the lock and call through it after releasing.
  //
  // That is safe because `instances_` is an unordered_map holding
  // AnimatorInstance *by value*: every mapped value lives in its own node, so
  // rehashing on insert moves buckets, never elements. References and pointers
  // to a mapped value stay valid until that key is erased. This is load-bearing
  // — instanceFor hands out a reference the caller keeps for the rest of the
  // frame, and the animator system inserts other entities in the same loop.

  AnimatorInstance &AnimationRuntime::instanceFor(Entity::EntityId entity)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Valid until this entity is removed (see the note above); a later insert
    // of another entity cannot invalidate it.
    return instances_[entity];
  }

  AnimatorInstance *AnimationRuntime::find(Entity::EntityId entity)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instances_.find(entity);
    return it == instances_.end() ? nullptr : &it->second;
  }

  const AnimatorInstance *AnimationRuntime::find(Entity::EntityId entity) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instances_.find(entity);
    return it == instances_.end() ? nullptr : &it->second;
  }

  bool AnimationRuntime::has(Entity::EntityId entity) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return instances_.find(entity) != instances_.end();
  }

  void AnimationRuntime::remove(Entity::EntityId entity)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    instances_.erase(entity);
    // The preview goes with it: entity ids are recycled, so a preview left
    // behind by a destroyed entity would later be painted onto its successor.
    previews_.erase(entity);
  }

  void AnimationRuntime::clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    instances_.clear();
    previews_.clear();
  }

  void AnimationRuntime::reset_all()
  {
    // Collect under the lock, reset outside it. Pointers into the map survive
    // the unlock (node-based storage, nothing is erased here).
    std::vector<AnimatorInstance *> live;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live.reserve(instances_.size());
      for (auto &entry : instances_)
      {
        live.push_back(&entry.second);
      }
    }

    for (AnimatorInstance *instance : live)
    {
      instance->reset();
    }
  }

  void AnimationRuntime::set_preview_palette(Entity::EntityId entity, std::vector<math::Mat4> palette)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (palette.empty())
    {
      // An empty palette is "nothing to preview", not "draw no bones": keeping
      // it would make has_preview true while palette_for had nothing to give.
      previews_.erase(entity);
      return;
    }
    previews_[entity] = std::move(palette);
  }

  void AnimationRuntime::clear_preview(Entity::EntityId entity)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    previews_.erase(entity);
  }

  void AnimationRuntime::clear_all_previews()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    previews_.clear();
  }

  bool AnimationRuntime::has_preview(Entity::EntityId entity) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return previews_.find(entity) != previews_.end();
  }

  bool AnimationRuntime::palette_for(Entity::EntityId entity, std::vector<math::Mat4> &out) const
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto preview = previews_.find(entity);
    if (preview != previews_.end())
    {
      out = preview->second;
      return true;
    }

    const auto it = instances_.find(entity);
    if (it == instances_.end())
    {
      return false;
    }

    // palette() is an inline accessor on the instance, so reading it under the
    // lock cannot re-enter the runtime. The copy is what the caller owns.
    const std::vector<math::Mat4> &palette = it->second.palette();
    if (palette.empty())
    {
      // Never evaluated yet: let the caller fall back to the bind pose rather
      // than upload an empty palette.
      return false;
    }

    out = palette;
    return true;
  }

  std::vector<AnimationEventFired> AnimationRuntime::drain_events(Entity::EntityId entity)
  {
    AnimatorInstance *instance = find(entity);
    if (instance == nullptr)
    {
      return {};
    }
    return instance->drain_events();
  }

  bool AnimationRuntime::event_fired(Entity::EntityId entity, const std::string &name) const
  {
    const AnimatorInstance *instance = find(entity);
    if (instance == nullptr)
    {
      return false;
    }
    return instance->event_fired(name);
  }
}
