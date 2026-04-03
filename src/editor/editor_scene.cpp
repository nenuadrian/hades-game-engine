#include "editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "imgui.h"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/rendering/model_preview.hpp"
#include "../engine/rendering/vector_text.hpp"

namespace hades
{
  namespace
  {
    constexpr char SCENE_WINDOW_TITLE[] = "World";
    constexpr char SETTINGS_WINDOW_TITLE[] = "Settings";
    constexpr float PI = 3.14159265358979323846f;
    constexpr float CUBE_HALF_EXTENT = 0.5f;
    constexpr float EDITOR_SCENE_CAMERA_TARGET_X = 0.0f;
    constexpr float EDITOR_SCENE_CAMERA_TARGET_Y = 0.0f;
    constexpr float EDITOR_SCENE_CAMERA_TARGET_Z = 0.0f;
    constexpr float EDITOR_SCENE_CAMERA_Y = 3.0f;
    constexpr float EDITOR_SCENE_CAMERA_Z = -6.0f;
    constexpr float EDITOR_SCENE_CAMERA_MIN_DISTANCE = 1.0f;
    constexpr float EDITOR_SCENE_CAMERA_MAX_DISTANCE = 250.0f;
    constexpr float EDITOR_SCENE_CAMERA_MIN_PITCH = -89.0f;
    constexpr float EDITOR_SCENE_CAMERA_MAX_PITCH = 89.0f;
    constexpr float EDITOR_SCENE_CAMERA_ROTATION_SENSITIVITY_X = 0.35f;
    constexpr float EDITOR_SCENE_CAMERA_ROTATION_SENSITIVITY_Y = 0.25f;
    constexpr float EDITOR_SCENE_CAMERA_ZOOM_FACTOR = 0.85f;
    constexpr float CAMERA_FRUSTUM_PREVIEW_MAX_DEPTH = 12.0f;
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

    struct EditorSceneViewCamera
    {
      PositionComponent3D position;
      Vec3 right;
      Vec3 up;
      Vec3 forward;
    };

    std::string entity_display_label(Entity::EntityId entity, ComponentManager &componentManager)
    {
      std::string name = "Entity";
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        name = componentManager.getComponent<NameComponent>(entity).value;
      }

      if (componentManager.hasComponent<CameraComponent>(entity) &&
          componentManager.getComponent<CameraComponent>(entity).isMainCamera)
      {
        name += " [Main]";
      }

      if (componentManager.hasComponent<WorldComponent>(entity))
      {
        name += componentManager.getComponent<WorldComponent>(entity).isDefault
                    ? " [World, Default]"
                    : " [World]";
      }

      return name + " (" + std::to_string(entity) + ")";
    }

    Vec3 make_vec3(float x, float y, float z)
    {
      return Vec3{x, y, z};
    }

    Vec3 make_vec3(const PositionComponent3D &position)
    {
      return make_vec3(position.x, position.y, position.z);
    }

    float degrees_to_radians(float degrees)
    {
      return degrees * (PI / 180.0f);
    }

    float radians_to_degrees(float radians)
    {
      return radians * (180.0f / PI);
    }

    Vec3 add_vec3(const Vec3 &lhs, const Vec3 &rhs)
    {
      return make_vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
    }

    Vec3 subtract_vec3(const Vec3 &lhs, const Vec3 &rhs)
    {
      return make_vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
    }

    Vec3 scale_vec3(const Vec3 &value, float scalar)
    {
      return make_vec3(value.x * scalar, value.y * scalar, value.z * scalar);
    }

    float dot_vec3(const Vec3 &lhs, const Vec3 &rhs)
    {
      return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
    }

    Vec3 cross_vec3(const Vec3 &lhs, const Vec3 &rhs)
    {
      return make_vec3(
          (lhs.y * rhs.z) - (lhs.z * rhs.y),
          (lhs.z * rhs.x) - (lhs.x * rhs.z),
          (lhs.x * rhs.y) - (lhs.y * rhs.x));
    }

    float length_vec3(const Vec3 &value)
    {
      return std::sqrt(dot_vec3(value, value));
    }

    Vec3 normalize_vec3(const Vec3 &value)
    {
      const float length = length_vec3(value);
      if (length <= 1e-5f)
      {
        return make_vec3(0.0f, 0.0f, 0.0f);
      }

      return scale_vec3(value, 1.0f / length);
    }

