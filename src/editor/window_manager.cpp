#include "window_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <system_error>

#include <SDL.h>

#include "native_dialogs.hpp"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#ifdef HADES_ENABLE_API
#include "../engine/api/hades_api.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/core/ecs/query.hpp"
#include <nlohmann/json.hpp>
#endif
#include "../engine/components/position_component_3d.hpp"
#include "../engine/core/log.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"
#include "../engine/audio/audio_engine.hpp"
#include "../engine/audio/script_audio.hpp"
#include "../engine/rendering/renderer.hpp"
#include "../engine/profiling/frame_metrics.hpp"
#ifdef HADES_PLATFORM_WEB
#include "../engine/rendering/webgpu_renderer.hpp"
using EditorRenderer = hades::WebGPURenderer;
#else
#include "../engine/rendering/vulkan.hpp"
using EditorRenderer = hades::VulkanRenderer;
#endif
#include "../engine/physics/physics_world.hpp"
#include "../engine/systems/audio_system.hpp"
#include "../engine/systems/movement_system.hpp"
#include "../engine/systems/physics_system.hpp"
#include "../engine/systems/render_system.hpp"

namespace
{
  constexpr char EDITOR_WINDOW_TITLE[] = "Hades Editor";
  constexpr char WORKSPACE_HISTORY_FILENAME[] = "recent_workspaces.txt";
  constexpr char LOGO_ASSET_PATH[] = "assets/logo.bmp";
  constexpr int EDITOR_WINDOW_WIDTH = 1280;
  constexpr int EDITOR_WINDOW_HEIGHT = 720;
  constexpr int WORKSPACE_LOGO_MAX_SIZE = 160;

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
      hades::Log::warn("editor", "failed to load '%s': %s", logo_path.c_str(), SDL_GetError());
    }

    return SurfacePtr(surface, SDL_FreeSurface);
  }

  void apply_editor_hover_cursor()
  {
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (context == nullptr)
    {
      return;
    }

    ImGuiContext &g = *context;
    ImGuiIO &io = ImGui::GetIO();
    if (io.MouseDrawCursor || (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0)
    {
      return;
    }

    if (ImGui::GetMouseCursor() != ImGuiMouseCursor_Arrow)
    {
      return;
    }

    // Dear ImGui leaves the default arrow cursor in place for many clickable widgets.
    // In the editor we prefer a pointing hand whenever a live item is hoverable/clickable,
    // while preserving more specific cursors such as text input and resize handles.
    if (g.HoveredId != 0 && !g.HoveredIdIsDisabled && g.ActiveId == 0)
    {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
  }

  void set_window_icon(SDL_Window *window, SDL_Surface *icon_surface)
  {
    if (window != nullptr && icon_surface != nullptr)
    {
      SDL_SetWindowIcon(window, icon_surface);
    }
  }

  std::filesystem::path fallback_preferences_directory()
  {
#ifdef _WIN32
    const char *appData = std::getenv("APPDATA");
    if (appData != nullptr && appData[0] != '\0')
    {
      return std::filesystem::path(appData) / "Hades";
    }
#else
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
      return std::filesystem::path(home) / ".config" / "hades";
    }
#endif

    std::error_code errorCode;
    const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(errorCode);
    if (!errorCode)
    {
      return tempDirectory / "hades";
    }

    return std::filesystem::path("hades");
  }

  std::filesystem::path workspace_history_path()
  {
    SdlStringPtr pref_path(SDL_GetPrefPath("Hades", "Editor"), SDL_free);
    if (pref_path)
    {
      return std::filesystem::path(pref_path.get()) / WORKSPACE_HISTORY_FILENAME;
    }

    return fallback_preferences_directory() / WORKSPACE_HISTORY_FILENAME;
  }

  std::string trim_copy(std::string_view value)
  {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0)
    {
      ++first;
    }

    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
    {
      --last;
    }

    return std::string(value.substr(first, last - first));
  }

  template <std::size_t Size>
  void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
  {
    buffer.fill('\0');
    const std::size_t copyLength = std::min(value.size(), Size - 1);
    std::copy_n(value.data(), copyLength, buffer.data());
    buffer[copyLength] = '\0';
  }

  std::optional<Uint32> event_window_id(const SDL_Event &event)
  {
    switch (event.type)
    {
    case SDL_WINDOWEVENT:
      return event.window.windowID;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      return event.key.windowID;
    case SDL_TEXTEDITING:
    case SDL_TEXTINPUT:
      return event.text.windowID;
    case SDL_MOUSEMOTION:
      return event.motion.windowID;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      return event.button.windowID;
    case SDL_MOUSEWHEEL:
      return event.wheel.windowID;
    case SDL_DROPFILE:
    case SDL_DROPTEXT:
    case SDL_DROPBEGIN:
    case SDL_DROPCOMPLETE:
      return event.drop.windowID;
    default:
      break;
    }

    return std::nullopt;
  }

  void build_workspace_logo_preview(
      SDL_Surface *logoSurface,
      std::vector<std::uint32_t> &pixels,
      int &width,
      int &height)
  {
    pixels.clear();
    width = 0;
    height = 0;

    if (logoSurface == nullptr)
    {
      return;
    }

    SurfacePtr rgbaSurface(SDL_ConvertSurfaceFormat(logoSurface, SDL_PIXELFORMAT_RGBA32, 0), SDL_FreeSurface);
    if (rgbaSurface == nullptr)
    {
      hades::Log::warn("editor", "failed to convert logo surface for workspace preview: %s", SDL_GetError());
      return;
    }

    const double scale = std::min(
        1.0,
        std::min(
            static_cast<double>(WORKSPACE_LOGO_MAX_SIZE) / static_cast<double>(rgbaSurface->w),
            static_cast<double>(WORKSPACE_LOGO_MAX_SIZE) / static_cast<double>(rgbaSurface->h)));

    width = std::max(static_cast<int>(std::lround(static_cast<double>(rgbaSurface->w) * scale)), 1);
    height = std::max(static_cast<int>(std::lround(static_cast<double>(rgbaSurface->h) * scale)), 1);

    SurfacePtr scaledSurface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32),
        SDL_FreeSurface);
    if (scaledSurface == nullptr)
    {
      hades::Log::warn("editor", "failed to allocate logo preview surface: %s", SDL_GetError());
      width = 0;
      height = 0;
      return;
    }

    if (SDL_BlitScaled(rgbaSurface.get(), nullptr, scaledSurface.get(), nullptr) != 0)
    {
      hades::Log::warn("editor", "failed to scale logo preview surface: %s", SDL_GetError());
      pixels.clear();
      width = 0;
      height = 0;
      return;
    }

    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
      const auto *row = static_cast<const std::uint8_t *>(scaledSurface->pixels) + (y * scaledSurface->pitch);
      for (int x = 0; x < width; ++x)
      {
        const auto *rawPixel = reinterpret_cast<const Uint32 *>(row + (x * sizeof(Uint32)));
        Uint8 red = 0;
        Uint8 green = 0;
        Uint8 blue = 0;
        Uint8 alpha = 0;
        SDL_GetRGBA(*rawPixel, scaledSurface->format, &red, &green, &blue, &alpha);
        pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)] =
            IM_COL32(red, green, blue, alpha);
      }
    }
  }

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
    style.WindowMenuButtonPosition = ImGuiDir_None;
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
      Log::error("editor", "%s", SDL_GetError());
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

  bool WindowManager::ImGuiSession::init(SDL_Window *window, Renderer &renderer, bool enableViewports)
  {
    if (initialized)
    {
      return true;
    }

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifdef __APPLE__
    io.ConfigMacOSXBehaviors = true;
#endif
#ifndef HADES_PLATFORM_WEB
    if (enableViewports)
    {
      io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
#else
    (void)enableViewports;  // multi-viewport requires multiple OS windows
#endif
    apply_editor_theme();

    // Load a more readable proportional font (bundled with ImGui) and fall
    // back to the built-in default if it isn't present — we must have at
    // least one base font before merging the icon glyphs below.
    bool baseFontLoaded = false;
    {
      const std::string fontPath = asset_path("_deps/imgui-src/misc/fonts/DroidSans.ttf");
      if (std::filesystem::exists(fontPath))
      {
        baseFontLoaded = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f) != nullptr;
      }
    }
    if (!baseFontLoaded)
    {
      io.Fonts->AddFontDefault();
    }

    // Merge FontAwesome 6 icon glyphs into the default font.
    {
      const std::string iconFontPath = asset_path("assets/fonts/fa-solid-900.ttf");
      if (std::filesystem::exists(iconFontPath))
      {
        static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.PixelSnapH = true;
        iconConfig.GlyphMinAdvanceX = 16.0f;
        iconConfig.GlyphOffset = ImVec2(0.0f, 2.0f);
        io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 14.0f, &iconConfig, iconRanges);
      }
    }

