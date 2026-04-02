#ifndef HADES_EDITOR_WINDOW_MANAGER_HPP
#define HADES_EDITOR_WINDOW_MANAGER_HPP

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/system_manager.hpp"
#include "../engine/rendering/renderer.hpp"
#include "../engine/rendering/vulkan.hpp"
#include "../engine/systems/movement_system.hpp"
#include "../engine/systems/render_system.hpp"
#include "editor.hpp"

namespace hades
{
  class WindowManager
  {
  private:
    struct SDLWindowDeleter
    {
      void operator()(SDL_Window *window) const
      {
        if (window != nullptr)
          SDL_DestroyWindow(window);
      }
    };

    class SdlSession
    {
    public:
      bool init(Uint32 flags)
      {
        if (initialized)
          return true;

        if (SDL_Init(flags) != 0)
        {
          std::fprintf(stderr, "Error: %s\n", SDL_GetError());
          return false;
        }

        initialized = true;

        // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
        SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
        return true;
      }

      ~SdlSession()
      {
        if (initialized)
          SDL_Quit();
      }

    private:
      bool initialized = false;
    };

    class ImGuiSession
    {
    public:
      bool init(SDL_Window *window, Renderer &renderer)
      {
        if (initialized)
          return true;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

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

      void begin_frame()
      {
        if (!initialized)
          return;

        renderer_->start_imgui_frame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
      }

      void render()
      {
        if (!initialized)
          return;

        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
          renderer_->render_imgui(draw_data);
      }

      ~ImGuiSession()
      {
        shutdown();
      }

    private:
      void shutdown()
      {
        if (!initialized)
          return;

        renderer_->shutdown_imgui_backend();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        renderer_ = nullptr;
        initialized = false;
      }

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
    std::unique_ptr<Renderer> renderer = std::make_unique<VulkanRenderer>();
    ImGuiSession imgui_session;
    bool initialized = false;
    bool running = false;

    void request_quit()
    {
      running = false;

      SDL_Event quit_event{};
      quit_event.type = SDL_QUIT;
      SDL_PushEvent(&quit_event);
    }

    void process_editor_events()
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

    bool init()
    {
      if (initialized)
        return true;

      if (!sdl_session.init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
        return false;

      // Create window with Vulkan graphics context
      SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
      window.reset(SDL_CreateWindow("Dear ImGui SDL2+Vulkan example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags));
      if (window == nullptr)
      {
        std::fprintf(stderr, "Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
      }

      if (!renderer->init(window.get()))
        return false;

      if (!imgui_session.init(window.get(), *renderer))
        return false;

      systemManager.registerSystem<MovementSystem>();
      systemManager.registerSystem<RenderSystem>();
      initialized = true;
      return true;
    }

    void render_frame()
    {
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
          running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window.get()))
          running = false;
      }

      if (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)
      {
        SDL_Delay(10);
        return;
      }

      renderer->render_frame(window.get());
      imgui_session.begin_frame();

      ImGuiIO &io = ImGui::GetIO();
      editor.render(io.DeltaTime, entityManager, componentManager);
      process_editor_events();
      if (!running)
        return;
      imgui_session.render();
    }

  public:
    int run()
    {
      if (!init())
        return EXIT_FAILURE;

      running = true;
      while (running)
      {
        render_frame();
      }

      return EXIT_SUCCESS;
    }
  };
}

#endif
