#ifndef HADES_EDITOR_DETACHED_PLAY_WINDOW_HPP
#define HADES_EDITOR_DETACHED_PLAY_WINDOW_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "../engine/core/ecs/entity.hpp"

struct SDL_Renderer;
struct SDL_Window;

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class DetachedPlayWindow
  {
  public:
    DetachedPlayWindow() = default;
    ~DetachedPlayWindow();

    DetachedPlayWindow(const DetachedPlayWindow &) = delete;
    DetachedPlayWindow &operator=(const DetachedPlayWindow &) = delete;

    bool open(std::string *errorMessage = nullptr);
    void close();
    bool is_open() const;
    std::optional<std::uint32_t> window_id() const;
    void render(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> world,
        std::optional<Entity::EntityId> activeCamera);

  private:
    struct SDLWindowDeleter
    {
      void operator()(SDL_Window *window) const;
    };

    struct SDLRendererDeleter
    {
      void operator()(SDL_Renderer *renderer) const;
    };

    using WindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;
    using RendererPtr = std::unique_ptr<SDL_Renderer, SDLRendererDeleter>;

    WindowPtr window_{nullptr};
    RendererPtr renderer_{nullptr};
  };
}

#endif
