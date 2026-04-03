#include "detached_play_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL.h>

#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/rendering/model_preview.hpp"
#include "../engine/rendering/vector_text.hpp"

namespace
{
  constexpr char PLAY_WINDOW_TITLE[] = "Hades Play";
  constexpr int PLAY_WINDOW_WIDTH = 1280;
  constexpr int PLAY_WINDOW_HEIGHT = 720;
  constexpr float PI = 3.14159265358979323846f;
  constexpr float CUBE_HALF_EXTENT = 0.5f;
  constexpr int BOX_EDGES[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  struct Vec3
  {
    float x;
    float y;
    float z;
  };

  Vec3 make_vec3(float x, float y, float z)
  {
    return Vec3{x, y, z};
  }

  Vec3 make_vec3(const hades::PositionComponent3D &position)
  {
    return make_vec3(position.x, position.y, position.z);
  }

  Vec3 add_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return make_vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
  }

  Vec3 subtract_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return make_vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
  }

  Vec3 lerp_vec3(const Vec3 &start, const Vec3 &end, float t)
  {
    return make_vec3(
        start.x + ((end.x - start.x) * t),
        start.y + ((end.y - start.y) * t),
        start.z + ((end.z - start.z) * t));
  }

  float degrees_to_radians(float degrees)
  {
    return degrees * (PI / 180.0f);
  }

  bool clip_segment_to_camera_depth(
      Vec3 &start,
      Vec3 &end,
      const hades::CameraComponent &camera)
  {
    auto clip_endpoint = [](Vec3 &point, float &depth, const Vec3 &otherPoint, float otherDepth, float targetDepth)
    {
      const float depthDelta = otherDepth - depth;
      if (std::abs(depthDelta) <= 1e-5f)
      {
        return false;
      }

      const float t = (targetDepth - depth) / depthDelta;
      point = lerp_vec3(point, otherPoint, t);
      depth = targetDepth;
      return true;
    };

    float startDepth = start.z;
    float endDepth = end.z;
    if ((startDepth < camera.nearClip && endDepth < camera.nearClip) ||
        (startDepth > camera.farClip && endDepth > camera.farClip))
    {
      return false;
    }

    if (startDepth < camera.nearClip &&
        !clip_endpoint(start, startDepth, end, endDepth, camera.nearClip))
    {
      return false;
    }
    else if (startDepth > camera.farClip &&
             !clip_endpoint(start, startDepth, end, endDepth, camera.farClip))
    {
      return false;
    }

    if (endDepth < camera.nearClip &&
        !clip_endpoint(end, endDepth, start, startDepth, camera.nearClip))
    {
      return false;
    }
    else if (endDepth > camera.farClip &&
             !clip_endpoint(end, endDepth, start, startDepth, camera.farClip))
    {
      return false;
    }

    return true;
  }

  bool project_camera_space_point(
      const Vec3 &cameraSpacePoint,
      const hades::CameraComponent &camera,
      int width,
      int height,
      SDL_FPoint &screenPoint)
  {
    if (width <= 0 || height <= 0 || camera.fovY <= 0.0f)
    {
      return false;
    }

    if (cameraSpacePoint.z <= camera.nearClip || cameraSpacePoint.z >= camera.farClip)
    {
      return false;
    }

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    const float halfFovRadians = degrees_to_radians(camera.fovY * 0.5f);
    const float tanHalfFov = std::tan(halfFovRadians);
    if (tanHalfFov <= 0.0f)
    {
      return false;
    }

    const float normalizedX = cameraSpacePoint.x / (cameraSpacePoint.z * tanHalfFov * aspectRatio);
    const float normalizedY = cameraSpacePoint.y / (cameraSpacePoint.z * tanHalfFov);
    if (std::abs(normalizedX) > 10.0f || std::abs(normalizedY) > 10.0f)
    {
      return false;
    }

    screenPoint.x = ((normalizedX + 1.0f) * 0.5f) * static_cast<float>(width);
    screenPoint.y = ((1.0f - normalizedY) * 0.5f) * static_cast<float>(height);
    return true;
  }

  bool project_segment(
      const Vec3 &worldStart,
      const Vec3 &worldEnd,
      const hades::PositionComponent3D &cameraPosition,
      const hades::CameraComponent &camera,
      int width,
      int height,
      SDL_FPoint &screenStart,
      SDL_FPoint &screenEnd)
  {
    Vec3 clippedStart = subtract_vec3(worldStart, make_vec3(cameraPosition));
    Vec3 clippedEnd = subtract_vec3(worldEnd, make_vec3(cameraPosition));
    if (!clip_segment_to_camera_depth(clippedStart, clippedEnd, camera))
    {
      return false;
    }

    return project_camera_space_point(clippedStart, camera, width, height, screenStart) &&
           project_camera_space_point(clippedEnd, camera, width, height, screenEnd);
  }

  bool draw_wire_box(
      SDL_Renderer *renderer,
      const hades::PositionComponent3D &cameraPosition,
      const hades::CameraComponent &camera,
      int width,
      int height,
      const hades::PositionComponent3D &position,
      const Vec3 &minCorner,
      const Vec3 &maxCorner,
      const SDL_Color &color)
  {
    Vec3 corners[8] = {
        add_vec3(make_vec3(position), make_vec3(minCorner.x, minCorner.y, minCorner.z)),
        add_vec3(make_vec3(position), make_vec3(maxCorner.x, minCorner.y, minCorner.z)),
        add_vec3(make_vec3(position), make_vec3(maxCorner.x, maxCorner.y, minCorner.z)),
        add_vec3(make_vec3(position), make_vec3(minCorner.x, maxCorner.y, minCorner.z)),
        add_vec3(make_vec3(position), make_vec3(minCorner.x, minCorner.y, maxCorner.z)),
        add_vec3(make_vec3(position), make_vec3(maxCorner.x, minCorner.y, maxCorner.z)),
        add_vec3(make_vec3(position), make_vec3(maxCorner.x, maxCorner.y, maxCorner.z)),
        add_vec3(make_vec3(position), make_vec3(minCorner.x, maxCorner.y, maxCorner.z)),
    };

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    bool visible = false;
    for (const auto &edge : BOX_EDGES)
    {
      SDL_FPoint screenStart{};
      SDL_FPoint screenEnd{};
      if (!project_segment(
              corners[edge[0]],
              corners[edge[1]],
              cameraPosition,
              camera,
              width,
              height,
              screenStart,
              screenEnd))
      {
        continue;
      }

      visible = true;
      SDL_RenderDrawLine(
          renderer,
          static_cast<int>(std::lround(screenStart.x)),
          static_cast<int>(std::lround(screenStart.y)),
          static_cast<int>(std::lround(screenEnd.x)),
          static_cast<int>(std::lround(screenEnd.y)));
    }

    return visible;
  }

  SDL_Color shaded_preview_color(
      std::uint8_t red,
      std::uint8_t green,
      std::uint8_t blue,
      float shade,
      std::uint8_t alpha)
  {
    return SDL_Color{
        hades::preview::scale_color_channel(red, shade),
        hades::preview::scale_color_channel(green, shade),
        hades::preview::scale_color_channel(blue, shade),
        alpha};
  }

  bool draw_model_mesh(
      SDL_Renderer *renderer,
      const hades::PositionComponent3D &cameraPosition,
      const hades::CameraComponent &camera,
      int width,
      int height,
      const hades::PositionComponent3D &position,
      const hades::ImportedModel &model)
  {
    const auto projectedTriangles = hades::preview::project_model_triangles(
        model,
        position,
        [&](const hades::preview::Vec3 &worldPoint)
        {
          return hades::preview::make_vec3(
              worldPoint.x - cameraPosition.x,
              worldPoint.y - cameraPosition.y,
              worldPoint.z - cameraPosition.z);
        },
        [&](const hades::preview::Vec3 &cameraPoint, hades::preview::Vec2 &screenPoint)
        {
          SDL_FPoint projectedPoint{};
          if (!project_camera_space_point(
                  make_vec3(cameraPoint.x, cameraPoint.y, cameraPoint.z),
                  camera,
                  width,
                  height,
                  projectedPoint))
          {
            return false;
          }

          screenPoint.x = projectedPoint.x;
          screenPoint.y = projectedPoint.y;
          return true;
        });

    if (projectedTriangles.empty())
    {
      return false;
    }

    bool drewTriangle = false;
    for (const auto &triangle : projectedTriangles)
    {
      const SDL_Color fillColor = shaded_preview_color(157, 172, 191, triangle.shade, 220);
      const SDL_Color outlineColor = shaded_preview_color(226, 232, 238, std::min(triangle.shade + 0.1f, 1.0f), 255);
      const SDL_Vertex vertices[3] = {
          {
              SDL_FPoint{triangle.points[0].x, triangle.points[0].y},
              fillColor,
              SDL_FPoint{0.0f, 0.0f},
          },
          {
              SDL_FPoint{triangle.points[1].x, triangle.points[1].y},
              fillColor,
              SDL_FPoint{0.0f, 0.0f},
          },
          {
              SDL_FPoint{triangle.points[2].x, triangle.points[2].y},
              fillColor,
              SDL_FPoint{0.0f, 0.0f},
          }};

      if (SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0) == 0)
      {
        drewTriangle = true;
      }

      SDL_SetRenderDrawColor(renderer, outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a);
      SDL_RenderDrawLine(
          renderer,
          static_cast<int>(std::lround(triangle.points[0].x)),
          static_cast<int>(std::lround(triangle.points[0].y)),
          static_cast<int>(std::lround(triangle.points[1].x)),
          static_cast<int>(std::lround(triangle.points[1].y)));
      SDL_RenderDrawLine(
          renderer,
          static_cast<int>(std::lround(triangle.points[1].x)),
          static_cast<int>(std::lround(triangle.points[1].y)),
          static_cast<int>(std::lround(triangle.points[2].x)),
          static_cast<int>(std::lround(triangle.points[2].y)));
      SDL_RenderDrawLine(
          renderer,
          static_cast<int>(std::lround(triangle.points[2].x)),
          static_cast<int>(std::lround(triangle.points[2].y)),
          static_cast<int>(std::lround(triangle.points[0].x)),
          static_cast<int>(std::lround(triangle.points[0].y)));
      drewTriangle = true;
    }

    return drewTriangle;
  }

  bool draw_vector_text(
      SDL_Renderer *renderer,
      const hades::PositionComponent3D &cameraPosition,
      const hades::CameraComponent &camera,
      int width,
      int height,
      const hades::PositionComponent3D &position,
      const hades::TextComponent &text,
      const SDL_Color &color)
  {
    const hades::VectorTextGeometry3D geometry = hades::build_vector_text_geometry(
        text.content,
        hades::VectorTextStyle{
            std::max(0.05f, text.fontSize),
            std::max(0.0f, text.wrapWidth),
            std::max(0.8f, text.lineSpacing),
        },
        hades::make_vector_text_frame_from_euler(
            hades::VectorTextPoint3D{position.x, position.y, position.z},
            text.yawDegrees,
            text.pitchDegrees,
            text.rollDegrees));

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    bool visible = false;
    for (const auto &segment : geometry.segments)
    {
      SDL_FPoint screenStart{};
      SDL_FPoint screenEnd{};
      if (!project_segment(
              make_vec3(segment.start.x, segment.start.y, segment.start.z),
              make_vec3(segment.end.x, segment.end.y, segment.end.z),
              cameraPosition,
              camera,
              width,
              height,
              screenStart,
              screenEnd))
      {
        continue;
      }

      SDL_RenderDrawLine(
          renderer,
          static_cast<int>(std::lround(screenStart.x)),
          static_cast<int>(std::lround(screenStart.y)),
          static_cast<int>(std::lround(screenEnd.x)),
          static_cast<int>(std::lround(screenEnd.y)));
      visible = true;
    }

    return visible;
  }

  void render_world_preview(
      SDL_Renderer *renderer,
      hades::EntityManager &entityManager,
      hades::ComponentManager &componentManager,
      std::optional<hades::Entity::EntityId> world,
      hades::Entity::EntityId activeCamera,
      const hades::PositionComponent3D &cameraPosition,
      const hades::CameraComponent &camera,
      int width,
      int height)
  {
    const SDL_Color primitiveColor{223, 228, 235, 255};
    const SDL_Color modelColor{179, 189, 202, 255};
    const SDL_Color textColor{227, 233, 240, 255};

    for (hades::Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (entity == activeCamera)
      {
        continue;
      }

      if (world.has_value() && !hades::entity_belongs_to_world(entity, *world, componentManager))
      {
        continue;
      }

      if (!componentManager.hasComponent<hades::PositionComponent3D>(entity))
      {
        continue;
      }

      const auto &position = componentManager.getComponent<hades::PositionComponent3D>(entity);
      if (componentManager.hasComponent<hades::PrimitiveComponent>(entity))
      {
        const auto &primitive = componentManager.getComponent<hades::PrimitiveComponent>(entity);
        if (primitive.type == hades::PrimitiveType::Cube)
        {
          draw_wire_box(
              renderer,
              cameraPosition,
              camera,
              width,
              height,
              position,
              make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT),
              make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT),
              primitiveColor);
        }
      }

      if (componentManager.hasComponent<hades::ModelComponent>(entity))
      {
        const auto &model = componentManager.getComponent<hades::ModelComponent>(entity).model;
        if (hades::preview::has_renderable_geometry(model) &&
            draw_model_mesh(
                renderer,
                cameraPosition,
                camera,
                width,
                height,
                position,
                model))
        {
          continue;
        }

        const Vec3 minCorner = model.hasBounds
                                   ? make_vec3(model.minX, model.minY, model.minZ)
                                   : make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT);
        const Vec3 maxCorner = model.hasBounds
                                   ? make_vec3(model.maxX, model.maxY, model.maxZ)
                                   : make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT);
        draw_wire_box(
            renderer,
            cameraPosition,
            camera,
            width,
            height,
            position,
            minCorner,
            maxCorner,
            modelColor);
      }

      if (componentManager.hasComponent<hades::TextComponent>(entity))
      {
        draw_vector_text(
            renderer,
            cameraPosition,
            camera,
            width,
            height,
            position,
            componentManager.getComponent<hades::TextComponent>(entity),
            textColor);
      }
    }
  }
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

  void DetachedPlayWindow::SDLRendererDeleter::operator()(SDL_Renderer *renderer) const
  {
    if (renderer != nullptr)
    {
      SDL_DestroyRenderer(renderer);
    }
  }

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
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
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

    RendererPtr renderer(SDL_CreateRenderer(
        window.get(),
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (renderer == nullptr)
    {
      renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
    }

    if (renderer == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::string("Failed to create play window renderer: ") + SDL_GetError();
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

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(renderer_.get(), &width, &height);
    if (width <= 0 || height <= 0)
    {
      return;
    }

    SDL_SetRenderDrawBlendMode(renderer_.get(), SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_.get(), 17, 20, 24, 255);
    SDL_RenderClear(renderer_.get());

    if (!world.has_value() || !activeCamera.has_value() ||
        !componentManager.hasComponent<CameraComponent>(*activeCamera) ||
        !componentManager.hasComponent<PositionComponent3D>(*activeCamera))
    {
      SDL_RenderPresent(renderer_.get());
      return;
    }

    const auto &camera = componentManager.getComponent<CameraComponent>(*activeCamera);
    const auto &cameraPosition = componentManager.getComponent<PositionComponent3D>(*activeCamera);
    render_world_preview(
        renderer_.get(),
        entityManager,
        componentManager,
        world,
        *activeCamera,
        cameraPosition,
        camera,
        width,
        height);

    SDL_RenderPresent(renderer_.get());
  }
}
