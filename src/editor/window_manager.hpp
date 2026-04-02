#ifndef HADES_EDITOR_WINDOW_MANAGER_HPP
#define HADES_EDITOR_WINDOW_MANAGER_HPP

#include <cstdint>
#include <memory>

#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/system_manager.hpp"
#include "editor.hpp"

struct SDL_Window;

namespace hades
{
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
    Editor editor;
    // Keep destruction order: ImGui, then renderer, then SDL window, then SDL itself.
    SdlSession sdl_session;
    WindowPtr window{nullptr};
    std::unique_ptr<Renderer> renderer;
    ImGuiSession imgui_session;
    bool initialized = false;
    bool running = false;

    void request_quit();
    void process_editor_events();
    bool init();
    void render_frame();

  public:
    WindowManager();
    ~WindowManager();

    int run();
  };
}

#endif
