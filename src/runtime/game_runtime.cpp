#include "game_runtime.hpp"

#include <cstdlib>
#include <unordered_set>

#include <SDL.h>


#include "engine/core/log.hpp"
#include "engine/audio/audio_engine.hpp"
#include "engine/audio/script_audio.hpp"
#include "engine/core/ecs/scene_serializer.hpp"
#include "engine/core/ecs/world_utils.hpp"
#include "engine/physics/physics_world.hpp"
#include "engine/rendering/vulkan.hpp"
#include "engine/assets/model_asset_cache.hpp"
#include "engine/animation/animation_clip_cache.hpp"
#include "engine/animation/animation_runtime.hpp"
#include "engine/runtime/main_camera_selection.hpp"
#include "engine/components/name_component.hpp"
#include "engine/components/position_component_3d.hpp"
#include "engine/core/ecs/query.hpp"
#include "engine/systems/animation_system.hpp"
#include "engine/systems/animator_system.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/movement_system.hpp"
#include "engine/systems/physics_system.hpp"
#include "engine/systems/render_system.hpp"
#include "engine/ui/script_ui.hpp"
#include "engine/ui/ui_input.hpp"

namespace
{
  using SdlStringPtr = std::unique_ptr<char, decltype(&SDL_free)>;
  using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;

  std::string asset_path(const char *relative_path)
  {
    SdlStringPtr base_path(SDL_GetBasePath(), SDL_free);
    if (!base_path)
    {
      return relative_path;
    }

    return std::string(base_path.get()) + relative_path;
  }

  void set_window_icon(SDL_Window *window)
  {
    const std::string logo_path = asset_path("assets/logo.bmp");
    SurfacePtr surface(SDL_LoadBMP(logo_path.c_str()), SDL_FreeSurface);
    if (window != nullptr && surface != nullptr)
    {
      SDL_SetWindowIcon(window, surface.get());
    }
  }
}

namespace hades
{
  void GameRuntime::SDLWindowDeleter::operator()(SDL_Window *window) const
  {
    if (window != nullptr)
    {
      SDL_DestroyWindow(window);
    }
  }

  bool GameRuntime::SdlSession::init(std::uint32_t flags)
  {
    if (initialized_)
    {
      return true;
    }

    if (SDL_Init(flags) != 0)
    {
      Log::error_tagged("runtime", "SDL_Init(): %s", SDL_GetError());
      return false;
    }

    initialized_ = true;
    return true;
  }

  GameRuntime::SdlSession::~SdlSession()
  {
    if (initialized_)
    {
      SDL_Quit();
    }
  }

  GameRuntime::GameRuntime()
      :
        renderer_(std::make_unique<VulkanRenderer>()),
        audioEngine_(std::make_unique<AudioEngine>()),
        physicsWorld_(std::make_unique<PhysicsWorld>())
  {
  }

  GameRuntime::~GameRuntime()
  {
    scriptRuntime_.stop();
    blueprintRuntime_.stop();
  }

  std::string GameRuntime::project_name() const
  {
    return projectPath_.filename().string();
  }

