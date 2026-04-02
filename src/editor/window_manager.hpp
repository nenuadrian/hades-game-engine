#ifndef HADES_EDITOR_WINDOW_MANAGER_HPP
#define HADES_EDITOR_WINDOW_MANAGER_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/system_manager.hpp"
#include "../engine/runtime/script_runtime.hpp"
#include "editor.hpp"
#include "workspace_manager.hpp"

struct SDL_Window;

namespace hades
{
  class AudioEngine;
  class Renderer;

  class WindowManager
  {
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
      bool initialized = false;
    };

    class ImGuiSession
    {
    public:
      bool init(SDL_Window *window, Renderer &renderer);
      void begin_frame();
      void render();
      ~ImGuiSession();

    private:
      void shutdown();

      Renderer *renderer_ = nullptr;
      bool initialized = false;
    };

    using WindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

    EntityManager entityManager;
    ComponentManager componentManager;
    SystemManager systemManager;
    ScriptRuntime scriptRuntime;
    WorkspaceManager workspaceManager;
    Editor editor;
    // Keep destruction order: ImGui, audio, renderer, SDL window, then SDL itself.
    SdlSession sdl_session;
    WindowPtr window{nullptr};
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<AudioEngine> audio_engine;
    ImGuiSession imgui_session;
    bool initialized = false;
    bool running = false;
    bool wasPlayingLastFrame = false;
    bool creatingWorkspace = false;
    std::string workspaceStatusMessage;
    std::array<char, 1024> openWorkspacePathBuffer{};
    std::array<char, 1024> createWorkspaceParentBuffer{};
    std::array<char, 256> createWorkspaceNameBuffer{};
    std::vector<std::uint32_t> workspaceLogoPixels;
    int workspaceLogoWidth = 0;
    int workspaceLogoHeight = 0;

    void request_quit();
    void process_editor_events();
    void render_workspace_logo() const;
    void render_workspace_selector();
    void open_workspace(const std::string &workspacePath);
    void create_workspace();
    void update_window_title();
    bool init();
    void render_frame();

  public:
    WindowManager();
    ~WindowManager();

    int run();
  };
}

#endif
