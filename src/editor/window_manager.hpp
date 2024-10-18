#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h>
#include <SDL.h>

#include "../engine/systems/render_system.hpp"
#include "../engine/systems/movement_system.hpp"
#include "../engine/core/ecs/system_manager.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/constants.h"
#include "editor.hpp"
#include "../engine/rendering/renderer.hpp"
#include "../engine/rendering/vulkan.hpp"

namespace hades
{
  class WindowManager
  {
  private:
    SDL_Window *window;
    EntityManager entityManager;
    ComponentManager componentManager;
    SystemManager systemManager;
    Editor editor;
    std::unique_ptr<Renderer> renderer = std::make_unique<VulkanRenderer>();

  public:
    bool running = true;

    int render_frame()
    {
      // Poll and handle events (inputs, window resize, etc.)
      // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
      // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
      // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
      // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
          running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
          running = false;
      }
      if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
      {
        SDL_Delay(10);
        return 10;
      }

      renderer.get()->render_frame(window);

      // Start the Dear ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();

      ImGuiIO &io = ImGui::GetIO();

      editor.render(io.DeltaTime, entityManager, componentManager);

      // Rendering
      ImGui::Render();
      ImDrawData *draw_data = ImGui::GetDrawData();
      const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
      if (!is_minimized)
      {
        renderer.get()->render_imgui(draw_data);
      }
      return 0;
    }

    int init()
    {
      // Setup SDL
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
      {
        printf("Error: %s\n", SDL_GetError());
        return -1;
      }

      // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
      SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

      // Create window with Vulkan graphics context
      SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
      window = SDL_CreateWindow("Dear ImGui SDL2+Vulkan example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
      if (window == nullptr)
      {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
      }

      renderer.get()->init(window);



      // Register systems
      auto movementSystem = systemManager.registerSystem<MovementSystem>();
      auto renderSystem = systemManager.registerSystem<RenderSystem>();

      return 0;
    }

    int cleanup()
    {
      // vkDeviceWaitIdle(renderer.g_Device);
      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();

      renderer.get()->cleanup();

      SDL_DestroyWindow(window);
      SDL_Quit();
      return 0;
    }

    static void check_vk_result(VkResult err)
    {
      if (err == 0)
        return;
      fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
      if (err < 0)
        abort();
    }
  };
}
