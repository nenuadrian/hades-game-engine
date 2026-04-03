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
#include "detached_play_window.hpp"
#include "editor.hpp"
#include "workspace_manager.hpp"

struct SDL_Window;
union SDL_Event;
struct ImGuiContext;

namespace hades
{
  class AudioEngine;
  class AudioSystem;
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
      bool init(SDL_Window *window, Renderer &renderer, bool enableViewports = true);
      void process_event(const SDL_Event &event);
      void begin_frame();
      void render();
      void close();
      ~ImGuiSession();

    private:
      void shutdown();

      ImGuiContext *context_ = nullptr;
      Renderer *renderer_ = nullptr;
      bool enableViewports_ = false;
      bool initialized = false;
    };

    using WindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

    class DetachedScriptEditorWindow
    {
    public:
      DetachedScriptEditorWindow() = default;
      ~DetachedScriptEditorWindow();

      DetachedScriptEditorWindow(const DetachedScriptEditorWindow &) = delete;
      DetachedScriptEditorWindow &operator=(const DetachedScriptEditorWindow &) = delete;

      bool open(std::string *errorMessage = nullptr);
      void close();
      bool is_open() const;
      std::optional<std::uint32_t> window_id() const;
      void process_event(const SDL_Event &event);
      void render(Editor &editor);

    private:
      void destroy();

      WindowPtr window_{nullptr};
      std::unique_ptr<Renderer> renderer_;
      ImGuiSession imgui_session_;
      bool visible_ = false;
    };

    EntityManager entityManager;
    ComponentManager componentManager;
    SystemManager systemManager;
    ScriptRuntime scriptRuntime;
    WorkspaceManager workspaceManager;
    Editor editor;
    // Keep destruction order: ImGui, audio, renderer, SDL window, then SDL itself.
    SdlSession sdl_session;
    WindowPtr window{nullptr};
    DetachedPlayWindow playWindow;
    DetachedScriptEditorWindow scriptEditorWindow;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<AudioEngine> audio_engine;
    std::shared_ptr<AudioSystem> audioSystem;
    ImGuiSession imgui_session;
    bool initialized = false;
    bool running = false;
    bool wasPlayingLastFrame = false;
    bool creatingWorkspace = false;
    std::string workspaceStatusMessage;
    std::array<char, 1024> createWorkspaceParentBuffer{};
    std::array<char, 256> createWorkspaceNameBuffer{};
    std::vector<std::uint32_t> workspaceLogoPixels;
    int workspaceLogoWidth = 0;
    int workspaceLogoHeight = 0;
    std::string imguiIniPath_;

    void request_quit();
    void process_editor_events();
    void render_workspace_logo() const;
    void render_workspace_selector();
    void open_workspace(const std::string &workspacePath);
    void create_workspace();
    void reset_workspace_session();
    void stop_active_play_mode(const std::string &message = std::string());
    void sync_play_window();
    void sync_script_editor_window();
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
