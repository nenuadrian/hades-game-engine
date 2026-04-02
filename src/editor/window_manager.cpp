#include "window_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "../engine/audio/audio_engine.hpp"
#include "../engine/rendering/renderer.hpp"
#include "../engine/rendering/vulkan.hpp"
#include "../engine/systems/audio_system.hpp"
#include "../engine/systems/movement_system.hpp"
#include "../engine/systems/render_system.hpp"

namespace
{
  constexpr char EDITOR_WINDOW_TITLE[] = "Hades Editor";
  constexpr char LOGO_ASSET_PATH[] = "assets/logo.bmp";
  constexpr int EDITOR_WINDOW_WIDTH = 1280;
  constexpr int EDITOR_WINDOW_HEIGHT = 720;
  constexpr int SPLASH_WINDOW_SIZE = 360;
  constexpr int SPLASH_WINDOW_PADDING = 28;
  constexpr std::uint32_t SPLASH_MINIMUM_DURATION_MS = 450;
  constexpr std::uint32_t SPLASH_FADE_DURATION_MS = 180;

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

  SurfacePtr load_logo_surface()
  {
    const std::string logo_path = asset_path(LOGO_ASSET_PATH);
    SDL_Surface *surface = SDL_LoadBMP(logo_path.c_str());
    if (surface == nullptr)
    {
      std::fprintf(stderr, "Warning: failed to load '%s': %s\n", logo_path.c_str(), SDL_GetError());
    }

    return SurfacePtr(surface, SDL_FreeSurface);
  }

  void set_window_icon(SDL_Window *window, SDL_Surface *icon_surface)
  {
    if (window != nullptr && icon_surface != nullptr)
    {
      SDL_SetWindowIcon(window, icon_surface);
    }
  }

  SDL_Rect centered_destination_rect(
      int source_width,
      int source_height,
      int destination_width,
      int destination_height,
      int padding)
  {
    const int available_width = std::max(destination_width - (padding * 2), 1);
    const int available_height = std::max(destination_height - (padding * 2), 1);
    const double scale = std::min(
        static_cast<double>(available_width) / static_cast<double>(source_width),
        static_cast<double>(available_height) / static_cast<double>(source_height));

    const int scaled_width = std::max(static_cast<int>(std::lround(source_width * scale)), 1);
    const int scaled_height = std::max(static_cast<int>(std::lround(source_height * scale)), 1);

    return SDL_Rect{
        (destination_width - scaled_width) / 2,
        (destination_height - scaled_height) / 2,
        scaled_width,
        scaled_height};
  }

  struct SplashScreen
  {
    ~SplashScreen()
    {
      if (window != nullptr)
      {
        SDL_DestroyWindow(window);
      }
    }

    bool show(SDL_Surface *logo_surface)
    {
      if (logo_surface == nullptr)
      {
        return true;
      }

      window = SDL_CreateWindow(
          "Hades",
          SDL_WINDOWPOS_CENTERED,
          SDL_WINDOWPOS_CENTERED,
          SPLASH_WINDOW_SIZE,
          SPLASH_WINDOW_SIZE,
          SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_ALWAYS_ON_TOP);
      if (window == nullptr)
      {
        std::fprintf(stderr, "Warning: SDL_CreateWindow() for splash failed: %s\n", SDL_GetError());
        return true;
      }

      SDL_Surface *window_surface = SDL_GetWindowSurface(window);
      if (window_surface == nullptr)
      {
        std::fprintf(stderr, "Warning: SDL_GetWindowSurface() for splash failed: %s\n", SDL_GetError());
        return true;
      }

      const Uint32 background = SDL_MapRGB(window_surface->format, 0, 0, 0);
      SDL_FillRect(window_surface, nullptr, background);

      SDL_Rect destination = centered_destination_rect(
          logo_surface->w,
          logo_surface->h,
          window_surface->w,
          window_surface->h,
          SPLASH_WINDOW_PADDING);
      SDL_BlitScaled(logo_surface, nullptr, window_surface, &destination);
      SDL_UpdateWindowSurface(window);

      shown_at = SDL_GetTicks();
      pump_events();
      return !cancelled;
    }

    bool finish()
    {
      if (window == nullptr)
      {
        return !cancelled;
      }

      const std::uint32_t elapsed = SDL_GetTicks() - shown_at;
      if (elapsed < SPLASH_MINIMUM_DURATION_MS)
      {
        wait_with_event_pump(SPLASH_MINIMUM_DURATION_MS - elapsed);
      }

      if (cancelled)
      {
        return false;
      }

      const Uint32 fade_started_at = SDL_GetTicks();
      const bool opacity_supported = (SDL_SetWindowOpacity(window, 1.0f) == 0);
      if (!opacity_supported)
      {
        wait_with_event_pump(SPLASH_FADE_DURATION_MS);
        return !cancelled;
      }

      while (!cancelled)
      {
        const std::uint32_t fade_elapsed = SDL_GetTicks() - fade_started_at;
        const float progress = std::min(
            static_cast<float>(fade_elapsed) / static_cast<float>(SPLASH_FADE_DURATION_MS),
            1.0f);

        SDL_SetWindowOpacity(window, 1.0f - progress);
        if (progress >= 1.0f)
        {
          break;
        }

        pump_events();
        SDL_Delay(16);
      }

      return !cancelled;
    }

