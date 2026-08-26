#ifndef HADES_ENGINE_RUNTIME_HADES_SCRIPT_HPP
#define HADES_ENGINE_RUNTIME_HADES_SCRIPT_HPP

#include <cmath>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>

#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../rendering/math3d.hpp"
#include "hades_value.hpp"

namespace hades
{
  class AudioEngine;

  struct ScriptContext
  {
    Entity::EntityId entityId;
    ComponentManager &componentManager;
    EntityManager &entityManager;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
  };

  class HadesScript
  {
  public:
    virtual ~HadesScript() = default;
    virtual void onStart(ScriptContext &ctx) {}
    virtual void onUpdate(ScriptContext &ctx, float deltaTime) {}
    virtual void onKeyDown(ScriptContext &ctx, int keyCode) {}
    virtual void onKeyUp(ScriptContext &ctx, int keyCode) {}
    virtual void onMouseDown(ScriptContext &ctx, int button, float screenX, float screenY) {}
    virtual void onMouseUp(ScriptContext &ctx, int button, float screenX, float screenY) {}
    virtual void onMouseMove(ScriptContext &ctx, float screenX, float screenY) {}

    /// Called by the Blueprint nodes `Send Script Message` and
    /// `Call Script Function`, which is how a graph reaches into C++.
    ///
    /// `name` is whatever the node's Name pin carries, `value` its Value pin.
    /// The returned value is what `Call Script Function` reads back on its
    /// Result pin; `Send Script Message` ignores it. Returning the default
    /// (empty) `ScriptValue` means "not handled", and when an entity carries
    /// several scripts the first non-empty answer wins.
    ///
    /// Dispatch is synchronous: Blueprints update after scripts, so a message
    /// sent from a graph lands inside the same frame. Sending an event back
    /// with `hades::Blueprints::sendEvent` from here is safe — that path is
    /// queued and will not re-enter the graph that is currently running.
    virtual ScriptValue onMessage(ScriptContext &ctx, const std::string &name, const ScriptValue &value)
    {
      (void)ctx;
      (void)name;
      (void)value;
      return ScriptValue();
    }
  };

  class HadesAPI
  {
  public:
    using ObsValue = std::variant<int, float, double, bool, std::string>;

    static void observe(const std::string &key, int value)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_[key] = value;
    }

    static void observe(const std::string &key, float value)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_[key] = value;
    }

    static void observe(const std::string &key, double value)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_[key] = value;
    }

    static void observe(const std::string &key, bool value)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_[key] = value;
    }

    static void observe(const std::string &key, const std::string &value)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_[key] = value;
    }

    static void clear()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observations_.clear();
      pendingWorldLoad_.reset();
    }

    // --- Audio engine access ---
    // The running game registers its AudioEngine before scripts execute, so
    // scripts (or the hades::Audio facade) can reach the live SoLoud instance
    // for procedural sound generation. Returns nullptr if audio is unavailable.
    static void setAudioEngine(AudioEngine *engine)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      audioEngine_ = engine;
    }

    static AudioEngine *audioEngine()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      return audioEngine_;
    }

    // --- World loading ---

    static void loadWorld(const std::string &worldName)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pendingWorldLoad_ = worldName;
    }

    static std::optional<std::string> consumePendingWorldLoad()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto result = pendingWorldLoad_;
      pendingWorldLoad_.reset();
      return result;
    }

    // --- Raycasting utilities ---

    struct Ray
    {
      math::Vec3 origin;
      math::Vec3 direction;
    };

    static Ray screenToWorldRay(
        float screenX, float screenY,
        float viewportWidth, float viewportHeight,
        const math::Vec3 &cameraPos,
        const math::Mat4 &viewMatrix,
        const math::Mat4 &projMatrix)
    {
      float ndcX = (2.0f * screenX / viewportWidth) - 1.0f;
      float ndcY = 1.0f - (2.0f * screenY / viewportHeight);

      math::Mat4 invVP = (projMatrix * viewMatrix).inverse();
      math::Vec4 worldPoint = invVP * math::Vec4(ndcX, ndcY, 1.0f, 1.0f);

      math::Vec3 target;
      if (std::abs(worldPoint.w) > 1e-6f)
      {
        float invW = 1.0f / worldPoint.w;
        target = math::Vec3(worldPoint.x * invW, worldPoint.y * invW, worldPoint.z * invW);
      }
      else
      {
        target = worldPoint.xyz();
      }

      math::Vec3 direction = (target - cameraPos).normalized();
      return {cameraPos, direction};
    }

    static float rayDistanceToPoint(const Ray &ray, const math::Vec3 &point)
    {
      math::Vec3 toPoint = point - ray.origin;
      float t = toPoint.dot(ray.direction);
      math::Vec3 closest = ray.origin + ray.direction * (t > 0.0f ? t : 0.0f);
      return (point - closest).length();
    }

    static std::string serializeJson()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (observations_.empty())
      {
        return "{}";
      }

      std::ostringstream oss;
      oss << '{';
      bool first = true;
      for (const auto &[key, val] : observations_)
      {
        if (!first)
        {
          oss << ',';
        }
        first = false;

        oss << '"' << key << "\":";
        std::visit([&oss](const auto &v)
                   {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>)
          {
            oss << '"' << v << '"';
          }
          else if constexpr (std::is_same_v<T, bool>)
          {
            oss << (v ? "true" : "false");
          }
          else
          {
            oss << v;
          } }, val);
      }
      oss << '}';
      return oss.str();
    }

  private:
    static inline std::mutex mutex_;
    static inline std::unordered_map<std::string, ObsValue> observations_;
    static inline std::optional<std::string> pendingWorldLoad_;
    static inline AudioEngine *audioEngine_ = nullptr;
  };
}

#endif