    Vec3 flatten_xz(const Vec3 &value)
    {
      return normalize_vec3(make_vec3(value.x, 0.0f, value.z));
    }

    Vec3 lerp_vec3(const Vec3 &start, const Vec3 &end, float t)
    {
      return make_vec3(
          start.x + ((end.x - start.x) * t),
          start.y + ((end.y - start.y) * t),
          start.z + ((end.z - start.z) * t));
    }

    EditorSceneViewCamera make_editor_scene_view_camera(
        float targetX,
        float targetY,
        float targetZ,
        float distance,
        float yawDegrees,
        float pitchDegrees)
    {
      const float yawRadians = degrees_to_radians(yawDegrees);
      const float pitchRadians = degrees_to_radians(pitchDegrees);
      const float cosPitch = std::cos(pitchRadians);
      const Vec3 forward = normalize_vec3(make_vec3(
          std::sin(yawRadians) * cosPitch,
          std::sin(pitchRadians),
          std::cos(yawRadians) * cosPitch));
      const Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);
      const Vec3 right = normalize_vec3(cross_vec3(worldUp, forward));
      const Vec3 up = normalize_vec3(cross_vec3(forward, right));

      return EditorSceneViewCamera{
          PositionComponent3D(
              targetX - (forward.x * distance),
              targetY - (forward.y * distance),
              targetZ - (forward.z * distance)),
          right,
          up,
          forward,
      };
    }

    Vec3 world_to_camera_space(const Vec3 &worldPoint, const EditorSceneViewCamera &sceneCamera)
    {
      const Vec3 relative = subtract_vec3(worldPoint, make_vec3(sceneCamera.position));
      return make_vec3(
          dot_vec3(relative, sceneCamera.right),
          dot_vec3(relative, sceneCamera.up),
          dot_vec3(relative, sceneCamera.forward));
    }

    bool clip_segment_to_camera_depth(
        Vec3 &start,
        Vec3 &end,
        const CameraComponent &camera)
    {
      auto clip_endpoint = [](Vec3 &point, float &depth, const Vec3 &otherPoint, float otherDepth, float targetDepth)
      {
        const float depthDelta = otherDepth - depth;
        if (std::abs(depthDelta) <= 1e-5f)
        {
          return false;
        }

        const float t = std::clamp((targetDepth - depth) / depthDelta, 0.0f, 1.0f);
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
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        ImVec2 &screenPoint)
    {
      if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f || camera.fovY <= 0.0f)
      {
        return false;
      }

      if (cameraSpacePoint.z <= camera.nearClip || cameraSpacePoint.z >= camera.farClip)
      {
        return false;
      }

      const float aspectRatio = canvasSize.x / canvasSize.y;
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

      screenPoint.x = canvasOrigin.x + ((normalizedX + 1.0f) * 0.5f * canvasSize.x);
      screenPoint.y = canvasOrigin.y + ((1.0f - normalizedY) * 0.5f * canvasSize.y);
      return true;
    }

    bool project_point(
        const Vec3 &worldPoint,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        ImVec2 &screenPoint)
    {
      return project_camera_space_point(
          world_to_camera_space(worldPoint, sceneCamera),
          camera,
          canvasOrigin,
          canvasSize,
          screenPoint);
    }

    bool project_line_segment(
        const Vec3 &start,
        const Vec3 &end,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        ImVec2 &screenStart,
        ImVec2 &screenEnd)
    {
      Vec3 clippedStart = world_to_camera_space(start, sceneCamera);
      Vec3 clippedEnd = world_to_camera_space(end, sceneCamera);
      if (!clip_segment_to_camera_depth(clippedStart, clippedEnd, camera))
      {
        return false;
      }

      return project_camera_space_point(clippedStart, camera, canvasOrigin, canvasSize, screenStart) &&
             project_camera_space_point(clippedEnd, camera, canvasOrigin, canvasSize, screenEnd);
    }

    CameraComponent editor_scene_camera()
    {
      return CameraComponent();
    }

    void draw_editor_grid(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize)
    {
      constexpr int GRID_HALF_EXTENT = 12;
      constexpr float GRID_HEIGHT = 0.0f;
      constexpr ImU32 GRID_MINOR_COLOR = IM_COL32(128, 128, 128, 96);
      constexpr ImU32 GRID_AXIS_X_COLOR = IM_COL32(172, 172, 172, 168);
      constexpr ImU32 GRID_AXIS_Z_COLOR = IM_COL32(172, 172, 172, 168);

      for (int x = -GRID_HALF_EXTENT; x <= GRID_HALF_EXTENT; ++x)
      {
        ImVec2 screenStart;
        ImVec2 screenEnd;
        if (!project_line_segment(
                make_vec3(static_cast<float>(x), GRID_HEIGHT, -static_cast<float>(GRID_HALF_EXTENT)),
                make_vec3(static_cast<float>(x), GRID_HEIGHT, static_cast<float>(GRID_HALF_EXTENT)),
                sceneCamera,
                camera,
                canvasOrigin,
                canvasSize,
                screenStart,
                screenEnd))
        {
          continue;
        }

        drawList->AddLine(
            screenStart,
            screenEnd,
            x == 0 ? GRID_AXIS_Z_COLOR : GRID_MINOR_COLOR,
            x == 0 ? 1.5f : 1.0f);
      }

      for (int z = -GRID_HALF_EXTENT; z <= GRID_HALF_EXTENT; ++z)
      {
        ImVec2 screenStart;
        ImVec2 screenEnd;
        if (!project_line_segment(
                make_vec3(-static_cast<float>(GRID_HALF_EXTENT), GRID_HEIGHT, static_cast<float>(z)),
                make_vec3(static_cast<float>(GRID_HALF_EXTENT), GRID_HEIGHT, static_cast<float>(z)),
                sceneCamera,
                camera,
                canvasOrigin,
                canvasSize,
                screenStart,
                screenEnd))
        {
          continue;
        }

        drawList->AddLine(
            screenStart,
            screenEnd,
            z == 0 ? GRID_AXIS_X_COLOR : GRID_MINOR_COLOR,
            z == 0 ? 1.5f : 1.0f);
      }
    }

    std::array<Vec3, 8> box_corners(
        const Vec3 &minCorner,
        const Vec3 &maxCorner,
        const PositionComponent3D &position)
    {
      return {
          make_vec3(position.x + minCorner.x, position.y + minCorner.y, position.z + minCorner.z),
          make_vec3(position.x + maxCorner.x, position.y + minCorner.y, position.z + minCorner.z),
          make_vec3(position.x + maxCorner.x, position.y + maxCorner.y, position.z + minCorner.z),
          make_vec3(position.x + minCorner.x, position.y + maxCorner.y, position.z + minCorner.z),
          make_vec3(position.x + minCorner.x, position.y + minCorner.y, position.z + maxCorner.z),
          make_vec3(position.x + maxCorner.x, position.y + minCorner.y, position.z + maxCorner.z),
          make_vec3(position.x + maxCorner.x, position.y + maxCorner.y, position.z + maxCorner.z),
          make_vec3(position.x + minCorner.x, position.y + maxCorner.y, position.z + maxCorner.z),
      };
    }

    Vec3 box_center(
        const Vec3 &minCorner,
        const Vec3 &maxCorner,
        const PositionComponent3D &position)
    {
      return make_vec3(
          position.x + ((minCorner.x + maxCorner.x) * 0.5f),
          position.y + ((minCorner.y + maxCorner.y) * 0.5f),
          position.z + ((minCorner.z + maxCorner.z) * 0.5f));
    }

    std::array<Vec3, 4> frustum_plane_corners(
        const PositionComponent3D &position,
        float depth,
        float halfWidth,
        float halfHeight)
    {
      return {
          make_vec3(position.x - halfWidth, position.y + halfHeight, position.z + depth),
          make_vec3(position.x + halfWidth, position.y + halfHeight, position.z + depth),
          make_vec3(position.x + halfWidth, position.y - halfHeight, position.z + depth),
          make_vec3(position.x - halfWidth, position.y - halfHeight, position.z + depth),
      };
    }

    hades::preview::Vec3 to_preview_vec3(const Vec3 &value)
    {
      return hades::preview::make_vec3(value.x, value.y, value.z);
    }

    Vec3 from_preview_vec3(const hades::preview::Vec3 &value)
    {
      return make_vec3(value.x, value.y, value.z);
    }

    ImU32 shaded_preview_color(
        std::uint8_t red,
        std::uint8_t green,
        std::uint8_t blue,
        float shade,
        std::uint8_t alpha)
    {
      return IM_COL32(
          hades::preview::scale_color_channel(red, shade),
          hades::preview::scale_color_channel(green, shade),
          hades::preview::scale_color_channel(blue, shade),
          alpha);
    }

    bool draw_model_mesh(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const ImportedModel &model,
        const std::string *label = nullptr,
        ImU32 labelColor = IM_COL32(205, 210, 218, 255))
    {
      const auto projectedTriangles = hades::preview::project_model_triangles(
          model,
          position,
          [&](const hades::preview::Vec3 &worldPoint)
          {
            return to_preview_vec3(world_to_camera_space(from_preview_vec3(worldPoint), sceneCamera));
          },
          [&](const hades::preview::Vec3 &cameraPoint, hades::preview::Vec2 &screenPoint)
          {
            ImVec2 projectedPoint;
            if (!project_camera_space_point(
                    from_preview_vec3(cameraPoint),
                    camera,
                    canvasOrigin,
                    canvasSize,
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

      for (const auto &triangle : projectedTriangles)
      {
        const ImVec2 points[3] = {
            ImVec2(triangle.points[0].x, triangle.points[0].y),
            ImVec2(triangle.points[1].x, triangle.points[1].y),
            ImVec2(triangle.points[2].x, triangle.points[2].y)};
        drawList->AddConvexPolyFilled(
            points,
            3,
            shaded_preview_color(182, 194, 208, triangle.shade, 230));
        drawList->AddPolyline(
            points,
            3,
            shaded_preview_color(227, 232, 238, std::min(triangle.shade + 0.1f, 1.0f), 255),
            ImDrawFlags_Closed,
            1.0f);
      }

      if (label != nullptr)
      {
        const Vec3 labelAnchor = model.hasBounds
                                     ? box_center(
                                           make_vec3(model.minX, model.minY, model.minZ),
                                           make_vec3(model.maxX, model.maxY, model.maxZ),
                                           position)
                                     : make_vec3(position.x, position.y, position.z);
        ImVec2 centerPoint;
        if (project_point(labelAnchor, sceneCamera, camera, canvasOrigin, canvasSize, centerPoint))
        {
          drawList->AddText(
              ImVec2(centerPoint.x + 6.0f, centerPoint.y + 6.0f),
              labelColor,
              label->c_str());
        }
      }

      return true;
    }

    bool draw_camera_frustum(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &previewCamera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const CameraComponent &camera,
        ImU32 lineColor,
        float thickness)
    {
      if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f ||
          camera.fovY <= 0.0f ||
          camera.nearClip <= 0.0f ||
          camera.farClip <= camera.nearClip)
      {
        return false;
      }

      const float farDepth = std::min(
          camera.farClip,
          std::max(CAMERA_FRUSTUM_PREVIEW_MAX_DEPTH, camera.nearClip + 0.001f));
      const float aspectRatio = canvasSize.x / canvasSize.y;
      const float halfFovRadians = degrees_to_radians(camera.fovY * 0.5f);
      const float tanHalfFov = std::tan(halfFovRadians);
      if (aspectRatio <= 0.0f || tanHalfFov <= 0.0f)
      {
        return false;
      }

      const float nearHalfHeight = tanHalfFov * camera.nearClip;
      const float nearHalfWidth = nearHalfHeight * aspectRatio;
      const float farHalfHeight = tanHalfFov * farDepth;
      const float farHalfWidth = farHalfHeight * aspectRatio;

      const auto nearCorners = frustum_plane_corners(position, camera.nearClip, nearHalfWidth, nearHalfHeight);
      const auto farCorners = frustum_plane_corners(position, farDepth, farHalfWidth, farHalfHeight);

      int drawnSegmentCount = 0;
      for (std::size_t index = 0; index < nearCorners.size(); ++index)
      {
        const std::size_t nextIndex = (index + 1) % nearCorners.size();
        ImVec2 lineStart;
        ImVec2 lineEnd;

        if (project_line_segment(
                nearCorners[index],
                nearCorners[nextIndex],
                sceneCamera,
                previewCamera,
                canvasOrigin,
                canvasSize,
                lineStart,
                lineEnd))
        {
          drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
          ++drawnSegmentCount;
        }

        if (project_line_segment(
                farCorners[index],
                farCorners[nextIndex],
                sceneCamera,
                previewCamera,
                canvasOrigin,
                canvasSize,
                lineStart,
                lineEnd))
        {
          drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
          ++drawnSegmentCount;
        }

        if (project_line_segment(
                nearCorners[index],
                farCorners[index],
                sceneCamera,
                previewCamera,
                canvasOrigin,
                canvasSize,
                lineStart,
                lineEnd))
        {
          drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
          ++drawnSegmentCount;
        }
      }

      return drawnSegmentCount > 0;
    }

    bool draw_wire_box(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const Vec3 &minCorner,
        const Vec3 &maxCorner,
        ImU32 lineColor,
        float thickness,
        const std::string *label = nullptr,
        ImU32 labelColor = IM_COL32(205, 210, 218, 255))
    {
      const auto corners = box_corners(minCorner, maxCorner, position);
      int drawnEdgeCount = 0;
      for (const auto &edge : BOX_EDGES)
      {
        ImVec2 lineStart;
        ImVec2 lineEnd;
        if (!project_line_segment(
                corners[edge[0]],
                corners[edge[1]],
                sceneCamera,
                camera,
                canvasOrigin,
                canvasSize,
                lineStart,
                lineEnd))
        {
          continue;
        }

        drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
        ++drawnEdgeCount;
      }

      if (drawnEdgeCount == 0)
      {
        return false;
      }

      if (label != nullptr)
      {
        ImVec2 centerPoint;
        if (project_point(box_center(minCorner, maxCorner, position), sceneCamera, camera, canvasOrigin, canvasSize, centerPoint))
        {
          drawList->AddText(
              ImVec2(centerPoint.x + 6.0f, centerPoint.y + 6.0f),
              labelColor,
              label->c_str());
        }
      }

      return true;
    }

    bool draw_vector_text(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const TextComponent &text,
        ImU32 lineColor,
        float thickness)
    {
      const VectorTextGeometry3D geometry = build_vector_text_geometry(
          text.content,
          VectorTextStyle{
              std::max(0.05f, text.fontSize),
              std::max(0.0f, text.wrapWidth),
              std::max(0.8f, text.lineSpacing),
          },
          make_vector_text_frame_from_euler(
              VectorTextPoint3D{position.x, position.y, position.z},
              text.yawDegrees,
              text.pitchDegrees,
              text.rollDegrees));

      int visibleSegmentCount = 0;
      for (const auto &segment : geometry.segments)
      {
        ImVec2 screenStart;
        ImVec2 screenEnd;
        if (!project_line_segment(
                make_vec3(segment.start.x, segment.start.y, segment.start.z),
                make_vec3(segment.end.x, segment.end.y, segment.end.z),
                sceneCamera,
                camera,
                canvasOrigin,
                canvasSize,
                screenStart,
                screenEnd))
        {
          continue;
        }

        drawList->AddLine(screenStart, screenEnd, lineColor, thickness);
        ++visibleSegmentCount;
      }

      return visibleSegmentCount > 0;
    }

    int draw_world_preview(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> world,
        std::optional<Entity::EntityId> excludedEntity)
    {
      int visibleRenderableCount = 0;
      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (excludedEntity.has_value() && entity == *excludedEntity)
        {
          continue;
        }

        if (world.has_value() && !entity_belongs_to_world(entity, *world, componentManager))
        {
          continue;
        }

        if (!componentManager.hasComponent<PositionComponent3D>(entity))
        {
          continue;
        }

        const auto &position = componentManager.getComponent<PositionComponent3D>(entity);
        const std::string label = entity_display_label(entity, componentManager);

        if (componentManager.hasComponent<CameraComponent>(entity))
        {
          const auto &entityCamera = componentManager.getComponent<CameraComponent>(entity);
          if (draw_camera_frustum(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  entityCamera,
                  IM_COL32(255, 165, 0, 255),
                  1.5f))
          {
            ++visibleRenderableCount;
          }
        }

        if (componentManager.hasComponent<PrimitiveComponent>(entity))
        {
          const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
          if (primitive.type != PrimitiveType::Cube)
          {
            continue;
          }

          if (draw_wire_box(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT),
                  make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT),
                  IM_COL32(223, 228, 235, 255),
                  1.5f,
                  &label))
          {
            ++visibleRenderableCount;
          }
          continue;
        }

        if (componentManager.hasComponent<ModelComponent>(entity))
        {
          const auto &model = componentManager.getComponent<ModelComponent>(entity).model;
          if (hades::preview::has_renderable_geometry(model) &&
              draw_model_mesh(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  model,
                  &label,
                  IM_COL32(205, 210, 218, 255)))
          {
            ++visibleRenderableCount;
            continue;
          }

          const Vec3 minCorner = model.hasBounds
                                     ? make_vec3(model.minX, model.minY, model.minZ)
                                     : make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT);
          const Vec3 maxCorner = model.hasBounds
                                     ? make_vec3(model.maxX, model.maxY, model.maxZ)
                                     : make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT);

          if (draw_wire_box(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  minCorner,
                  maxCorner,
                  IM_COL32(179, 189, 202, 255),
                  1.5f,
                  &label,
                  IM_COL32(205, 210, 218, 255)))
          {
            ++visibleRenderableCount;
          }
        }

        if (componentManager.hasComponent<TextComponent>(entity))
        {
          const auto &text = componentManager.getComponent<TextComponent>(entity);
          if (draw_vector_text(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  text,
                  IM_COL32(227, 233, 240, 255),
                  1.5f))
          {
            ++visibleRenderableCount;
          }
        }
      }

      return visibleRenderableCount;
    }
  }

  void Editor::reset_scene_camera()
  {
    sceneCameraTargetX_ = EDITOR_SCENE_CAMERA_TARGET_X;
    sceneCameraTargetY_ = EDITOR_SCENE_CAMERA_TARGET_Y;
    sceneCameraTargetZ_ = EDITOR_SCENE_CAMERA_TARGET_Z;
    sceneCameraDistance_ = std::sqrt(
        (EDITOR_SCENE_CAMERA_Y * EDITOR_SCENE_CAMERA_Y) +
        (EDITOR_SCENE_CAMERA_Z * EDITOR_SCENE_CAMERA_Z));
    sceneCameraYawDegrees_ = 0.0f;
    sceneCameraPitchDegrees_ = -radians_to_degrees(
        std::atan2(EDITOR_SCENE_CAMERA_Y, std::abs(EDITOR_SCENE_CAMERA_Z)));
  }

  void Editor::scene(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(SCENE_WINDOW_TITLE);
    ImGuiIO &io = ImGui::GetIO();

    const auto sceneWorld = state.isPlaying
                                ? state.activeWorld
                                : (state.loadedWorld.has_value() &&
                                           componentManager.hasComponent<WorldComponent>(*state.loadedWorld)
                                       ? state.loadedWorld
                                       : normalize_default_world(entityManager, componentManager));

    if (sceneWorld.has_value())
    {
      ImGui::TextDisabled(
          "%s: %s",
          state.isPlaying ? "Active World" : "Loaded World",
          entity_label(*sceneWorld, componentManager).c_str());
    }
    else
    {
      ImGui::TextDisabled("No world is loaded.");
    }

    if (!sceneWorld.has_value())
    {
      ImGui::End();
      return;
    }

    ImGui::Spacing();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 64.0f || canvasSize.y < 64.0f)
    {
      ImGui::End();
      return;
    }

    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y);
    ImGui::InvisibleButton("scene_canvas", canvasSize);
    const bool sceneCanvasHovered = ImGui::IsItemHovered();
    const bool sceneCanvasActive = ImGui::IsItemActive();
    const bool sceneWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool rotateModifierDown = io.KeyCtrl || io.KeySuper;

    if (sceneCanvasHovered && io.MouseWheel != 0.0f)
    {
      sceneCameraDistance_ = std::clamp(
          sceneCameraDistance_ * static_cast<float>(std::pow(EDITOR_SCENE_CAMERA_ZOOM_FACTOR, io.MouseWheel)),
          EDITOR_SCENE_CAMERA_MIN_DISTANCE,
          EDITOR_SCENE_CAMERA_MAX_DISTANCE);
    }

    if (sceneCanvasActive && rotateModifierDown && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
      sceneCameraYawDegrees_ = std::remainder(
          sceneCameraYawDegrees_ + (io.MouseDelta.x * EDITOR_SCENE_CAMERA_ROTATION_SENSITIVITY_X),
          360.0f);
      sceneCameraPitchDegrees_ = std::clamp(
          sceneCameraPitchDegrees_ - (io.MouseDelta.y * EDITOR_SCENE_CAMERA_ROTATION_SENSITIVITY_Y),
          EDITOR_SCENE_CAMERA_MIN_PITCH,
          EDITOR_SCENE_CAMERA_MAX_PITCH);
    }

    EditorSceneViewCamera sceneCamera = make_editor_scene_view_camera(
        sceneCameraTargetX_,
        sceneCameraTargetY_,
        sceneCameraTargetZ_,
        sceneCameraDistance_,
        sceneCameraYawDegrees_,
        sceneCameraPitchDegrees_);

    if (sceneWindowFocused && !rotateModifierDown && !io.WantTextInput)
    {
      Vec3 movement = make_vec3(0.0f, 0.0f, 0.0f);
      const Vec3 forwardOnGround = flatten_xz(sceneCamera.forward);

      if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
      {
        movement = subtract_vec3(movement, sceneCamera.right);
      }
      if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
      {
        movement = add_vec3(movement, sceneCamera.right);
      }
      if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
      {
        movement = add_vec3(movement, forwardOnGround);
      }
      if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
      {
        movement = subtract_vec3(movement, forwardOnGround);
      }

      movement = flatten_xz(movement);
      if (length_vec3(movement) > 0.0f)
      {
        const float cameraMoveSpeed = std::max(3.0f, sceneCameraDistance_);
        sceneCameraTargetX_ += movement.x * cameraMoveSpeed * io.DeltaTime;
        sceneCameraTargetZ_ += movement.z * cameraMoveSpeed * io.DeltaTime;
        sceneCamera = make_editor_scene_view_camera(
            sceneCameraTargetX_,
            sceneCameraTargetY_,
            sceneCameraTargetZ_,
            sceneCameraDistance_,
            sceneCameraYawDegrees_,
            sceneCameraPitchDegrees_);
      }
    }

    const CameraComponent camera = editor_scene_camera();

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasOrigin, canvasMax, IM_COL32(17, 20, 24, 255));
    drawList->AddRect(canvasOrigin, canvasMax, IM_COL32(70, 76, 86, 255));
    drawList->PushClipRect(canvasOrigin, canvasMax, true);
    drawList->AddText(
        ImVec2(canvasOrigin.x + 12.0f, canvasOrigin.y + 12.0f),
        IM_COL32(135, 142, 154, 255),
        "Arrows move  |  Wheel zoom  |  Ctrl/Cmd + drag rotate");

    if (!state.isPlaying)
    {
      draw_editor_grid(drawList, sceneCamera, camera, canvasOrigin, canvasSize);
    }

    draw_world_preview(
        drawList,
        sceneCamera,
        camera,
        canvasOrigin,
        canvasSize,
        entityManager,
        componentManager,
        sceneWorld,
        std::nullopt);

    drawList->PopClipRect();
    ImGui::End();
  }

  void Editor::render_settings_window()
  {
    if (!openSettingsWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (focusSettingsWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusSettingsWindow_ = false;
    }

    if (!ImGui::Begin(SETTINGS_WINDOW_TITLE, &openSettingsWindow_))
    {
      ImGui::End();
      return;
    }

    ImGui::TextDisabled("Editor");
    ImGui::Separator();
    ImGui::Checkbox("Show Debug Window", &state.showDebugInfo);

    ImGui::Spacing();
    ImGui::TextDisabled("Scene Camera");
    ImGui::Separator();

    float target[3] = {sceneCameraTargetX_, sceneCameraTargetY_, sceneCameraTargetZ_};
    if (ImGui::DragFloat3("Target", target, 0.1f))
    {
      sceneCameraTargetX_ = target[0];
      sceneCameraTargetY_ = target[1];
      sceneCameraTargetZ_ = target[2];
    }

    ImGui::SliderFloat(
        "Distance",
        &sceneCameraDistance_,
        EDITOR_SCENE_CAMERA_MIN_DISTANCE,
        EDITOR_SCENE_CAMERA_MAX_DISTANCE,
        "%.1f");
    ImGui::SliderFloat("Yaw", &sceneCameraYawDegrees_, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat(
        "Pitch",
        &sceneCameraPitchDegrees_,
        EDITOR_SCENE_CAMERA_MIN_PITCH,
        EDITOR_SCENE_CAMERA_MAX_PITCH,
        "%.1f deg");

    if (ImGui::Button("Reset Scene Camera"))
    {
      reset_scene_camera();
    }

    ImGui::End();
  }

  void Editor::debug(float deltaTime)
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %f", 1 / deltaTime);
    ImGui::Text("Play Mode: %s", state.isPlaying ? "Playing" : "Stopped");
    if (state.activeCamera.has_value())
    {
      ImGui::Text("Active Camera: %u", *state.activeCamera);
    }
    ImGui::End();
  }
}
