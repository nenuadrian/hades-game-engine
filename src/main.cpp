#include <SDL.h>
#include <SDL_opengl.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <CLI/CLI.hpp>
#include "engine/core/ecs/system_manager.hpp"
#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/core/ecs/entity.hpp"
#include <chrono>
#include "engine/core/ecs/constants.h"

void init()
{
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
  {
    std::cerr << "Error: " << SDL_GetError() << std::endl;
    return;
  }

  // Set OpenGL attributes
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

  // Create SDL window with OpenGL context
  SDL_Window *window = SDL_CreateWindow("Hades Game Engine",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        1280, 720,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (window == nullptr)
  {
    std::cerr << "Error: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return;
  }

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Initialize ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  ImGui::StyleColorsDark();

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init("#version 330");

  // Main loop

  EntityManager entityManager;
  ComponentManager componentManager;
  SystemManager systemManager;

  // Register systems
  auto movementSystem = systemManager.registerSystem<MovementSystem>();

  // Create an entity
  Entity::Id entity = entityManager.createEntity();

  // Add components to the entity
  // componentManager.addComponent<Transform>(entity, {Vec3(0, 0, 0)});
  // componentManager.addComponent<Velocity>(entity, {Vec3(1, 0, 0)});

  // Set the entity signature to include both Transform and Velocity
  std::bitset<MAX_COMPONENTS> signature;
  // signature.set(getComponentTypeId<Transform>());
  // signature.set(getComponentTypeId<Velocity>());
  entityManager.setComponentSignature(entity, signature);

  auto previousTime = std::chrono::high_resolution_clock::now();
  bool running = true;
  SDL_Event event;
  while (running)
  {
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
      {
        running = false;
      }
      auto currentTime = std::chrono::high_resolution_clock::now();

      // Calculate delta time (time between frames in seconds)
      std::chrono::duration<float> deltaTime = currentTime - previousTime;
      previousTime = currentTime;

      // Convert delta time to seconds
      float dt = deltaTime.count();
      systemManager.updateSystems(dt, componentManager);
      ImGui_ImplSDL2_ProcessEvent(&event);
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Create ImGui window
    ImGui::Begin("Hello, ImGui!");
    ImGui::Text("This is a cross-platform ImGui window with SDL and OpenGL.");
    ImGui::End();

    // Rendering
    ImGui::Render();
    SDL_GL_MakeCurrent(window, gl_context);
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

int main(int argc, char *argv[])
{
  CLI::App app{"Hades"};

  CLI11_PARSE(app, argc, argv);

  init();

  return 0;
}