  private:
    void wait_with_event_pump(std::uint32_t duration_ms)
    {
      const Uint32 started_at = SDL_GetTicks();
      while (!cancelled && (SDL_GetTicks() - started_at) < duration_ms)
      {
        pump_events();
        SDL_Delay(16);
      }
    }

    void pump_events()
    {
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        if (event.type == SDL_QUIT)
        {
          cancelled = true;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            window != nullptr &&
            event.window.windowID == SDL_GetWindowID(window))
        {
          cancelled = true;
        }
      }
    }

    SDL_Window *window = nullptr;
    Uint32 shown_at = 0;
    bool cancelled = false;
  };

  void apply_editor_theme()
  {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    const ImVec4 bg0 = ImVec4(0.08f, 0.06f, 0.06f, 1.00f);
    const ImVec4 bg1 = ImVec4(0.11f, 0.09f, 0.09f, 1.00f);
    const ImVec4 bg2 = ImVec4(0.15f, 0.12f, 0.12f, 1.00f);
    const ImVec4 bg3 = ImVec4(0.19f, 0.16f, 0.16f, 1.00f);
    const ImVec4 border = ImVec4(0.24f, 0.20f, 0.20f, 1.00f);
    const ImVec4 border_soft = ImVec4(0.17f, 0.14f, 0.14f, 1.00f);
    const ImVec4 text = ImVec4(0.93f, 0.90f, 0.88f, 1.00f);
    const ImVec4 text_muted = ImVec4(0.63f, 0.59f, 0.57f, 1.00f);
    const ImVec4 accent = ImVec4(0.70f, 0.66f, 0.63f, 1.00f);
    const ImVec4 accent_strong = ImVec4(0.84f, 0.79f, 0.76f, 1.00f);

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.45f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.CellPadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.03f, 0.50f);
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.00f, 0.50f);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_muted;
    colors[ImGuiCol_WindowBg] = bg0;
    colors[ImGuiCol_ChildBg] = bg0;
    colors[ImGuiCol_PopupBg] = bg1;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = bg1;
    colors[ImGuiCol_FrameBgHovered] = bg2;
    colors[ImGuiCol_FrameBgActive] = bg3;
    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg0;
    colors[ImGuiCol_TitleBgCollapsed] = bg0;
    colors[ImGuiCol_MenuBarBg] = bg0;
    colors[ImGuiCol_ScrollbarBg] = bg0;
    colors[ImGuiCol_ScrollbarGrab] = bg2;
    colors[ImGuiCol_ScrollbarGrabHovered] = bg3;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent_strong;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_strong;
    colors[ImGuiCol_Button] = bg1;
    colors[ImGuiCol_ButtonHovered] = bg2;
    colors[ImGuiCol_ButtonActive] = bg3;
    colors[ImGuiCol_Header] = bg1;
    colors[ImGuiCol_HeaderHovered] = bg2;
    colors[ImGuiCol_HeaderActive] = bg3;
    colors[ImGuiCol_Separator] = border_soft;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent_strong;
    colors[ImGuiCol_ResizeGrip] = border_soft;
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accent_strong;
    colors[ImGuiCol_Tab] = bg1;
    colors[ImGuiCol_TabHovered] = bg2;
    colors[ImGuiCol_TabActive] = bg2;
    colors[ImGuiCol_TabUnfocused] = bg0;
    colors[ImGuiCol_TabUnfocusedActive] = bg1;
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    colors[ImGuiCol_DockingEmptyBg] = bg0;
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accent_strong;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accent_strong;
    colors[ImGuiCol_TableHeaderBg] = bg1;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border_soft;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.18f);
    colors[ImGuiCol_DragDropTarget] = accent_strong;
    colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(accent_strong.x, accent_strong.y, accent_strong.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
  }
}

namespace hades
{
  void WindowManager::SDLWindowDeleter::operator()(SDL_Window *window) const
  {
    if (window != nullptr)
    {
      SDL_DestroyWindow(window);
    }
  }

  bool WindowManager::SdlSession::init(std::uint32_t flags)
  {
    if (initialized)
    {
      return true;
    }

    if (SDL_Init(flags) != 0)
    {
      std::fprintf(stderr, "Error: %s\n", SDL_GetError());
      return false;
    }

    initialized = true;

#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    return true;
  }

  WindowManager::SdlSession::~SdlSession()
  {
    if (initialized)
    {
      SDL_Quit();
    }
  }

