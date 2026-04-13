#ifndef HADES_RUNTIME_GAME_RUNTIME_HPP
#define HADES_RUNTIME_GAME_RUNTIME_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/core/ecs/system_manager.hpp"
#include "engine/core/events/event_bus.hpp"
#ifndef HADES_PLATFORM_WEB
#include "engine/runtime/script_runtime.hpp"
#endif
#ifdef HADES_ENABLE_API
#include "engine/api/hades_api.hpp"
#endif

struct SDL_Window;

namespace hades
{
  class AudioEngine;
  class AudioSystem;
  class PhysicsSystem;
  class PhysicsWorld;
  class Renderer;
  class RenderSystem;

  class GameRuntime
  {
  public:
    GameRuntime();
    ~GameRuntime();

    bool init(const std::filesystem::path &projectPath, bool headless = false, bool apiMode = false, int apiPort = 7777);
    int run();
    void render_frame();
    void tick_frame(float deltaTime);
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
    std::shared_ptr<RenderSystem> renderSystem_;
    bool headless_ = false;
    bool apiMode_ = false;
    bool initialized_ = false;
    bool running_ = false;
    std::optional<Entity::EntityId> activeWorld_;
#ifdef HADES_ENABLE_API
    std::unique_ptr<HadesAPI> api_;
    int apiPort_ = 7777;
    nlohmann::json initialWorldSnapshot_;

    void run_api_loop();
    std::string collect_entity_state_json();
    void update_api_state();
    void reset_game();
#endif

    std::string project_name() const;
#ifndef HADES_PLATFORM_WEB
    void handle_pending_world_load();
#endif
  };
}

#endif
