#include "game_runtime.hpp"

#include <cstdio>
#include <cstdlib>

#include <SDL.h>

#include "engine/audio/audio_engine.hpp"
#include "engine/core/ecs/scene_serializer.hpp"
#include "engine/core/ecs/world_utils.hpp"
#include "engine/physics/physics_world.hpp"
#include "engine/rendering/vulkan.hpp"
#include "engine/runtime/main_camera_selection.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/movement_system.hpp"
#include "engine/systems/physics_system.hpp"
#include "engine/systems/render_system.hpp"

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
      std::fprintf(stderr, "Error: SDL_Init(): %s\n", SDL_GetError());
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
      : renderer_(std::make_unique<VulkanRenderer>()),
        audioEngine_(std::make_unique<AudioEngine>()),
        physicsWorld_(std::make_unique<PhysicsWorld>())
  {
  }

  GameRuntime::~GameRuntime()
  {
    scriptRuntime_.stop();
  }

  std::string GameRuntime::project_name() const
  {
    return projectPath_.filename().string();
  }

  bool GameRuntime::init(const std::filesystem::path &projectPath)
  {
    if (initialized_)
    {
      return true;
    }

    projectPath_ = projectPath;

    if (!sdlSession_.init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
    {
      return false;
    }

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
      std::fprintf(stderr, "Error: SDL_CreateWindow(): %s\n", SDL_GetError());
      return false;
    }

    set_window_icon(window_.get());

    if (!renderer_->init(window_.get()))
    {
      return false;
    }

    // Do NOT call init_imgui_backend -- the runtime has no editor UI.

    if (!audioEngine_->init())
    {
      std::fprintf(stderr, "Warning: audio engine is unavailable. Audio playback has been disabled.\n");
      audioEngine_.reset();
    }

    if (!physicsWorld_->init())
    {
      std::fprintf(stderr, "Warning: physics engine is unavailable. Physics simulation has been disabled.\n");
      physicsWorld_.reset();
    }

    physicsSystem_ = systemManager_.registerSystem<PhysicsSystem>();
    physicsSystem_->setPhysicsWorld(physicsWorld_.get());
    systemManager_.registerSystem<MovementSystem>();
    systemManager_.registerSystem<RenderSystem>();
    audioSystem_ = systemManager_.registerSystem<AudioSystem>();
    audioSystem_->setAudioEngine(audioEngine_.get());

    // Load all worlds from the project.
    std::string loadError;
    const auto worlds = load_all_worlds(projectPath_, entityManager_, componentManager_, &loadError);
    if (worlds.empty())
    {
      std::fprintf(stderr, "Error: failed to load worlds: %s\n", loadError.c_str());
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
      std::fprintf(stderr, "Warning: %s\n", main_camera_selection_message(cameraSelection.status));
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
        std::fprintf(stderr, "Warning: script runtime failed to start: %s\n", scriptError.c_str());
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
        if (scriptRuntime_.faulted())
        {
          std::fprintf(stderr, "Script error: %s\n", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
      else if (event.type == SDL_KEYUP)
      {
        scriptRuntime_.on_key_up(static_cast<int>(event.key.keysym.sym));
        if (scriptRuntime_.faulted())
        {
          std::fprintf(stderr, "Script error: %s\n", scriptRuntime_.last_error().c_str());
          running_ = false;
        }
      }
    }

    // Handle swap chain management (resize, etc.).
    renderer_->render_frame(window_.get());

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
      scriptRuntime_.update(deltaTime, componentManager_, entityManager_);
      if (scriptRuntime_.faulted())
      {
        std::fprintf(stderr, "Script error: %s\n", scriptRuntime_.last_error().c_str());
        running_ = false;
        return;
      }
    }

    // Update all engine systems (physics, movement, rendering, audio).
    systemManager_.updateSystems(deltaTime, componentManager_, entityManager_);

    // Present the frame (clear + present, no ImGui).
    renderer_->present_frame();
  }

  int GameRuntime::run()
  {
    if (!initialized_)
    {
      return EXIT_FAILURE;
    }

    running_ = true;
    while (running_)
    {
      render_frame();
    }

    scriptRuntime_.stop();
    if (audioEngine_ != nullptr)
    {
      audioEngine_->stop_all();
    }

    return EXIT_SUCCESS;
  }
}