#ifdef HADES_PLATFORM_WEB
    if (!ImGui_ImplSDL2_InitForOther(window))
    {
      Log::error("ImGui_ImplSDL2_InitForOther() failed.");
      ImGui::DestroyContext(context_);
      context_ = nullptr;
      return false;
    }
#else
    if (!ImGui_ImplSDL2_InitForVulkan(window))
    {
      Log::error("ImGui_ImplSDL2_InitForVulkan() failed.");
      ImGui::DestroyContext(context_);
      context_ = nullptr;
      return false;
    }
#endif

    renderer_ = &renderer;
    enableViewports_ = enableViewports;
    renderer_->init_imgui_backend();
    initialized = true;
    return true;
  }

  void WindowManager::ImGuiSession::process_event(const SDL_Event &event)
  {
    if (!initialized || context_ == nullptr)
    {
      return;
    }

    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDL2_ProcessEvent(&event);
  }

  void WindowManager::ImGuiSession::begin_frame()
  {
    if (!initialized || context_ == nullptr)
    {
      return;
    }

    ImGui::SetCurrentContext(context_);
    renderer_->start_imgui_frame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
  }

  void WindowManager::ImGuiSession::render()
  {
    if (!initialized || context_ == nullptr)
    {
      return;
    }

    ImGui::SetCurrentContext(context_);
    apply_editor_hover_cursor();
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized)
    {
      renderer_->render_imgui(draw_data);
    }

    if (enableViewports_)
    {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }
  }

  void WindowManager::ImGuiSession::close()
  {
    shutdown();
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

    ImGuiContext *previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    renderer_->shutdown_imgui_backend();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context_);
    if (previousContext != context_)
    {
      ImGui::SetCurrentContext(previousContext);
    }
    else
    {
      ImGui::SetCurrentContext(nullptr);
    }
    context_ = nullptr;
    renderer_ = nullptr;
    enableViewports_ = false;
    initialized = false;
  }

  WindowManager::WindowManager()
      : renderer(std::make_unique<EditorRenderer>()),
        audio_engine(std::make_unique<AudioEngine>()),
        physics_world(std::make_unique<PhysicsWorld>()) {}

  WindowManager::~WindowManager()
  {
    scriptRuntime.stop();
    // Release the AudioEngine pointer from the scripting facade before the
    // unique_ptr tears it down, so any lingering Audio::* calls are no-ops.
    register_script_audio_engine(nullptr);
  }

  void WindowManager::request_quit()
  {
    running = false;

    SDL_Event quit_event{};
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
  }

  bool WindowManager::persist_workspace_state(
      const std::filesystem::path &workspacePath,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return true;
    }

    std::string worldError;
    std::string settingsError;
    const bool savedWorlds = hades::save_all_worlds(workspacePath, entityManager, componentManager, &worldError);
    const bool savedSettings = editor.save_workspace_settings(workspacePath, &settingsError);
    if (savedWorlds && savedSettings)
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return true;
    }

    if (errorMessage != nullptr)
    {
      errorMessage->clear();
      if (!savedWorlds)
      {
        *errorMessage = "Failed to save worlds: " + worldError;
      }
      if (!savedSettings)
      {
        if (!errorMessage->empty())
        {
          *errorMessage += " ";
        }
        *errorMessage += "Failed to save settings: " + settingsError;
      }
    }

    return false;
  }

  void WindowManager::process_editor_events()
  {
    while (!editor.state.events.empty())
    {
      const auto event = editor.state.events.front();
      editor.state.events.pop();

      if (event == EDITOR_QUIT)
      {
        if (workspaceManager.has_current_workspace())
        {
          reset_workspace_session();
          workspaceManager.close_current_workspace();
        }
        else
        {
          request_quit();
        }
      }
    }
  }

  void WindowManager::render_workspace_logo() const
  {
    if (workspaceLogoPixels.empty() || workspaceLogoWidth <= 0 || workspaceLogoHeight <= 0)
    {
      return;
    }

    const ImVec2 logoSize(static_cast<float>(workspaceLogoWidth), static_cast<float>(workspaceLogoHeight));
    const float startX = ImGui::GetCursorPosX() + std::max((ImGui::GetContentRegionAvail().x - logoSize.x) * 0.5f, 0.0f);
    ImGui::SetCursorPosX(startX);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    for (int y = 0; y < workspaceLogoHeight; ++y)
    {
      for (int x = 0; x < workspaceLogoWidth; ++x)
      {
        const std::uint32_t color =
            workspaceLogoPixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(workspaceLogoWidth)) + static_cast<std::size_t>(x)];
        if ((color & IM_COL32_A_MASK) == 0)
        {
          continue;
        }

        drawList->AddRectFilled(
            ImVec2(origin.x + static_cast<float>(x), origin.y + static_cast<float>(y)),
            ImVec2(origin.x + static_cast<float>(x + 1), origin.y + static_cast<float>(y + 1)),
            color);
      }
    }

    ImGui::Dummy(ImVec2(logoSize.x, logoSize.y));
  }

  void WindowManager::render_workspace_selector()
  {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("Workspace Selector", nullptr, windowFlags))
    {
      ImGui::End();
      ImGui::PopStyleVar(2);
      return;
    }

    ImGui::SetCursorPosY(24.0f);
    render_workspace_logo();

    if (!workspaceStatusMessage.empty())
    {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.22f, 0.11f, 0.10f, 0.90f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.66f, 0.34f, 0.31f, 0.85f));
      ImGui::BeginChild("Workspace Status", ImVec2(0.0f, 54.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.80f, 0.75f, 1.00f));
      ImGui::TextWrapped("%s", workspaceStatusMessage.c_str());
      ImGui::PopStyleColor();
      ImGui::EndChild();
      ImGui::PopStyleColor(2);
      ImGui::Dummy(ImVec2(0.0f, 14.0f));
    }
    else
    {
      ImGui::Dummy(ImVec2(0.0f, 18.0f));
    }

    std::string pruneError;
    if (!workspaceManager.prune_missing_recent_workspaces(&pruneError) && workspaceStatusMessage.empty())
    {
      workspaceStatusMessage = pruneError;
    }

    const float buttonGap = 14.0f;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - buttonGap) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.13f, 0.13f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.18f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.29f, 0.23f, 0.22f, 1.00f));
    if (ImGui::Button("Create New", ImVec2(buttonWidth, 0.0f)))
    {
      std::string pickerError;
      const auto pickedFolder = hades::pick_folder_with_native_dialog("Select a location for the new workspace", &pickerError);
      if (pickedFolder.has_value())
      {
        set_buffer_text(createWorkspaceParentBuffer, pickedFolder->string());
        workspaceStatusMessage.clear();
        creatingWorkspace = true;
      }
      else if (!pickerError.empty())
      {
        workspaceStatusMessage = pickerError;
      }
    }

    ImGui::SameLine(0.0f, buttonGap);
    if (ImGui::Button("Browse Existing", ImVec2(buttonWidth, 0.0f)))
    {
      std::string pickerError;
      const auto pickedFolder = hades::pick_folder_with_native_dialog("Select a workspace folder", &pickerError);
      if (pickedFolder.has_value())
      {
        workspaceStatusMessage.clear();
        open_workspace(pickedFolder->string());
      }
      else if (!pickerError.empty())
      {
        workspaceStatusMessage = pickerError;
      }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 18.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.08f, 0.08f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.15f, 0.15f, 1.00f));
    ImGui::BeginChild("Recent Workspaces List", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

    if (workspaceManager.recent_workspaces().empty())
    {
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      ImGui::TextDisabled("No existing projects yet.");
    }
    else
    {
      for (std::size_t index = 0; index < workspaceManager.recent_workspaces().size(); ++index)
      {
        const WorkspaceEntry &workspace = workspaceManager.recent_workspaces()[index];
        ImGui::PushID(static_cast<int>(index));

        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const ImVec2 cardSize(cardWidth, 58.0f);
        const ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);

        ImGui::InvisibleButton("workspace_card", cardSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 fillColor = hovered ? IM_COL32(42, 33, 33, 255) : IM_COL32(28, 24, 24, 255);
        const ImU32 borderColor = hovered ? IM_COL32(170, 150, 144, 255) : IM_COL32(66, 56, 56, 255);
        const ImU32 accentColor = hovered ? IM_COL32(214, 200, 192, 255) : IM_COL32(140, 128, 122, 255);

        drawList->AddRectFilled(cardMin, cardMax, fillColor, 6.0f);
        drawList->AddRect(cardMin, cardMax, borderColor, 6.0f, 0, 1.0f);
        drawList->AddRectFilled(cardMin, ImVec2(cardMin.x + 4.0f, cardMax.y), accentColor, 6.0f, ImDrawFlags_RoundCornersLeft);

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 18.0f, cardMin.y + 10.0f));
        ImGui::TextUnformatted(workspace.name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 18.0f, cardMin.y + 31.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.64f, 0.61f, 0.60f, 1.00f));
        ImGui::PushTextWrapPos(cardMax.x - 14.0f);
        ImGui::TextUnformatted(workspace.path.string().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        if (clicked)
        {
          open_workspace(workspace.path.string());
        }

        if (index + 1 < workspaceManager.recent_workspaces().size())
        {
          ImGui::Dummy(ImVec2(0.0f, 10.0f));
        }

        ImGui::PopID();
      }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    if (creatingWorkspace)
    {
      ImGui::OpenPopup("Create Workspace");
      creatingWorkspace = false;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create Workspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::InputText("Workspace Name", createWorkspaceNameBuffer.data(), createWorkspaceNameBuffer.size());
      ImGui::InputText("Location", createWorkspaceParentBuffer.data(), createWorkspaceParentBuffer.size());

      if (ImGui::Button("Browse Location..."))
      {
        std::string pickerError;
        const auto pickedFolder = hades::pick_folder_with_native_dialog("Select a workspace folder", &pickerError);
        if (pickedFolder.has_value())
        {
          set_buffer_text(createWorkspaceParentBuffer, pickedFolder->string());
          workspaceStatusMessage.clear();
        }
        else if (!pickerError.empty())
        {
          workspaceStatusMessage = pickerError;
        }
      }

      const std::string workspaceName = trim_copy(createWorkspaceNameBuffer.data());
      const std::string parentDirectory = trim_copy(createWorkspaceParentBuffer.data());
      if (!workspaceName.empty() && !parentDirectory.empty())
      {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", (std::filesystem::path(parentDirectory) / workspaceName).string().c_str());
      }

      ImGui::Spacing();

      const bool canCreateWorkspace = !workspaceName.empty() && !parentDirectory.empty();
      if (!canCreateWorkspace)
      {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Create Workspace", ImVec2(180.0f, 0.0f)))
      {
        create_workspace();
        if (workspaceManager.has_current_workspace())
        {
          ImGui::CloseCurrentPopup();
        }
      }
      if (!canCreateWorkspace)
      {
        ImGui::EndDisabled();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
      {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  void WindowManager::open_workspace(const std::string &workspacePath)
  {
    const std::filesystem::path previousWorkspacePath =
        workspaceManager.current_workspace().has_value()
            ? workspaceManager.current_workspace()->path
            : std::filesystem::path();

    if (!previousWorkspacePath.empty())
    {
      std::string saveError;
      if (!persist_workspace_state(previousWorkspacePath, &saveError))
      {
        workspaceStatusMessage = "Failed to save current workspace: " + saveError;
      }
    }

    // Save ImGui layout to current workspace before switching away.
    if (!previousWorkspacePath.empty() && !imguiIniPath_.empty())
    {
      ImGui::SaveIniSettingsToDisk(imguiIniPath_.c_str());
    }

    std::string errorMessage;
    const auto workspace = workspaceManager.open_workspace(std::filesystem::path(trim_copy(workspacePath)), &errorMessage);
    if (!workspace.has_value())
    {
      workspaceStatusMessage = errorMessage.empty() ? "Unable to open the selected workspace folder." : errorMessage;
      return;
    }

    if (workspace->path != previousWorkspacePath)
    {
      reset_workspace_session();
    }

    // Point ImGui ini at the workspace .hades directory.
    {
      const auto hadesDir = workspace->path / ".hades";
      std::error_code ec;
      std::filesystem::create_directories(hadesDir, ec);
      imguiIniPath_ = (hadesDir / "imgui.ini").string();
      ImGui::GetIO().IniFilename = imguiIniPath_.c_str();
      ImGui::LoadIniSettingsFromDisk(imguiIniPath_.c_str());
    }

    creatingWorkspace = false;
    workspaceStatusMessage = errorMessage;
    std::string settingsError;
    if (!editor.load_workspace_settings(workspace->path, &settingsError))
    {
      workspaceStatusMessage =
          (workspaceStatusMessage.empty() ? std::string() : workspaceStatusMessage + " ") +
          "Failed to load workspace settings: " + settingsError;
    }
    set_buffer_text(createWorkspaceParentBuffer, workspace->path.parent_path().string());
    update_window_title();
  }

  void WindowManager::create_workspace()
  {
    const std::filesystem::path previousWorkspacePath =
        workspaceManager.current_workspace().has_value()
            ? workspaceManager.current_workspace()->path
            : std::filesystem::path();

    if (!previousWorkspacePath.empty())
    {
      std::string saveError;
      if (!persist_workspace_state(previousWorkspacePath, &saveError))
      {
        workspaceStatusMessage = "Failed to save current workspace: " + saveError;
      }
    }

    if (!previousWorkspacePath.empty() && !imguiIniPath_.empty())
    {
      ImGui::SaveIniSettingsToDisk(imguiIniPath_.c_str());
    }

    std::string errorMessage;
    const auto workspace = workspaceManager.create_workspace(
        std::filesystem::path(trim_copy(createWorkspaceParentBuffer.data())),
        trim_copy(createWorkspaceNameBuffer.data()),
        &errorMessage);
    if (!workspace.has_value())
    {
      workspaceStatusMessage = errorMessage.empty() ? "Unable to create the workspace folder." : errorMessage;
      return;
    }

    if (workspace->path != previousWorkspacePath)
    {
      reset_workspace_session();
    }

    {
      const auto hadesDir = workspace->path / ".hades";
      std::error_code ec;
      std::filesystem::create_directories(hadesDir, ec);
      imguiIniPath_ = (hadesDir / "imgui.ini").string();
      ImGui::GetIO().IniFilename = imguiIniPath_.c_str();
      ImGui::LoadIniSettingsFromDisk(imguiIniPath_.c_str());
    }

    seed_default_world_entities(workspace->path);

    creatingWorkspace = false;
    workspaceStatusMessage = errorMessage;
    std::string settingsError;
    if (!editor.load_workspace_settings(workspace->path, &settingsError))
    {
      workspaceStatusMessage =
          (workspaceStatusMessage.empty() ? std::string() : workspaceStatusMessage + " ") +
          "Failed to load workspace settings: " + settingsError;
    }
    set_buffer_text(createWorkspaceParentBuffer, workspace->path.parent_path().string());
    set_buffer_text(createWorkspaceNameBuffer, std::string());
    update_window_title();
  }

  void WindowManager::seed_default_world_entities(const std::filesystem::path &workspacePath)
  {
    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);

    const auto camera = EntityFactory::createCamera(entityManager, componentManager, world);
    if (componentManager.hasComponent<PositionComponent3D>(camera))
    {
      componentManager.getComponent<PositionComponent3D>(camera) = PositionComponent3D(0.0f, 0.0f, -5.0f);
    }

    EntityFactory::createCube(entityManager, componentManager, world);

    std::string worldError;
    if (!hades::save_all_worlds(workspacePath, entityManager, componentManager, &worldError))
    {
      workspaceStatusMessage = "Failed to seed default world: " + worldError;
    }
  }

  void WindowManager::reset_workspace_session()
  {
    scriptRuntime.stop();
#ifdef HADES_ENABLE_API
    stop_preview_api();
#endif
    if (audio_engine != nullptr)
    {
      audio_engine->stop_all();
    }

#ifndef HADES_PLATFORM_WEB
    playWindow.close();
#endif
    entityManager = EntityManager();
    componentManager = ComponentManager(&entityManager);
    eventBus.clear();
    editor.reset_workspace_session();
    wasPlayingLastFrame = false;

    if (audioSystem != nullptr)
    {
      audioSystem->set_active_world(std::nullopt);
    }
  }

  void WindowManager::stop_active_play_mode(const std::string &message)
  {
    editor.stop_play_mode(entityManager, componentManager, scriptRuntime);
    editor.state.pendingPlayAction = EditorPlayAction::None;
    editor.state.playModeMessage = message;
    if (!message.empty())
    {
      editor.log_error("Play mode stopped: " + message);
    }
#ifndef HADES_PLATFORM_WEB
    playWindow.close();
#endif

    if (audio_engine != nullptr)
    {
      audio_engine->stop_all();
    }
    if (audioSystem != nullptr)
    {
      audioSystem->set_active_world(std::nullopt);
    }
    if (physicsSystem != nullptr)
    {
      physicsSystem->set_active_world(std::nullopt);
      physicsSystem->clear_bodies();
    }
#ifdef HADES_ENABLE_API
    stop_preview_api();
#endif
  }

  void WindowManager::sync_play_window()
  {
#ifdef HADES_PLATFORM_WEB
    // Detached play window (a second SDL window) is not supported on the web.
#else
    if (!editor.state.isPlaying)
    {
      playWindow.close();
      return;
    }

    std::string errorMessage;
    if (!playWindow.is_open() && !playWindow.open(&errorMessage))
    {
      stop_active_play_mode(
          errorMessage.empty()
              ? "Unable to open the detached play window."
              : errorMessage);
      return;
    }

    playWindow.render(
        entityManager,
        componentManager,
        editor.state.activeWorld,
        editor.state.activeCamera);
#endif
  }

#ifdef HADES_ENABLE_API
  void WindowManager::start_preview_api_if_needed()
  {
    if (!editor.game_preview_hades_api_enabled())
    {
      stop_preview_api();
      return;
    }

    if (previewApi_ && previewApi_->is_running())
    {
      return;
    }

    previewApi_ = std::make_unique<HadesAPI>();
    HadesAPI::Config config;
    config.port = 7777;
    if (!previewApi_->start(config))
    {
      previewApi_.reset();
      editor.log_error("Game Preview HadesAPI failed to start.");
      return;
    }

    previewApi_->set_game_over(false);
    previewApi_->set_observed_state(scriptRuntime.collect_observations());
    previewApi_->set_entity_state(collect_preview_entity_state_json());
    editor.log_info("Game Preview HadesAPI is listening on http://localhost:7777");
  }

  void WindowManager::stop_preview_api()
  {
    if (previewApi_)
    {
      previewApi_->set_game_over(true);
      previewApi_->stop();
      previewApi_.reset();
    }
  }

  std::string WindowManager::collect_preview_entity_state_json()
  {
    if (!editor.state.activeWorld.has_value())
    {
      return "[]";
    }

    const auto entities = query<PositionComponent3D>(entityManager, componentManager, editor.state.activeWorld);
    nlohmann::json response = nlohmann::json::array();

    for (const auto entityId : entities)
    {
      nlohmann::json row;
      row["id"] = entityId;
      if (componentManager.hasComponent<NameComponent>(entityId))
      {
        row["name"] = componentManager.getComponent<NameComponent>(entityId).value;
      }

      const auto &position = componentManager.getComponent<PositionComponent3D>(entityId);
      row["position"] = {
          {"x", position.x},
          {"y", position.y},
          {"z", position.z},
      };
      response.push_back(std::move(row));
    }

    return response.dump();
  }
#endif

  void WindowManager::update_window_title()
  {
    if (window == nullptr)
    {
      return;
    }

    if (!workspaceManager.current_workspace().has_value())
    {
      SDL_SetWindowTitle(window.get(), EDITOR_WINDOW_TITLE);
      return;
    }

    const WorkspaceEntry &workspace = workspaceManager.current_workspace().value();
    const std::string title = std::string(EDITOR_WINDOW_TITLE) + " - " + workspace.name;
    SDL_SetWindowTitle(window.get(), title.c_str());
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

    workspaceManager.set_storage_path(workspace_history_path());
    std::string workspaceLoadError;
    if (!workspaceManager.load(&workspaceLoadError))
    {
      workspaceStatusMessage = workspaceLoadError;
    }

    std::error_code errorCode;
    const std::filesystem::path defaultCreateDirectory = workspaceManager.recent_workspaces().empty()
                                                             ? std::filesystem::current_path(errorCode)
                                                             : workspaceManager.recent_workspaces().front().path.parent_path();
    if (!errorCode)
    {
      set_buffer_text(createWorkspaceParentBuffer, defaultCreateDirectory.string());
    }

    SurfacePtr logo_surface = load_logo_surface();
    build_workspace_logo_preview(
        logo_surface.get(),
        workspaceLogoPixels,
        workspaceLogoWidth,
        workspaceLogoHeight);

#ifdef HADES_PLATFORM_WEB
    const SDL_WindowFlags window_flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
#else
    const SDL_WindowFlags window_flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
#endif
    window.reset(SDL_CreateWindow(
        EDITOR_WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        EDITOR_WINDOW_WIDTH,
        EDITOR_WINDOW_HEIGHT,
        window_flags));
    if (window == nullptr)
    {
      Log::error("editor", "SDL_CreateWindow(): %s", SDL_GetError());
      return false;
    }

    set_window_icon(window.get(), logo_surface.get());

    if (!renderer->init(window.get()))
    {
      return false;
    }

    if (!imgui_session.init(window.get(), *renderer, false))
    {
      return false;
    }

    editor.set_renderer(renderer.get());

    if (!audio_engine->init())
    {
      Log::warn("audio engine is unavailable. Audio playback has been disabled.");
      audio_engine.reset();
    }
    // Expose the live AudioEngine to scripts for procedural audio via the
    // hades::Audio facade and hades::HadesAPI::audioEngine().
    register_script_audio_engine(audio_engine.get());

    if (!physics_world->init())
    {
      Log::warn("physics engine is unavailable. Physics simulation has been disabled.");
      physics_world.reset();
    }

    componentManager = ComponentManager(&entityManager);

    physicsSystem = systemManager.registerSystem<PhysicsSystem>(SystemPhase::Physics);
    physicsSystem->setPhysicsWorld(physics_world.get());
    systemManager.registerSystem<MovementSystem>(SystemPhase::Logic);
    renderSystem = systemManager.registerSystem<RenderSystem>(SystemPhase::Render);
    audioSystem = systemManager.registerSystem<AudioSystem>(SystemPhase::Audio);
    audioSystem->setAudioEngine(audio_engine.get());
    update_window_title();
    SDL_ShowWindow(window.get());
    SDL_RaiseWindow(window.get());
    initialized = true;
    return true;
  }

  void WindowManager::render_frame()
  {
    HADES_FRAME_METRIC_SCOPE("frame_total");

    {
      HADES_FRAME_METRIC_SCOPE("event_poll");
      const Uint32 editorWindowId = window != nullptr ? SDL_GetWindowID(window.get()) : 0U;
      bool closedAuxiliaryWindowThisFrame = false;
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        const auto targetWindowId = event_window_id(event);
#ifdef HADES_PLATFORM_WEB
        const std::optional<Uint32> playWindowId;
#else
        const auto playWindowId = playWindow.window_id();
#endif
        if (!targetWindowId.has_value() || *targetWindowId == editorWindowId)
        {
          imgui_session.process_event(event);
        }
        if (event.type == SDL_QUIT)
        {
          if (!closedAuxiliaryWindowThisFrame)
          {
            running = false;
          }
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE)
        {
          if (event.window.windowID == editorWindowId)
          {
            if (workspaceManager.has_current_workspace())
            {
              reset_workspace_session();
              workspaceManager.close_current_workspace();
              closedAuxiliaryWindowThisFrame = true;
            }
            else
            {
              running = false;
            }
          }
          else if (playWindowId.has_value() &&
                   event.window.windowID == *playWindowId)
          {
            closedAuxiliaryWindowThisFrame = true;
            stop_active_play_mode();
          }
        }

        if (editor.state.isPlaying)
        {
          if (event.type == SDL_KEYDOWN)
          {
            scriptRuntime.on_key_down(static_cast<int>(event.key.keysym.sym));
            if (scriptRuntime.faulted())
            {
              stop_active_play_mode(scriptRuntime.last_error());
            }
          }
          else if (event.type == SDL_KEYUP)
          {
            scriptRuntime.on_key_up(static_cast<int>(event.key.keysym.sym));
            if (scriptRuntime.faulted())
            {
              stop_active_play_mode(scriptRuntime.last_error());
            }
          }
          else if (event.type == SDL_MOUSEBUTTONDOWN)
          {
            scriptRuntime.on_mouse_down(
                static_cast<int>(event.button.button),
                static_cast<float>(event.button.x),
                static_cast<float>(event.button.y));
            if (scriptRuntime.faulted())
            {
              stop_active_play_mode(scriptRuntime.last_error());
            }
          }
          else if (event.type == SDL_MOUSEBUTTONUP)
          {
            scriptRuntime.on_mouse_up(
                static_cast<int>(event.button.button),
                static_cast<float>(event.button.x),
                static_cast<float>(event.button.y));
            if (scriptRuntime.faulted())
            {
              stop_active_play_mode(scriptRuntime.last_error());
            }
          }
          else if (event.type == SDL_MOUSEMOTION)
          {
            scriptRuntime.on_mouse_move(
                static_cast<float>(event.motion.x),
                static_cast<float>(event.motion.y));
            if (scriptRuntime.faulted())
            {
              stop_active_play_mode(scriptRuntime.last_error());
            }
          }
        }

        if (event.type == SDL_FINGERDOWN)
        {
          activeTrackpadFingers_.insert(static_cast<std::int64_t>(event.tfinger.fingerId));
        }
        else if (event.type == SDL_FINGERUP)
        {
          activeTrackpadFingers_.erase(static_cast<std::int64_t>(event.tfinger.fingerId));
        }
      }
      editor.state.threeFingerDragActive = (activeTrackpadFingers_.size() >= 3);
    }

    {
      HADES_FRAME_METRIC_SCOPE("vulkan_render_frame");
      renderer->render_frame(window.get());
    }
    {
      HADES_FRAME_METRIC_SCOPE("imgui_begin");
      imgui_session.begin_frame();
    }

    ImGuiIO &io = ImGui::GetIO();
    if (!workspaceManager.has_current_workspace())
    {
      render_workspace_selector();
      wasPlayingLastFrame = false;
    }
    else
    {
      {
        HADES_FRAME_METRIC_SCOPE("editor_render");
        editor.render(io.DeltaTime, workspaceManager.current_workspace()->path, entityManager, componentManager, scriptRuntime);
      }
      if (audioSystem != nullptr)
      {
        audioSystem->set_active_world(editor.state.isPlaying ? editor.state.activeWorld : std::nullopt);
      }
      if (physicsSystem != nullptr)
      {
        physicsSystem->set_active_world(editor.state.isPlaying ? editor.state.activeWorld : std::nullopt);
      }
      if (editor.state.isPlaying)
      {
#ifdef HADES_ENABLE_API
        start_preview_api_if_needed();
#endif
        int requestedApiTicks = 0;
        bool requestedApiReset = false;

#ifdef HADES_ENABLE_API
        if (previewApi_ && previewApi_->is_running())
        {
          const auto pendingInputs = previewApi_->consume_pending_inputs();
          for (const auto &input : pendingInputs)
          {
            if (input.down)
            {
              scriptRuntime.on_key_down(input.keyCode);
            }
            else
            {
              scriptRuntime.on_key_up(input.keyCode);
            }
          }

          if (previewApi_->has_pending_step())
          {
            requestedApiTicks = previewApi_->consume_pending_step();
          }
          if (previewApi_->has_pending_reset())
          {
            previewApi_->consume_pending_reset();
            requestedApiReset = true;
          }
        }
#endif

        scriptRuntime.set_viewport_size(io.DisplaySize.x, io.DisplaySize.y);
        {
          HADES_FRAME_METRIC_SCOPE("script_update");
          scriptRuntime.update(io.DeltaTime, componentManager, entityManager);
        }
        if (scriptRuntime.faulted())
        {
          stop_active_play_mode(scriptRuntime.last_error());
        }
        else
        {
          HADES_FRAME_METRIC_SCOPE("systems_update");
          eventBus.dispatch();
          SystemContext context{componentManager, entityManager, eventBus};
          systemManager.updateSystems(io.DeltaTime, context);

#ifdef HADES_ENABLE_API
          if (requestedApiTicks > 1)
          {
            constexpr int API_MAX_EXTRA_TICKS = 240;
            const int extraTicks = std::min(requestedApiTicks - 1, API_MAX_EXTRA_TICKS);
            constexpr float API_FIXED_DT = 1.0f / 60.0f;
            for (int i = 0; i < extraTicks; ++i)
            {
              scriptRuntime.update(API_FIXED_DT, componentManager, entityManager);
              if (scriptRuntime.faulted())
              {
                stop_active_play_mode(scriptRuntime.last_error());
                break;
              }

              eventBus.dispatch();
              SystemContext extraContext{componentManager, entityManager, eventBus};
              systemManager.updateSystems(API_FIXED_DT, extraContext);
            }
          }

          if (previewApi_ && previewApi_->is_running())
          {
            previewApi_->set_observed_state(scriptRuntime.collect_observations());
            previewApi_->set_entity_state(collect_preview_entity_state_json());
            previewApi_->set_game_over(false);

            if (requestedApiTicks > 0)
            {
              previewApi_->signal_step_complete();
            }
            if (requestedApiReset)
            {
              previewApi_->signal_reset_complete();
            }
          }
#endif
        }

        // Handle pending world load requests from scripts.
        if (editor.state.isPlaying)
        {
          if (auto worldName = ScriptRuntime::consume_pending_world_load(); worldName.has_value())
          {
            const auto &workspacePath = workspaceManager.current_workspace()->path;
            const auto worldFilePath = workspacePath / ".hades" / "worlds" / (*worldName + ".json");

            if (std::filesystem::exists(worldFilePath))
            {
              scriptRuntime.stop();

              if (editor.state.activeWorld.has_value())
              {
                destroy_world_tree(*editor.state.activeWorld, entityManager, componentManager);
              }

              auto newWorld = load_world_from_file(worldFilePath, entityManager, componentManager);
              if (newWorld.has_value())
              {
                editor.state.activeWorld = newWorld;
                auto cameraSelection = select_main_camera(entityManager, componentManager, newWorld);
                if (cameraSelection.status == MainCameraSelectionStatus::Ready && cameraSelection.entity.has_value())
                {
                  editor.state.activeCamera = cameraSelection.entity;
                }

                if (physicsSystem != nullptr)
                {
                  physicsSystem->clear_bodies();
                  physicsSystem->set_active_world(newWorld);
                }
                if (audioSystem != nullptr)
                {
                  audioSystem->set_active_world(newWorld);
                }

                std::string scriptError;
                if (!scriptRuntime.start(componentManager, entityManager, workspacePath, newWorld, &scriptError))
                {
                  stop_active_play_mode("World load failed: " + scriptError);
                }
              }
              else
              {
                stop_active_play_mode("Failed to load world: " + *worldName);
              }
            }
            else
            {
              editor.log_error("World file not found: " + worldFilePath.string());
            }
          }
        }
      }
      else if (wasPlayingLastFrame && audio_engine != nullptr)
      {
        audio_engine->stop_all();
      }

#ifdef HADES_ENABLE_API
      if (!editor.state.isPlaying)
      {
        stop_preview_api();
      }
#endif

      wasPlayingLastFrame = editor.state.isPlaying;
      process_editor_events();
      if (!running)
      {
        return;
      }
    }

    {
      HADES_FRAME_METRIC_SCOPE("imgui_render");
      imgui_session.render();
    }
    {
      HADES_FRAME_METRIC_SCOPE("sync_play_window");
      sync_play_window();
    }

    HADES_FRAME_METRIC_END_FRAME();
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

    if (!imguiIniPath_.empty())
    {
      ImGui::SaveIniSettingsToDisk(imguiIniPath_.c_str());
    }

    if (workspaceManager.current_workspace().has_value())
    {
      std::string errorMessage;
      if (!persist_workspace_state(workspaceManager.current_workspace()->path, &errorMessage))
      {
        Log::warn("failed to save workspace on shutdown: %s", errorMessage.c_str());
      }
    }

    return EXIT_SUCCESS;
  }
}
