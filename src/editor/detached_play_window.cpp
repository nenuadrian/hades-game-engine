#include "detached_play_window.hpp"

#include <SDL.h>

#include "../engine/components/camera_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/rendering/render_types.hpp"
#include "../engine/rendering/vulkan.hpp"

namespace
{
  constexpr char PLAY_WINDOW_TITLE[] = "Hades Play";
  constexpr int PLAY_WINDOW_WIDTH = 1280;
  constexpr int PLAY_WINDOW_HEIGHT = 720;
}

namespace hades
{
  void DetachedPlayWindow::SDLWindowDeleter::operator()(SDL_Window *window) const
  {
    if (window != nullptr)
    {
      SDL_DestroyWindow(window);
    }
  }

  DetachedPlayWindow::DetachedPlayWindow() = default;

  DetachedPlayWindow::~DetachedPlayWindow()
  {
    close();
  }

  bool DetachedPlayWindow::open(std::string *errorMessage)
  {
    if (is_open())
    {
      return true;
    }

    const SDL_WindowFlags windowFlags =
        static_cast<SDL_WindowFlags>(
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    WindowPtr window(SDL_CreateWindow(
        PLAY_WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        PLAY_WINDOW_WIDTH,
        PLAY_WINDOW_HEIGHT,
        windowFlags));
    if (window == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::string("Failed to create play window: ") + SDL_GetError();
      }
      return false;
    }

    auto renderer = std::make_unique<VulkanRenderer>();
    if (!renderer->init(window.get()))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to initialize Vulkan for play window";
      }
      return false;
    }

    window_ = std::move(window);
    renderer_ = std::move(renderer);
    SDL_ShowWindow(window_.get());
    SDL_RaiseWindow(window_.get());
    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return true;
  }

  void DetachedPlayWindow::close()
  {
    renderer_.reset();
    window_.reset();
  }

  bool DetachedPlayWindow::is_open() const
  {
    return window_ != nullptr && renderer_ != nullptr;
  }

  std::optional<std::uint32_t> DetachedPlayWindow::window_id() const
  {
    if (window_ == nullptr)
    {
      return std::nullopt;
    }

    return SDL_GetWindowID(window_.get());
  }

  void DetachedPlayWindow::render(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> world,
      std::optional<Entity::EntityId> activeCamera)
  {
    if (!is_open())
    {
      return;
    }

    if (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_MINIMIZED)
    {
      return;
    }

    renderer_->render_frame(window_.get());

    if (world.has_value() && activeCamera.has_value() &&
        componentManager.hasComponent<CameraComponent>(*activeCamera) &&
        componentManager.hasComponent<PositionComponent3D>(*activeCamera))
    {
      int w = 0;
      int h = 0;
      SDL_GetWindowSize(window_.get(), &w, &h);
      const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;
      const RenderCamera camera = sceneRenderer_.buildCamera(*activeCamera, aspect, componentManager);
      const RenderList list = sceneRenderer_.buildRenderList(
          camera, componentManager, entityManager, world);
      renderer_->render_scene_to_main(list);
    }

    renderer_->present_frame();
  }
}