  bool WindowManager::ImGuiSession::init(SDL_Window *window, Renderer &renderer)
  {
    if (initialized)
    {
      return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    apply_editor_theme();

    if (!ImGui_ImplSDL2_InitForVulkan(window))
    {
      std::fprintf(stderr, "Error: ImGui_ImplSDL2_InitForVulkan() failed.\n");
      ImGui::DestroyContext();
      return false;
    }

    renderer_ = &renderer;
    renderer_->init_imgui_backend();
    initialized = true;
    return true;
  }

  void WindowManager::ImGuiSession::begin_frame()
  {
    if (!initialized)
    {
      return;
    }

    renderer_->start_imgui_frame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
  }

  void WindowManager::ImGuiSession::render()
  {
    if (!initialized)
    {
      return;
    }

    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized)
    {
      renderer_->render_imgui(draw_data);
    }
  }

  WindowManager::ImGuiSession::~ImGuiSession()
  {
    shutdown();
  }

  void WindowManager::ImGuiSession::shutdown()
  {
    if (!initialized)
    {
      return;
    }

    renderer_->shutdown_imgui_backend();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    renderer_ = nullptr;
    initialized = false;
  }

  WindowManager::WindowManager()
      : renderer(std::make_unique<VulkanRenderer>()),
        audio_engine(std::make_unique<AudioEngine>()) {}

  WindowManager::~WindowManager()
  {
    scriptRuntime.stop();
  }

  void WindowManager::request_quit()
  {
    running = false;

    SDL_Event quit_event{};
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
  }

  void WindowManager::process_editor_events()
  {
    while (!editor.state.events.empty())
    {
      const auto event = editor.state.events.front();
      editor.state.events.pop();

      if (event == EDITOR_QUIT)
      {
        request_quit();
      }
    }
  }

  bool WindowManager::init()
  {
    if (initialized)
    {
      return true;
    }

    if (!sdl_session.init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
    {
      return false;
    }

    SurfacePtr logo_surface = load_logo_surface();
    SplashScreen splash_screen;
    if (!splash_screen.show(logo_surface.get()))
    {
      return false;
    }

    const SDL_WindowFlags window_flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    window.reset(SDL_CreateWindow(
        EDITOR_WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        EDITOR_WINDOW_WIDTH,
        EDITOR_WINDOW_HEIGHT,
        window_flags));
    if (window == nullptr)
    {
      std::fprintf(stderr, "Error: SDL_CreateWindow(): %s\n", SDL_GetError());
      return false;
    }

    set_window_icon(window.get(), logo_surface.get());

    if (!renderer->init(window.get()))
    {
      return false;
    }

    if (!imgui_session.init(window.get(), *renderer))
    {
      return false;
    }

    if (!audio_engine->init())
    {
      std::fprintf(stderr, "Warning: audio engine is unavailable. Audio playback has been disabled.\n");
      audio_engine.reset();
    }

    systemManager.registerSystem<MovementSystem>();
    systemManager.registerSystem<RenderSystem>();
    auto audioSystem = systemManager.registerSystem<AudioSystem>();
    audioSystem->setAudioEngine(audio_engine.get());
    SDL_ShowWindow(window.get());
    SDL_RaiseWindow(window.get());
    if (!splash_screen.finish())
    {
      return false;
    }

    SDL_RaiseWindow(window.get());
    initialized = true;
    return true;
  }

  void WindowManager::render_frame()
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
      {
        running = false;
      }
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window.get()))
      {
        running = false;
      }
    }

    if (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)
    {
      SDL_Delay(10);
      return;
    }

    renderer->render_frame(window.get());
    imgui_session.begin_frame();

    ImGuiIO &io = ImGui::GetIO();
    editor.render(io.DeltaTime, entityManager, componentManager, scriptRuntime);
    if (editor.state.isPlaying)
    {
      scriptRuntime.update(io.DeltaTime, componentManager, entityManager);
      if (scriptRuntime.faulted())
      {
        editor.state.isPlaying = false;
        editor.state.activeCamera.reset();
        editor.state.playModeMessage = scriptRuntime.last_error();
        if (audio_engine != nullptr)
        {
          audio_engine->stop_all();
        }
      }
      else
      {
        systemManager.updateSystems(io.DeltaTime, componentManager, entityManager);
      }
    }
    else if (wasPlayingLastFrame && audio_engine != nullptr)
    {
      audio_engine->stop_all();
    }
    wasPlayingLastFrame = editor.state.isPlaying;
    process_editor_events();
    if (!running)
    {
      return;
    }

    imgui_session.render();
  }

  int WindowManager::run()
  {
    if (!init())
    {
      return EXIT_FAILURE;
    }

    running = true;
    while (running)
    {
      render_frame();
    }

    return EXIT_SUCCESS;
  }
}
