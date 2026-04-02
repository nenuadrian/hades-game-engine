#include "window_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#else
#include <sys/wait.h>
#endif

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

  std::string trim_copy(const std::string &value)
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

    return value.substr(first, last - first);
  }

  template <std::size_t Size>
  void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
  {
    buffer.fill('\0');
    const std::size_t copyLength = std::min(value.size(), Size - 1);
    std::copy_n(value.data(), copyLength, buffer.data());
    buffer[copyLength] = '\0';
  }

#ifdef _WIN32
  std::optional<std::filesystem::path> pick_folder_with_native_dialog(std::string *errorMessage)
  {
    HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Unable to initialize the Windows folder picker.";
      }
      return std::nullopt;
    }

    IFileDialog *dialog = nullptr;
    const HRESULT createResult = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(createResult) || dialog == nullptr)
    {
      if (shouldUninitialize)
      {
        CoUninitialize();
      }
      if (errorMessage != nullptr)
      {
        *errorMessage = "Unable to create the Windows folder picker dialog.";
      }
      return std::nullopt;
    }

    DWORD dialogOptions = 0;
    dialog->GetOptions(&dialogOptions);
    dialog->SetOptions(dialogOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    const HRESULT showResult = dialog->Show(nullptr);
    if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
      dialog->Release();
      if (shouldUninitialize)
      {
        CoUninitialize();
      }
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return std::nullopt;
    }

    if (FAILED(showResult))
    {
      dialog->Release();
      if (shouldUninitialize)
      {
        CoUninitialize();
      }
      if (errorMessage != nullptr)
      {
        *errorMessage = "The Windows folder picker failed to open.";
      }
      return std::nullopt;
    }

    IShellItem *item = nullptr;
    const HRESULT resultItemStatus = dialog->GetResult(&item);
    dialog->Release();
    if (FAILED(resultItemStatus) || item == nullptr)
    {
      if (shouldUninitialize)
      {
        CoUninitialize();
      }
      if (errorMessage != nullptr)
      {
        *errorMessage = "The Windows folder picker did not return a folder.";
      }
      return std::nullopt;
    }

    PWSTR rawPath = nullptr;
    const HRESULT pathStatus = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    item->Release();

    std::optional<std::filesystem::path> selectedPath;
    if (SUCCEEDED(pathStatus) && rawPath != nullptr)
    {
      selectedPath = std::filesystem::path(rawPath);
      CoTaskMemFree(rawPath);
    }

    if (shouldUninitialize)
    {
      CoUninitialize();
    }

    if (!selectedPath.has_value())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "The Windows folder picker did not return a valid filesystem path.";
      }
      return std::nullopt;
    }

    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return selectedPath;
  }
#else
  std::optional<std::string> capture_command_output(const std::string &command, int *exitCode = nullptr)
  {
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
      if (exitCode != nullptr)
      {
        *exitCode = -1;
      }
      return std::nullopt;
    }

    std::string output;
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
      output += buffer.data();
    }

    const int commandStatus = pclose(pipe);
    int commandExitCode = commandStatus;
#if defined(WIFEXITED) && defined(WEXITSTATUS)
    if (commandStatus >= 0 && WIFEXITED(commandStatus))
    {
      commandExitCode = WEXITSTATUS(commandStatus);
    }
#endif
    if (exitCode != nullptr)
    {
      *exitCode = commandExitCode;
    }

    return output;
  }

  std::optional<std::filesystem::path> pick_folder_with_native_dialog(std::string *errorMessage)
  {
#ifdef __APPLE__
    int exitCode = 0;
    const auto output = capture_command_output(
        "osascript -e 'POSIX path of (choose folder with prompt \"Select a workspace folder\")'",
        &exitCode);
    if (!output.has_value())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Unable to launch the macOS folder picker.";
      }
      return std::nullopt;
    }

    const std::string trimmedOutput = trim_copy(*output);
    if (exitCode != 0 || trimmedOutput.empty())
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return std::nullopt;
    }

    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return std::filesystem::path(trimmedOutput);