  bool GameRuntime::init(const std::filesystem::path &projectPath, bool headless, bool apiMode, int apiPort)
  {
    if (initialized_)
    {
      return true;
    }

    projectPath_ = projectPath;
    headless_ = headless;
    apiMode_ = apiMode;
#ifdef HADES_ENABLE_API
    apiPort_ = apiPort;
#else
    (void)apiPort;
    if (apiMode)
    {
      Log::warn("HadesAPI was requested but the build does not include API support.");
      apiMode_ = false;
    }
#endif

    const std::uint32_t sdlFlags = headless_
                                       ? (SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER)
                                       : (SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);
    if (!sdlSession_.init(sdlFlags))
    {
      return false;
    }

    if (!headless_)
    {
      const std::string title = project_name();

      constexpr int WINDOW_WIDTH = 1280;
      constexpr int WINDOW_HEIGHT = 720;
      const SDL_WindowFlags window_flags =
          static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
      window_.reset(SDL_CreateWindow(
          title.c_str(),
          SDL_WINDOWPOS_CENTERED,
          SDL_WINDOWPOS_CENTERED,
          WINDOW_WIDTH,
          WINDOW_HEIGHT,
          window_flags));
      if (window_ == nullptr)
      {
        Log::error_tagged("runtime", "SDL_CreateWindow(): %s", SDL_GetError());
        return false;
      }

      set_window_icon(window_.get());

      if (!renderer_->init(window_.get()))
      {
        return false;
      }
    }
    else
    {
      renderer_.reset();
    }

    // Do NOT call init_imgui_backend -- the runtime has no editor UI.

    if (!audioEngine_->init())
    {
      Log::warn("audio engine is unavailable. Audio playback has been disabled.");
      audioEngine_.reset();
    }
    // Publish the live AudioEngine so scripts can reach SoLoud via
    // hades::Audio / hades::HadesAPI::audioEngine() for procedural audio.
    register_script_audio_engine(audioEngine_.get());

    if (!physicsWorld_->init())
    {
      Log::warn("physics engine is unavailable. Physics simulation has been disabled.");
      physicsWorld_.reset();
    }

    componentManager_ = ComponentManager(&entityManager_);
    // Publish the live ECS so hades::UI can reach UICanvasComponent data.
    register_script_ui_components(&componentManager_);

    physicsSystem_ = systemManager_.registerSystem<PhysicsSystem>(SystemPhase::Physics);
    physicsSystem_->setPhysicsWorld(physicsWorld_.get());
    systemManager_.registerSystem<MovementSystem>(SystemPhase::Logic);
    systemManager_.registerSystem<AnimationSystem>(SystemPhase::Logic);
    systemManager_.registerSystem<AnimatorSystem>(SystemPhase::Logic);
    renderSystem_ = systemManager_.registerSystem<RenderSystem>(SystemPhase::Render);
    audioSystem_ = systemManager_.registerSystem<AudioSystem>(SystemPhase::Audio);
    audioSystem_->setAudioEngine(audioEngine_.get());

    // Model and animation asset paths resolve relative to the project directory.
    ModelAssetCache::instance().setAssetRoot(projectPath_);
    AnimationClipCache::instance().setAssetRoot(projectPath_);

    // Load all worlds from the project.
    std::string loadError;
    const auto worlds = load_all_worlds(projectPath_, entityManager_, componentManager_, &loadError);
    if (worlds.empty())
    {
      Log::error_tagged("runtime", "failed to load worlds: %s", loadError.c_str());
      return false;
    }

    // Find the default world.
    activeWorld_ = normalize_default_world(entityManager_, componentManager_);
    if (!activeWorld_.has_value())
    {
      // Fall back to the first loaded world.
      activeWorld_ = worlds.front();
    }

    // Verify a main camera exists.
    const auto cameraSelection = select_main_camera(entityManager_, componentManager_, activeWorld_);
    if (cameraSelection.status != MainCameraSelectionStatus::Ready)
    {
      Log::warn_tagged("runtime", "%s", main_camera_selection_message(cameraSelection.status));
    }

    // Set up audio and physics for the active world.
    if (audioSystem_ != nullptr)
    {
      audioSystem_->set_active_world(activeWorld_);
    }
    if (physicsSystem_ != nullptr)
    {
      physicsSystem_->set_active_world(activeWorld_);
    }

    // Start the script runtime.
    std::string scriptError;
    if (!scriptRuntime_.start(componentManager_, entityManager_, projectPath_, activeWorld_, &scriptError))
    {
      if (!scriptError.empty())
      {
        Log::warn("script runtime failed to start: %s", scriptError.c_str());
      }
    }

    // Blueprints run beside the C++ scripts and share the same host services.
    blueprintHost_.bind(&componentManager_, physicsWorld_.get(), audioEngine_.get());
    blueprintHost_.set_script_runtime(&scriptRuntime_);
    blueprintRuntime_.set_host(&blueprintHost_);
    blueprintRuntime_.attach_event_bus(eventBus_);

    std::string blueprintError;
    if (!blueprintRuntime_.start(componentManager_, entityManager_, projectPath_, activeWorld_, &blueprintError))
    {
      if (!blueprintError.empty())
      {
        Log::error_tagged("runtime", "blueprint runtime failed to start: %s", blueprintError.c_str());
      }
    }

    initialized_ = true;
    return true;
  }

