#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h> // printf, fprintf
#include <SDL.h>
#include <SDL_vulkan.h>

#include "../engine/systems/render_system.hpp"
#include "../engine/systems/movement_system.hpp"
#include "../engine/core/ecs/system_manager.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/constants.h"
#include "editor.hpp"
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
    VulkanRenderer renderer;

  public:
    bool running = true;

    int render_frame()
    {
      ImGuiIO &io = ImGui::GetIO();

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
      int fb_width, fb_height;
      SDL_GetWindowSize(window, &fb_width, &fb_height);
      renderer.new_frame(fb_width, fb_height);

      // Start the Dear ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();

      editor.render(io.DeltaTime, entityManager, componentManager);

      // Rendering
      ImGui::Render();
      ImDrawData *draw_data = ImGui::GetDrawData();
      const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
      if (!is_minimized)
      {
        renderer.render_imgui(draw_data);
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

      ImVector<const char *> extensions;
      uint32_t extensions_count = 0;
      SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, nullptr);
      extensions.resize(extensions_count);
      SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extensions.Data);
      renderer.SetupVulkan(extensions);

      // Create Window Surface
      VkSurfaceKHR surface;
      VkResult err;
      if (SDL_Vulkan_CreateSurface(window, renderer.g_Instance, &surface) == 0)
      {
        printf("Failed to create Vulkan surface.\n");
        return 1;
      }

      // Create Framebuffers
      int w, h;
      SDL_GetWindowSize(window, &w, &h);

      renderer.SetupVulkanWindow(surface, w, h);

      // Setup Dear ImGui context
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGuiIO &io = ImGui::GetIO();
      (void)io;
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

      // Setup Dear ImGui style
      ImGui::StyleColorsDark();
      // ImGui::StyleColorsLight();

      // Setup Platform/Renderer backends
      ImGui_ImplSDL2_InitForVulkan(window);
      ImGui_ImplVulkan_InitInfo init_info = {};
      init_info.Instance = renderer.g_Instance;
      init_info.PhysicalDevice = renderer.g_PhysicalDevice;
      init_info.Device = renderer.g_Device;
      init_info.QueueFamily = renderer.g_QueueFamily;
      init_info.Queue = renderer.g_Queue;
      init_info.PipelineCache = renderer.g_PipelineCache;
      init_info.DescriptorPool = renderer.g_DescriptorPool;
      init_info.RenderPass = renderer.g_MainWindowData.RenderPass;
      init_info.Subpass = 0;
      init_info.MinImageCount = renderer.g_MinImageCount;
      init_info.ImageCount = renderer.g_MainWindowData.ImageCount;
      init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
      init_info.Allocator = renderer.g_Allocator;
      init_info.CheckVkResultFn = check_vk_result;
      ImGui_ImplVulkan_Init(&init_info);

      // Register systems
      auto movementSystem = systemManager.registerSystem<MovementSystem>();
      auto renderSystem = systemManager.registerSystem<RenderSystem>();

      return 0;
    }

    int cleanup()
    {
      vkDeviceWaitIdle(renderer.g_Device);
      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();

      renderer.cleanup();

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