#elif defined(__linux__)
    int exitCode = 0;
    const auto output = capture_command_output(
        "sh -c 'if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --directory --title=\"Select a workspace folder\"; "
        "elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getexistingdirectory; "
        "else exit 127; fi'",
        &exitCode);
    if (exitCode == 127)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No native folder picker is available. Enter a folder path manually.";
      }
      return std::nullopt;
    }

    if (!output.has_value())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Unable to launch the native folder picker.";
      }
      return std::nullopt;
    }

    const std::string trimmedOutput = trim_copy(*output);
    if (exitCode != 0 || trimmedOutput.empty())
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return std::nullopt;
    }

    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return std::filesystem::path(trimmedOutput);
#else
    if (errorMessage != nullptr)
    {
      *errorMessage = "This platform does not provide a native folder picker in this build.";
    }
    return std::nullopt;
#endif
  }
#endif

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
      std::fprintf(stderr, "Warning: failed to convert logo surface for workspace preview: %s\n", SDL_GetError());
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
      std::fprintf(stderr, "Warning: failed to allocate logo preview surface: %s\n", SDL_GetError());
      width = 0;
      height = 0;
      return;
    }

    if (SDL_BlitScaled(rgbaSurface.get(), nullptr, scaledSurface.get(), nullptr) != 0)
    {
      std::fprintf(stderr, "Warning: failed to scale logo preview surface: %s\n", SDL_GetError());
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
    const ImVec2 cardPadding(12.0f, 12.0f);
    const ImVec2 cardMin(origin.x - cardPadding.x, origin.y - cardPadding.y);
    const ImVec2 cardMax(origin.x + logoSize.x + cardPadding.x, origin.y + logoSize.y + cardPadding.y);
    drawList->AddRectFilled(cardMin, cardMax, IM_COL32(18, 14, 14, 255), 10.0f);
    drawList->AddRect(cardMin, cardMax, IM_COL32(62, 53, 52, 255), 10.0f);

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

    ImGui::Dummy(ImVec2(logoSize.x, logoSize.y + cardPadding.y));
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

    const float panelWidth = std::min(ImGui::GetContentRegionAvail().x, 760.0f);
    const float leftMargin = std::max((ImGui::GetWindowSize().x - panelWidth) * 0.5f, 24.0f);
    ImGui::SetCursorPosX(leftMargin);
    ImGui::SetCursorPosY(28.0f);

    ImGui::BeginChild("Workspace Selector Panel", ImVec2(panelWidth, 0.0f), true);
    render_workspace_logo();
    if (workspaceLogoWidth > 0)
    {
      ImGui::Spacing();
    }
    ImGui::TextUnformatted(creatingWorkspace ? "Create a Workspace" : "Select a Workspace");
    ImGui::Spacing();

    if (creatingWorkspace)
    {
      ImGui::TextWrapped("Choose the parent folder and name for the new empty workspace.");
    }
    else
    {
      ImGui::TextWrapped("Open a recent workspace folder, browse to an existing folder, or create a new empty workspace.");
    }

    if (!workspaceStatusMessage.empty())
    {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.62f, 0.56f, 1.00f));
      ImGui::TextWrapped("%s", workspaceStatusMessage.c_str());
      ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (creatingWorkspace)
    {
      ImGui::InputText("Workspace Name", createWorkspaceNameBuffer.data(), createWorkspaceNameBuffer.size());
      ImGui::InputText("Location", createWorkspaceParentBuffer.data(), createWorkspaceParentBuffer.size());

      if (ImGui::Button("Browse Location..."))
      {
        std::string pickerError;
        const auto pickedFolder = pick_folder_with_native_dialog(&pickerError);
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

      ImGui::SameLine();
      if (ImGui::Button("Back"))
      {
        creatingWorkspace = false;
        workspaceStatusMessage.clear();
      }

      const std::string workspaceName = trim_copy(createWorkspaceNameBuffer.data());
      const std::string parentDirectory = trim_copy(createWorkspaceParentBuffer.data());
      if (!workspaceName.empty() && !parentDirectory.empty())
      {
        ImGui::Spacing();
        ImGui::TextDisabled("Workspace folder");
        const std::string targetPath = (std::filesystem::path(parentDirectory) / workspaceName).string();
        ImGui::PushTextWrapPos();
        ImGui::Text("%s", targetPath.c_str());
        ImGui::PopTextWrapPos();
      }

      const bool canCreateWorkspace = !workspaceName.empty() && !parentDirectory.empty();
      if (!canCreateWorkspace)
      {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Create Workspace", ImVec2(180.0f, 0.0f)))
      {
        create_workspace();
      }
      if (!canCreateWorkspace)
      {
        ImGui::EndDisabled();
      }
    }
    else
    {
      ImGui::InputText("Workspace Folder", openWorkspacePathBuffer.data(), openWorkspacePathBuffer.size());

      if (ImGui::Button("Browse Existing..."))
      {
        std::string pickerError;
        const auto pickedFolder = pick_folder_with_native_dialog(&pickerError);
        if (pickedFolder.has_value())
        {
          set_buffer_text(openWorkspacePathBuffer, pickedFolder->string());
          workspaceStatusMessage.clear();
        }
        else if (!pickerError.empty())
        {
          workspaceStatusMessage = pickerError;
        }
      }

      const bool hasOpenPath = !trim_copy(openWorkspacePathBuffer.data()).empty();
      ImGui::SameLine();
      if (!hasOpenPath)
      {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Open Workspace"))
      {
        open_workspace(openWorkspacePathBuffer.data());
      }
      if (!hasOpenPath)
      {
        ImGui::EndDisabled();
      }

      ImGui::SameLine();
      if (ImGui::Button("Create New Workspace"))
      {
        creatingWorkspace = true;
        workspaceStatusMessage.clear();
        if (createWorkspaceParentBuffer[0] == '\0')
        {
          std::error_code errorCode;
          set_buffer_text(createWorkspaceParentBuffer, std::filesystem::current_path(errorCode).string());
        }
      }

      ImGui::Spacing();
      ImGui::TextUnformatted("Recent Workspaces");
      ImGui::Separator();

      if (workspaceManager.recent_workspaces().empty())
      {
        ImGui::Spacing();
        ImGui::TextDisabled("No recent workspaces yet.");
      }
      else
      {
        ImGui::BeginChild("Recent Workspaces List", ImVec2(0.0f, 0.0f), false);
        for (std::size_t index = 0; index < workspaceManager.recent_workspaces().size(); ++index)
        {
          const WorkspaceEntry &workspace = workspaceManager.recent_workspaces()[index];
          ImGui::PushID(static_cast<int>(index));

          if (ImGui::Button(workspace.name.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
          {
            open_workspace(workspace.path.string());
          }

          ImGui::PushTextWrapPos();
          ImGui::TextDisabled("%s", workspace.path.string().c_str());
          ImGui::PopTextWrapPos();

          if (index + 1 < workspaceManager.recent_workspaces().size())
          {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
          }

          ImGui::PopID();
        }
        ImGui::EndChild();
      }
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  void WindowManager::open_workspace(const std::string &workspacePath)
  {
    std::string errorMessage;
    const auto workspace = workspaceManager.open_workspace(std::filesystem::path(trim_copy(workspacePath)), &errorMessage);
    if (!workspace.has_value())
    {
      workspaceStatusMessage = errorMessage.empty() ? "Unable to open the selected workspace folder." : errorMessage;
      return;
    }

    creatingWorkspace = false;
    workspaceStatusMessage = errorMessage;
    set_buffer_text(openWorkspacePathBuffer, workspace->path.string());
    set_buffer_text(createWorkspaceParentBuffer, workspace->path.parent_path().string());
    update_window_title();
  }

  void WindowManager::create_workspace()
  {
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

    creatingWorkspace = false;
    workspaceStatusMessage = errorMessage;
    set_buffer_text(openWorkspacePathBuffer, workspace->path.string());
    set_buffer_text(createWorkspaceParentBuffer, workspace->path.parent_path().string());
    set_buffer_text(createWorkspaceNameBuffer, std::string());
    update_window_title();
  }

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
    update_window_title();
    SDL_ShowWindow(window.get());
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
    if (!workspaceManager.has_current_workspace())
    {
      render_workspace_selector();
      wasPlayingLastFrame = false;
    }
    else
    {
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