  void GameRuntime::render_frame()
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
      {
        running_ = false;
      }
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE)
      {
        running_ = false;
      }

      if (event.type == SDL_KEYDOWN)
      {
        scriptRuntime_.on_key_down(static_cast<int>(event.key.keysym.sym));
        blueprintRuntime_.on_key_down(static_cast<int>(event.key.keysym.sym));
        if (scriptRuntime_.faulted())
        {
          Log::error("script: %s", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
      else if (event.type == SDL_KEYUP)
      {
        scriptRuntime_.on_key_up(static_cast<int>(event.key.keysym.sym));
        blueprintRuntime_.on_key_up(static_cast<int>(event.key.keysym.sym));
        if (scriptRuntime_.faulted())
        {
          Log::error("script: %s", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
      else if (event.type == SDL_MOUSEBUTTONDOWN)
      {
        // Screen-space UI hears the click first (button events + script
        // "ui.clicked" messages); the raw event still reaches gameplay.
        if (event.button.button == SDL_BUTTON_LEFT && window_ != nullptr)
        {
          int windowW = 0, windowH = 0;
          SDL_GetWindowSize(window_.get(), &windowW, &windowH);
          ui::dispatch_ui_click(
              componentManager_, entityManager_, activeWorld_,
              static_cast<float>(event.button.x), static_cast<float>(event.button.y),
              static_cast<float>(windowW), static_cast<float>(windowH),
              &blueprintRuntime_, &scriptRuntime_);
        }
        scriptRuntime_.on_mouse_down(
            static_cast<int>(event.button.button),
            static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
        blueprintRuntime_.on_mouse_down(
            static_cast<int>(event.button.button),
            static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
        if (scriptRuntime_.faulted())
        {
          Log::error("script: %s", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
      else if (event.type == SDL_MOUSEBUTTONUP)
      {
        scriptRuntime_.on_mouse_up(
            static_cast<int>(event.button.button),
            static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
        blueprintRuntime_.on_mouse_up(
            static_cast<int>(event.button.button),
            static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
        if (scriptRuntime_.faulted())
        {
          Log::error("script: %s", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
      else if (event.type == SDL_MOUSEMOTION)
      {
        ui::set_ui_pointer(
            static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y));
        scriptRuntime_.on_mouse_move(
            static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y));
        blueprintRuntime_.on_mouse_move(
            static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y));
        if (scriptRuntime_.faulted())
        {
          Log::error("script: %s", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
    }

    if (renderer_)
    {
      // Handle swap chain management (resize, etc.).
      renderer_->render_frame(window_.get());
    }

    // Compute delta time.
    static Uint64 lastTicks = SDL_GetPerformanceCounter();
    const Uint64 currentTicks = SDL_GetPerformanceCounter();
    const float deltaTime = static_cast<float>(
        static_cast<double>(currentTicks - lastTicks) /
        static_cast<double>(SDL_GetPerformanceFrequency()));
    lastTicks = currentTicks;

    // Update scripts.
    if (scriptRuntime_.is_running())
    {
      int windowW = 0, windowH = 0;
      SDL_GetWindowSize(window_.get(), &windowW, &windowH);
      scriptRuntime_.set_viewport_size(static_cast<float>(windowW), static_cast<float>(windowH));

      scriptRuntime_.update(deltaTime, componentManager_, entityManager_);
      if (scriptRuntime_.faulted())
      {
        Log::error("script: %s", scriptRuntime_.last_error().c_str());
        running_ = false;
        return;
      }

      handle_pending_world_load();
    }

    if (blueprintRuntime_.is_running())
    {
      blueprintRuntime_.update(deltaTime, componentManager_, entityManager_);
      if (blueprintRuntime_.faulted())
      {
        Log::error("blueprint: %s", blueprintRuntime_.last_error().c_str());
        running_ = false;
        return;
      }

      handle_pending_world_load();
    }

    // Dispatch pending events, then update all engine systems.
    eventBus_.dispatch();
    SystemContext context{componentManager_, entityManager_, eventBus_};
    systemManager_.updateSystems(deltaTime, context);

    if (renderer_)
    {
      const auto cameraSelection = select_main_camera(entityManager_, componentManager_, activeWorld_);
      if (cameraSelection.status == MainCameraSelectionStatus::Ready &&
          cameraSelection.entity.has_value())
      {
        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSize(window_.get(), &windowW, &windowH);
        const float aspect = (windowH > 0)
                                 ? (static_cast<float>(windowW) / static_cast<float>(windowH))
                                 : 1.0f;
        const RenderCamera camera = sceneRenderer_.buildCamera(*cameraSelection.entity, aspect, componentManager_);
        const RenderList list = sceneRenderer_.buildRenderList(
            camera, componentManager_, entityManager_, activeWorld_,
            static_cast<float>(windowW), static_cast<float>(windowH));
        renderer_->render_scene_to_main(list);
      }
      // Present the frame (clear + present, no ImGui).
      renderer_->present_frame();
    }
  }

  void GameRuntime::tick_frame(float deltaTime)
  {
    // Update scripts.
    if (scriptRuntime_.is_running())
    {
      scriptRuntime_.update(deltaTime, componentManager_, entityManager_);
      if (scriptRuntime_.faulted())
      {
        Log::error("script: %s", scriptRuntime_.last_error().c_str());
        running_ = false;
        return;
      }

      handle_pending_world_load();
    }

    if (blueprintRuntime_.is_running())
    {
      blueprintRuntime_.update(deltaTime, componentManager_, entityManager_);
      if (blueprintRuntime_.faulted())
      {
        Log::error("blueprint: %s", blueprintRuntime_.last_error().c_str());
        running_ = false;
        return;
      }

      handle_pending_world_load();
    }

    // Dispatch pending events, then update all engine systems.
    eventBus_.dispatch();
    SystemContext context{componentManager_, entityManager_, eventBus_};
    systemManager_.updateSystems(deltaTime, context);
  }

  int GameRuntime::run()
  {
    if (!initialized_)
    {
      return EXIT_FAILURE;
    }

    running_ = true;

#ifdef HADES_ENABLE_API
    if (apiMode_)
    {
      run_api_loop();
      scriptRuntime_.stop();
      blueprintRuntime_.stop();
      if (audioEngine_ != nullptr)
      {
        audioEngine_->stop_all();
      }
      register_script_audio_engine(nullptr);
      register_script_ui_components(nullptr);
      return EXIT_SUCCESS;
    }
#endif

    while (running_)
    {
      render_frame();
    }

    scriptRuntime_.stop();
    blueprintRuntime_.stop();
    if (audioEngine_ != nullptr)
    {
      audioEngine_->stop_all();
    }
    register_script_audio_engine(nullptr);
    register_script_ui_components(nullptr);

    return EXIT_SUCCESS;
  }

#ifdef HADES_ENABLE_API
  std::string GameRuntime::collect_entity_state_json()
  {
    auto entities = query<PositionComponent3D>(entityManager_, componentManager_, activeWorld_);

    nlohmann::json arr = nlohmann::json::array();
    for (auto entityId : entities)
    {
      nlohmann::json obj;
      obj["id"] = entityId;

      if (componentManager_.hasComponent<NameComponent>(entityId))
      {
        obj["name"] = componentManager_.getComponent<NameComponent>(entityId).value;
      }

      const auto &pos = componentManager_.getComponent<PositionComponent3D>(entityId);
      obj["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
      arr.push_back(std::move(obj));
    }

    return arr.dump();
  }

  void GameRuntime::update_api_state()
  {
    if (!api_)
    {
      return;
    }

    api_->set_observed_state(scriptRuntime_.collect_observations());
    api_->set_entity_state(collect_entity_state_json());
    api_->set_game_over(!running_);
  }

  void GameRuntime::reset_game()
  {
    scriptRuntime_.stop();
    blueprintRuntime_.stop();

    // Restore the initial world state from the snapshot taken at startup.
    std::unordered_map<Entity::EntityId, Entity::EntityId> idMap;
    restore_all_worlds_from_snapshot(initialWorldSnapshot_, entityManager_, componentManager_, &idMap);

    // The restore destroys and re-creates in one call, so the earliest point
    // the reset entities can be given clean animators is right after it. They
    // have none yet -- AnimatorSystem has not run -- so this only drops the
    // pre-reset state that the recycled ids would otherwise inherit.
    AnimationRuntime::instance().clear();

    // Re-resolve the active world after restore (IDs may have changed).
    if (activeWorld_.has_value())
    {
      auto it = idMap.find(*activeWorld_);
      if (it != idMap.end())
      {
        activeWorld_ = it->second;
      }
      else
      {
        activeWorld_ = normalize_default_world(entityManager_, componentManager_);
      }
    }

    // Re-initialize subsystems for the new world.
    if (audioSystem_ != nullptr)
    {
      audioSystem_->set_active_world(activeWorld_);
    }
    if (physicsSystem_ != nullptr)
    {
      physicsSystem_->set_active_world(activeWorld_);
    }

    std::string scriptError;
    if (!scriptRuntime_.start(componentManager_, entityManager_, projectPath_, activeWorld_, &scriptError))
    {
      if (!scriptError.empty())
      {
        Log::warn("script runtime failed to restart: %s", scriptError.c_str());
      }
    }

    std::string blueprintError;
    if (!blueprintRuntime_.start(componentManager_, entityManager_, projectPath_, activeWorld_, &blueprintError))
    {
      if (!blueprintError.empty())
      {
        Log::error_tagged("runtime", "blueprint runtime failed to restart: %s", blueprintError.c_str());
      }
    }

    running_ = true;
  }

  void GameRuntime::run_api_loop()
  {
    // Take a snapshot of the initial world state for reset support.
    initialWorldSnapshot_ = snapshot_all_worlds(entityManager_, componentManager_);

    api_ = std::make_unique<HadesAPI>();
    HadesAPI::Config config;
    config.port = apiPort_;

    if (!api_->start(config))
    {
      Log::error("HadesAPI failed to start on port %d", apiPort_);
      return;
    }

    // Publish initial state.
    update_api_state();

    // The API loop blocks waiting for commands from HTTP clients.
    while (running_ && api_->is_running())
    {
      if (!api_->wait_for_command())
      {
        break;
      }

      if (api_->has_pending_reset())
      {
        api_->consume_pending_reset();
        reset_game();
        update_api_state();
        api_->signal_reset_complete();
        continue;
      }

      if (api_->has_pending_step())
      {
        const int ticks = api_->consume_pending_step();
        auto inputs = api_->consume_pending_inputs();

        // Inject queued key events into the script and Blueprint runtimes.
        for (const auto &input : inputs)
        {
          if (input.down)
          {
            scriptRuntime_.on_key_down(input.keyCode);
            blueprintRuntime_.on_key_down(input.keyCode);
          }
          else
          {
            scriptRuntime_.on_key_up(input.keyCode);
            blueprintRuntime_.on_key_up(input.keyCode);
          }

          if (scriptRuntime_.faulted())
          {
            Log::error("script: %s", scriptRuntime_.last_error().c_str());
            running_ = false;
            break;
          }
        }

        // Advance the simulation by the requested number of ticks.
        constexpr float fixedDt = 1.0f / 60.0f;
        for (int i = 0; i < ticks && running_; ++i)
        {
          tick_frame(fixedDt);

          // Also pump SDL events so the OS doesn't think we're hung.
          SDL_Event event;
          while (SDL_PollEvent(&event))
          {
            if (event.type == SDL_QUIT)
            {
              running_ = false;
            }
          }

          if (renderer_)
          {
            renderer_->render_frame(window_.get());
            renderer_->present_frame();
          }
        }

        update_api_state();
        api_->signal_step_complete();
      }
    }

    api_->stop();
  }
#endif // HADES_ENABLE_API

  void GameRuntime::handle_pending_world_load()
  {
    auto worldName = ScriptRuntime::consume_pending_world_load();
    if (!worldName.has_value())
    {
      return;
    }

    const auto worldFilePath = projectPath_ / ".hades" / "worlds" / (*worldName + ".json");
    if (!std::filesystem::exists(worldFilePath))
    {
      Log::error("World file not found: %s", worldFilePath.string().c_str());
      return;
    }

    scriptRuntime_.stop();
    blueprintRuntime_.stop();

    if (activeWorld_.has_value())
    {
      destroy_world_tree(*activeWorld_, entityManager_, componentManager_);
    }

    // Entity ids carry no generation counter, so the ids the destroy above
    // just released go straight back onto the free list and the reload hands
    // them to brand-new entities. Any animator state still keyed by one of
    // them is adopted whole: the reloaded world would start mid-animation in
    // the previous occupant's state, with its own authored parameter, speed
    // and playOnStart overrides silently skipped (AnimatorSystem's "first
    // sight" test is what applies them, and an inherited instance makes it
    // false). destroy_world_tree drops the state for the entities it destroys,
    // but a Blueprint's Destroy Entity node in the same tick does not, so the
    // free list can still carry an id with live state.
    //
    // Take the live set here and purge exactly the ids the reload claims,
    // rather than clearing the whole runtime: every other world in the project
    // is loaded and running too (load_all_worlds at startup), and clearing
    // would yank all of their animators back to their authored defaults.
    const std::unordered_set<Entity::EntityId> liveBeforeLoad(
        entityManager_.getAllEntities().begin(), entityManager_.getAllEntities().end());

    auto newWorld = load_world_from_file(worldFilePath, entityManager_, componentManager_);
    if (!newWorld.has_value())
    {
      Log::error("Failed to load world: %s", worldName->c_str());
      running_ = false;
      return;
    }

    // Before the scripts start: onStart is allowed to call hades::Animation
    // and create instances, and those are the new world's own, not inherited.
    for (Entity::EntityId entity : entityManager_.getAllEntities())
    {
      if (liveBeforeLoad.find(entity) == liveBeforeLoad.end())
      {
        AnimationRuntime::instance().remove(entity);
      }
    }

    activeWorld_ = newWorld;

    if (physicsSystem_)
    {
      physicsSystem_->clear_bodies();
      physicsSystem_->set_active_world(newWorld);
    }
    if (audioSystem_)
    {
      audioSystem_->set_active_world(newWorld);
    }

    std::string scriptError;
    if (!scriptRuntime_.start(componentManager_, entityManager_, projectPath_, newWorld, &scriptError))
    {
      Log::error("World load script error: %s", scriptError.c_str());
      running_ = false;
      return;
    }

    std::string blueprintError;
    if (!blueprintRuntime_.start(componentManager_, entityManager_, projectPath_, newWorld, &blueprintError))
    {
      Log::error("World load blueprint error: %s", blueprintError.c_str());
      running_ = false;
    }
  }
}
