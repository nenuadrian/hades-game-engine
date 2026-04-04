#ifndef HADES_RUNTIME_GAME_RUNTIME_HPP
#define HADES_RUNTIME_GAME_RUNTIME_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/core/ecs/system_manager.hpp"
#include "engine/core/events/event_bus.hpp"
#ifndef HADES_PLATFORM_WEB
#include "engine/runtime/script_runtime.hpp"
#endif

struct SDL_Window;

namespace hades
{
  class AudioEngine;
  class AudioSystem;
  class PhysicsSystem;
  class PhysicsWorld;
  class Renderer;

  class GameRuntime
  {
  public:
    GameRuntime();
    ~GameRuntime();

    bool init(const std::filesystem::path &projectPath, bool headless = false);
    int run();
    void render_frame();
    bool is_running() const { return running_; }

  private:
    struct SDLWindowDeleter
    {
      void operator()(SDL_Window *window) const;
    };

    class SdlSession
    {
    public:
      bool init(std::uint32_t flags);
      ~SdlSession();

    private:
      bool initialized_ = false;
    };

    using WindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

    std::filesystem::path projectPath_;
    EntityManager entityManager_;
    ComponentManager componentManager_;
    SystemManager systemManager_;
    EventBus eventBus_;
#ifndef HADES_PLATFORM_WEB
    ScriptRuntime scriptRuntime_;
#endif
    SdlSession sdlSession_;
    WindowPtr window_{nullptr};
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<AudioEngine> audioEngine_;
    std::shared_ptr<AudioSystem> audioSystem_;
    std::unique_ptr<PhysicsWorld> physicsWorld_;
    std::shared_ptr<PhysicsSystem> physicsSystem_;
    bool headless_ = false;
    bool initialized_ = false;
    bool running_ = false;
    std::optional<Entity::EntityId> activeWorld_;

    std::string project_name() const;
  };
}

#endif
