#include "editor.hpp"
#include "native_dialogs.hpp"
#include "script_document.hpp"
#include "workspace_file_operations.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_2d.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/gui/imgui.hpp"
#include "../engine/rendering/model_preview.hpp"
#include "../engine/rendering/vector_text.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char COMPONENTS_WINDOW_TITLE[] = "Components";
    constexpr char SCENE_WINDOW_TITLE[] = "World";
    constexpr char SCRIPT_EDITOR_WINDOW_TITLE[] = "Script Editor";
    constexpr char SETTINGS_WINDOW_TITLE[] = "Settings";
    constexpr char IMPORT_MODEL_POPUP_TITLE[] = "Import Model";
    constexpr char WORKSPACE_CREATE_POPUP_TITLE[] = "Create Workspace Item";
    constexpr char WORKSPACE_IMPORT_POPUP_TITLE[] = "Import Into Workspace";
    constexpr char WORKSPACE_DELETE_POPUP_TITLE[] = "Delete Workspace Item";
    constexpr char SCRIPT_EDITOR_UNSAVED_POPUP_TITLE[] = "Unsaved Script Changes";
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

    const char *primitive_type_label(PrimitiveType type)
    {
      switch (type)
      {
      case PrimitiveType::Cube:
        return "Cube";
      }

      return "Unknown";
    }

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

    EditorSceneViewCamera make_axis_aligned_scene_view_camera(const PositionComponent3D &position)
    {
      return EditorSceneViewCamera{
          position,
          make_vec3(1.0f, 0.0f, 0.0f),
          make_vec3(0.0f, 1.0f, 0.0f),
          make_vec3(0.0f, 0.0f, 1.0f),
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

    std::array<Vec3, 8> cube_corners(const PositionComponent3D &position)
    {
      return box_corners(
          make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT),
          make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT),
          position);
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
          VectorTextFrame3D{
              VectorTextPoint3D{position.x, position.y, position.z},
              VectorTextPoint3D{sceneCamera.right.x, sceneCamera.right.y, sceneCamera.right.z},
              VectorTextPoint3D{sceneCamera.up.x, sceneCamera.up.y, sceneCamera.up.z},
              0.5f,
              0.5f,
          });

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

    std::string script_component_label(const ScriptAttachment &attachment, std::size_t index)
    {
      std::string suffix;
      if (!attachment.className.empty())
      {
        suffix = attachment.className;
      }
      else if (!attachment.scriptPath.empty())
      {
        suffix = std::filesystem::path(attachment.scriptPath).stem().string();
      }

      std::string label = "Script Component " + std::to_string(index + 1);
      if (!suffix.empty())
      {
        label += ": " + suffix;
      }

      return label;
    }

    std::string path_display_name(const std::filesystem::path &path)
    {
      const std::string filename = path.filename().string();
      return filename.empty() ? path.string() : filename;
    }

    std::string relative_workspace_path(const std::filesystem::path &workspacePath, const std::filesystem::path &path)
    {
      const std::filesystem::path relativePath = path.lexically_relative(workspacePath);
      return relativePath.empty() ? path.generic_string() : relativePath.generic_string();
    }

    bool has_path_separator(const std::string &name)
    {
      return name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
    }

    bool path_is_same_or_within(
        const std::filesystem::path &parentPath,
        const std::filesystem::path &candidatePath)
    {
      const std::filesystem::path normalizedParent = parentPath.lexically_normal();
      const std::filesystem::path normalizedCandidate = candidatePath.lexically_normal();
      if (normalizedParent == normalizedCandidate)
      {
        return true;
      }

      const std::filesystem::path relativePath = normalizedCandidate.lexically_relative(normalizedParent);
      if (relativePath.empty())
      {
        return false;
      }

      for (const auto &segment : relativePath)
      {
        if (segment == "..")
        {
          return false;
        }
      }

      return true;
    }

    template <std::size_t Size>
    void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
    {
      buffer.fill('\0');
      const std::size_t copyLength = std::min(value.size(), Size - 1);
      std::copy_n(value.data(), copyLength, buffer.data());
      buffer[copyLength] = '\0';
    }

    std::string workspace_delete_button_label(const std::filesystem::path &path)
    {
      std::error_code errorCode;
      return std::filesystem::is_directory(path, errorCode) ? "Delete Folder" : "Delete File";
    }

    std::string csharp_class_name_from_stem(const std::string &stem)
    {
      std::string className;
      bool capitalizeNext = true;
      for (const char character : stem)
      {
        const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter) == 0)
        {
          capitalizeNext = true;
          continue;
        }

        if (className.empty() && std::isdigit(unsignedCharacter) != 0)
        {
          className.push_back('_');
        }

        if (capitalizeNext)
        {
          className.push_back(static_cast<char>(std::toupper(unsignedCharacter)));
          capitalizeNext = false;
        }
        else
        {
          className.push_back(character);
        }
      }

      return className.empty() ? "NewScript" : className;
    }

    std::string build_script_template(const std::string &className)
    {
      return "using Hades.Scripting;\n\n"
             "public sealed class " +
             className +
             " : HadesScript\n"
             "{\n"
             "    public override void OnStart(EntityContext context)\n"
             "    {\n"
             "    }\n"
             "\n"
             "    public override void OnUpdate(EntityContext context, float deltaTime)\n"
             "    {\n"
             "    }\n"
             "}\n";
    }

    std::string trim(const std::string &s)
    {
      const auto start = s.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
      {
        return {};
      }
      const auto end = s.find_last_not_of(" \t\r\n");
      return s.substr(start, end - start + 1);
    }

    std::vector<std::pair<std::string, std::string>> parse_public_fields(
        const std::filesystem::path &scriptPath)
    {
      std::vector<std::pair<std::string, std::string>> fields;

      std::ifstream file(scriptPath);
      if (!file.is_open())
      {
        return fields;
      }

      bool insideTargetClass = false;
      int braceDepth = 0;
      int classBraceDepth = -1;

      std::string line;
      while (std::getline(file, line))
      {
        const std::string trimmed = trim(line);

        // Look for a class that extends HadesScript.
        if (!insideTargetClass)
        {
          if (trimmed.find("HadesScript") != std::string::npos &&
              trimmed.find("class ") != std::string::npos)
          {
            insideTargetClass = true;
            // Count any opening braces on this line.
            for (char ch : trimmed)
            {
              if (ch == '{')
              {
                if (classBraceDepth < 0)
                {
                  classBraceDepth = braceDepth;
                }
                ++braceDepth;
              }
              else if (ch == '}')
              {
                --braceDepth;
              }
            }
            continue;
          }
        }

        // Track braces.
        for (char ch : trimmed)
        {
          if (ch == '{')
          {
            if (insideTargetClass && classBraceDepth < 0)
            {
              classBraceDepth = braceDepth;
            }
            ++braceDepth;
          }
          else if (ch == '}')
          {
            --braceDepth;
            if (insideTargetClass && braceDepth <= classBraceDepth)
            {
              // Exited the class body.
              insideTargetClass = false;
              classBraceDepth = -1;
            }
          }
        }

        if (!insideTargetClass || classBraceDepth < 0)
        {
          continue;
        }

        // Only look at direct class-body members (one brace level inside the class).
        if (braceDepth != classBraceDepth + 1)
        {
          continue;
        }

        // Skip lines that are methods, overrides, static, or don't start with public.
        if (trimmed.find("public") != 0)
        {
          continue;
        }
        if (trimmed.find("override") != std::string::npos ||
            trimmed.find("virtual") != std::string::npos ||
            trimmed.find("static") != std::string::npos ||
            trimmed.find("(") != std::string::npos ||
            trimmed.find("void") != std::string::npos ||
            trimmed.find("class ") != std::string::npos)
        {
          continue;
        }

        // Parse: public <type> <name> [= ...] ;
        std::istringstream iss(trimmed);
        std::string keyword;
        std::string type;
        std::string name;
        if (!(iss >> keyword >> type >> name))
        {
          continue;
        }

        // Remove trailing ; or = from name.
        while (!name.empty() && (name.back() == ';' || name.back() == '='))
        {
          name.pop_back();
        }

        if (!name.empty() && !type.empty())
        {
          fields.emplace_back(type, name);
        }
      }

      return fields;
    }

    template <typename T>
    void remove_component_if_present(ComponentManager &componentManager, Entity::EntityId entity)
    {
      if (componentManager.hasComponent<T>(entity))
      {
        componentManager.removeComponent<T>(entity);
      }
    }

    bool hierarchy_contains_entity(
        Entity::EntityId root,
        Entity::EntityId target,
        ComponentManager &componentManager)
    {
      if (root == target)
      {
        return true;
      }

      if (!componentManager.hasComponent<TransformHierarchyComponent>(root))
      {
        return false;
      }

      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(root);
      for (const Entity::EntityId child : hierarchy.children)
      {
        if (hierarchy_contains_entity(child, target, componentManager))
        {
          return true;
        }
      }

      return false;
    }

    void destroy_entity_subtree(
        Entity::EntityId entity,
        EntityManager &entityManager,
        ComponentManager &componentManager)
    {
      if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        return;
      }

      const auto hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      for (const Entity::EntityId child : hierarchy.children)
      {
        destroy_entity_subtree(child, entityManager, componentManager);
      }

      if (hierarchy.parent.has_value() &&
          componentManager.hasComponent<TransformHierarchyComponent>(*hierarchy.parent))
      {
        auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(*hierarchy.parent);
        parentHierarchy.removeChild(entity);
      }

      remove_component_if_present<NameComponent>(componentManager, entity);
      remove_component_if_present<WorldComponent>(componentManager, entity);
      remove_component_if_present<TransformHierarchyComponent>(componentManager, entity);
      remove_component_if_present<PositionComponent2D>(componentManager, entity);
      remove_component_if_present<PositionComponent3D>(componentManager, entity);
      remove_component_if_present<CameraComponent>(componentManager, entity);
      remove_component_if_present<AudioListenerComponent>(componentManager, entity);
      remove_component_if_present<PrimitiveComponent>(componentManager, entity);
      remove_component_if_present<AudioSourceComponent>(componentManager, entity);
      remove_component_if_present<ModelComponent>(componentManager, entity);
      remove_component_if_present<RenderComponent>(componentManager, entity);
      remove_component_if_present<ScriptComponent>(componentManager, entity);

      entityManager.destroyEntity(entity);
    }

    bool create_workspace_item(
        const std::filesystem::path &parentPath,
        const std::string &rawName,
        const Editor::WorkspaceCreateKind kind,
        std::string *errorMessage)
    {
      if (rawName.empty())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Enter a name before creating the item.";
        }
        return false;
      }

      if (has_path_separator(rawName))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Names cannot contain path separators.";
        }
        return false;
      }

      std::error_code errorCode;
      if (!std::filesystem::exists(parentPath, errorCode) || !std::filesystem::is_directory(parentPath, errorCode))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "The selected parent folder is no longer available.";
        }
        return false;
      }

      std::filesystem::path targetPath = parentPath / rawName;
      if (kind == Editor::WorkspaceCreateKind::Script)
      {
        if (!targetPath.has_extension())
        {
          targetPath += ".cs";
        }
        else if (targetPath.extension() != ".cs")
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Scripts must use the .cs extension.";
          }
          return false;
        }
      }

      if (std::filesystem::exists(targetPath, errorCode))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "'" + path_display_name(targetPath) + "' already exists.";
        }
        return false;
      }

      if (kind == Editor::WorkspaceCreateKind::Folder)
      {
        if (!std::filesystem::create_directory(targetPath, errorCode))
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "Unable to create folder '" + targetPath.string() + "': " + errorCode.message();
          }
          return false;
        }
        return true;
      }

      std::ofstream output(targetPath);
      if (!output)
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Unable to create script '" + targetPath.string() + "'.";
        }
        return false;
      }

      output << build_script_template(csharp_class_name_from_stem(targetPath.stem().string()));
      if (!output.good())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "Unable to write script '" + targetPath.string() + "'.";
        }
        return false;
      }

      return true;
    }

    bool build_workspace_tree(
        const std::filesystem::path &path,
        const std::filesystem::path &workspacePath,
        Editor::WorkspaceTreeNode &node,
        std::vector<std::string> &scriptFiles,
        std::string *errorMessage)
    {
      node.path = path;
      node.directory = std::filesystem::is_directory(path);
      node.children.clear();

      if (!node.directory)
      {
        if (path.extension() == ".cs")
        {
          scriptFiles.push_back(relative_workspace_path(workspacePath, path));
        }
        return true;
      }

      std::error_code errorCode;
      std::vector<std::filesystem::directory_entry> entries;
      for (std::filesystem::directory_iterator iterator(path, errorCode); !errorCode && iterator != std::filesystem::directory_iterator(); iterator.increment(errorCode))
      {
        entries.push_back(*iterator);
      }

      if (errorCode)
      {
        if (errorMessage != nullptr && errorMessage->empty())
        {
          *errorMessage = "Unable to inspect workspace folder '" + path.string() + "': " + errorCode.message();
        }
        return false;
      }

      std::sort(
          entries.begin(),
          entries.end(),
          [](const std::filesystem::directory_entry &lhs, const std::filesystem::directory_entry &rhs)
          {
            std::error_code lhsError;
            std::error_code rhsError;
            const bool lhsDirectory = lhs.is_directory(lhsError);
            const bool rhsDirectory = rhs.is_directory(rhsError);
            if (lhsDirectory != rhsDirectory)
            {
              return lhsDirectory > rhsDirectory;
            }

            return lhs.path().filename().string() < rhs.path().filename().string();
          });

      for (const auto &entry : entries)
      {
        Editor::WorkspaceTreeNode child;
        build_workspace_tree(entry.path(), workspacePath, child, scriptFiles, errorMessage);
        node.children.push_back(std::move(child));
      }

      return true;
    }
  }

  Editor::Editor() : gui(std::make_unique<ImGui_GUI>())
  {
    reset_scene_camera();
  }

  Editor::~Editor() = default;

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

  void Editor::reset_workspace_session()
  {
    state.selectedEntity.reset();
    state.pendingEntityPreset = EditorEntityPreset::None;
    state.pendingPlayAction = EditorPlayAction::None;
    state.isPlaying = false;
    state.loadedWorld.reset();
    state.activeWorld.reset();
    state.activeCamera.reset();
    state.playModeMessage.clear();
    pendingEntityDeletion_.reset();
    reset_scene_camera();

    activeWorkspacePath_.clear();
    workspaceTreeRoot_.reset();
    workspaceScriptFiles_.clear();
    workspaceScanError_.clear();
    activeScriptEditorPath_.reset();
    activeScriptEditorRelativePath_.clear();
    activeScriptEditorContents_.clear();
    activeScriptEditorSavedWriteTime_.reset();
    activeScriptEditorDirty_ = false;
    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;
    focusScriptEditorWindow_ = false;
    openScriptEditorUnsavedChangesDialog_ = false;
    pendingScriptEditorPath_.reset();
    pendingScriptEditorRelativePath_.clear();
    nextWorkspaceScanTime_ = 0.0;
    workspaceScriptListDirty_ = false;
    scriptModTimes_.clear();
    parsedFieldsCache_.clear();
    parsedFieldsModTimes_.clear();
    lastCompileError_.clear();
    lastCompileSucceeded_ = true;
    backgroundCompileInProgress_ = false;
  }

  void Editor::render(
      float deltaTime,
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    refresh_workspace_cache(deltaTime, workspacePath);
    ensure_world_state(entityManager, componentManager);
    sync_menu_bar(entityManager, componentManager);
    configure_default_dock_layout(gui->render_frame());

    // File watch: detect script changes and trigger background compile.
    bool scriptsChanged = workspaceScriptListDirty_;
    if (!activeWorkspacePath_.empty())
    {
      for (const auto &relPath : workspaceScriptFiles_)
      {
        const auto fullPath = activeWorkspacePath_ / relPath;
        std::error_code ec;
        const auto modTime = std::filesystem::last_write_time(fullPath, ec);
        if (ec)
        {
          continue;
        }
        auto it = scriptModTimes_.find(relPath);
        if (it == scriptModTimes_.end())
        {
          scriptModTimes_[relPath] = modTime;
          scriptsChanged = true;
        }
        else if (it->second != modTime)
        {
          it->second = modTime;
          scriptsChanged = true;
        }
      }

      if (scriptsChanged)
      {
        queue_workspace_script_compile();
      }
    }

    if (backgroundCompileInProgress_ && backgroundCompileResult_.valid() &&
        backgroundCompileResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
      lastCompileError_ = backgroundCompileResult_.get();
      lastCompileSucceeded_ = lastCompileError_.empty();
      backgroundCompileInProgress_ = false;
    }

    handle_entity_creation_requests(entityManager, componentManager);
    import_model(entityManager, componentManager);
    handle_play_mode_requests(entityManager, componentManager, scriptRuntime);
    workspace(entityManager, componentManager);
    render_script_editor();
    entities(entityManager, componentManager);
    handle_entity_deletion_requests(entityManager, componentManager, scriptRuntime);
    scene(entityManager, componentManager);
    properties(entityManager, componentManager);
    components(entityManager, componentManager);
    render_settings_window();
    debug(deltaTime);
  }

  void Editor::sync_menu_bar(EntityManager &entityManager, ComponentManager &componentManager)
  {
    gui->menu_bar_items.clear();

    MenuBarItem file;
    file.title = "File";

    MenuBarItem newWorld;
    newWorld.title = "New World";
    newWorld.on_activate = [this, &entityManager, &componentManager]()
    {
      create_world(entityManager, componentManager);
    };

    MenuBarItem settings;
    settings.title = "Settings";
    settings.on_activate = [this]()
    {
      openSettingsWindow_ = true;
      focusSettingsWindow_ = true;
    };

    MenuBarItem exit;
    exit.title = "Exit";
    exit.on_activate = [this]()
    {
      state.events.push(EDITOR_QUIT);
    };

    file.children_menu_items.push_back(newWorld);
    file.children_menu_items.push_back(settings);
    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);

    MenuBarItem worlds;
    worlds.title = "Worlds";

    const auto worldEntities = find_world_entities(entityManager, componentManager);
    if (worldEntities.empty())
    {
      MenuBarItem emptyWorlds;
      emptyWorlds.title = "No Worlds Available";
      worlds.children_menu_items.push_back(emptyWorlds);
    }
    else
    {
      for (const Entity::EntityId world : worldEntities)
      {
        std::string label = entity_label(world, componentManager);
        if (state.loadedWorld.has_value() && *state.loadedWorld == world)
        {
          label += " [Loaded]";
        }

        MenuBarItem worldItem;
        worldItem.title = std::move(label);
        worldItem.on_activate = [this, &componentManager, world]()
        {
          load_world(world, componentManager);
        };
        worlds.children_menu_items.push_back(std::move(worldItem));
      }
    }

    gui->menu_bar_items.push_back(worlds);

    MenuBarItem game;
    game.title = "Game";

    MenuBarItem play;
    play.title = "Play";
    play.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Start;
    };

    MenuBarItem stop;
    stop.title = "Stop";
    stop.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Stop;
    };

    game.children_menu_items.push_back(play);
    game.children_menu_items.push_back(stop);
    gui->menu_bar_items.push_back(game);
  }

  void Editor::configure_default_dock_layout(std::uint32_t dockspaceId)
  {
    if (dockLayoutInitialized || dockspaceId == 0)
    {
      return;
    }

    dockLayoutInitialized = true;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID mainDockId = dockspaceId;
    ImGuiID workspaceDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.22f, nullptr, &mainDockId);
    const ImGuiID entitiesDockId = ImGui::DockBuilderSplitNode(workspaceDockId, ImGuiDir_Down, 0.56f, nullptr, &workspaceDockId);
    const ImGuiID scriptEditorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.38f, nullptr, &mainDockId);
    ImGuiID inspectorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.34f, nullptr, &mainDockId);
    const ImGuiID componentsDockId = ImGui::DockBuilderSplitNode(inspectorDockId, ImGuiDir_Right, 0.45f, nullptr, &inspectorDockId);
    ImGui::DockBuilderDockWindow(WORKSPACE_WINDOW_TITLE, workspaceDockId);
    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(COMPONENTS_WINDOW_TITLE, componentsDockId);
    ImGui::DockBuilderDockWindow(SCRIPT_EDITOR_WINDOW_TITLE, scriptEditorDockId);
    ImGui::DockBuilderDockWindow(SCENE_WINDOW_TITLE, mainDockId);
    ImGui::DockBuilderFinish(dockspaceId);
  }

  void Editor::refresh_workspace_cache(float deltaTime, const std::filesystem::path &workspacePath)
  {
    const double now = ImGui::GetTime();
    if (workspacePath != activeWorkspacePath_)
    {
      activeWorkspacePath_ = workspacePath;
      workspaceTreeRoot_.reset();
      workspaceScriptFiles_.clear();
      workspaceScanError_.clear();
      activeScriptEditorPath_.reset();
      activeScriptEditorRelativePath_.clear();
      activeScriptEditorContents_.clear();
      activeScriptEditorSavedWriteTime_.reset();
      activeScriptEditorDirty_ = false;
      scriptEditorStatusMessage_.clear();
      scriptEditorStatusIsError_ = false;
      focusScriptEditorWindow_ = false;
      openScriptEditorUnsavedChangesDialog_ = false;
      pendingScriptEditorPath_.reset();
      pendingScriptEditorRelativePath_.clear();
      workspaceScriptListDirty_ = false;
      scriptModTimes_.clear();
      parsedFieldsCache_.clear();
      parsedFieldsModTimes_.clear();
      nextWorkspaceScanTime_ = 0.0;
    }

    if (activeWorkspacePath_.empty() || (workspaceTreeRoot_.has_value() && now < nextWorkspaceScanTime_))
    {
      return;
    }

    WorkspaceTreeNode rootNode;
    std::vector<std::string> scriptFiles;
    std::string scanError;
    build_workspace_tree(activeWorkspacePath_, activeWorkspacePath_, rootNode, scriptFiles, &scanError);
    std::sort(scriptFiles.begin(), scriptFiles.end());
    if (scriptFiles != workspaceScriptFiles_)
    {
      workspaceScriptListDirty_ = true;

      std::unordered_map<std::string, std::filesystem::file_time_type> retainedModTimes;
      retainedModTimes.reserve(scriptFiles.size());
      for (const auto &relPath : scriptFiles)
      {
        const auto existing = scriptModTimes_.find(relPath);
        if (existing != scriptModTimes_.end())
        {
          retainedModTimes.emplace(existing->first, existing->second);
        }
      }
      scriptModTimes_ = std::move(retainedModTimes);
    }
    workspaceTreeRoot_ = std::move(rootNode);
    workspaceScriptFiles_ = std::move(scriptFiles);
    workspaceScanError_ = std::move(scanError);
    nextWorkspaceScanTime_ = now + std::max(static_cast<double>(deltaTime), 1.0);
  }

  void Editor::invalidate_workspace_cache()
  {
    workspaceTreeRoot_.reset();
    workspaceScanError_.clear();
    nextWorkspaceScanTime_ = 0.0;
  }

  void Editor::request_workspace_item_creation(WorkspaceCreateKind kind, const std::filesystem::path &parentPath)
  {
    pendingWorkspaceCreateKind_ = kind;
    pendingWorkspaceCreateParentPath_ = parentPath;
    workspaceCreateNameBuffer_.fill('\0');
    workspaceCreateError_.clear();
    openWorkspaceCreateDialog_ = true;
  }

  void Editor::request_workspace_item_import(const std::filesystem::path &destinationDirectory)
  {
    pendingWorkspaceImportParentPath_ = destinationDirectory;
    workspaceImportSourcePathBuffer_.fill('\0');
    workspaceImportError_.clear();
    openWorkspaceImportDialog_ = true;
  }

  void Editor::request_workspace_item_deletion(const std::filesystem::path &targetPath)
  {
    pendingWorkspaceDeletePath_ = targetPath;
    workspaceDeleteError_.clear();
    openWorkspaceDeleteDialog_ = true;
  }

  void Editor::render_workspace_create_dialog()
  {
    if (openWorkspaceCreateDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_CREATE_POPUP_TITLE);
      openWorkspaceCreateDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_CREATE_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    const bool creatingScript = pendingWorkspaceCreateKind_ == WorkspaceCreateKind::Script;
    ImGui::TextWrapped("%s", creatingScript ? "New Script" : "New Folder");
    ImGui::TextWrapped("%s", pendingWorkspaceCreateParentPath_.string().c_str());
    ImGui::InputText(creatingScript ? "Script Name" : "Folder Name", workspaceCreateNameBuffer_.data(), workspaceCreateNameBuffer_.size());

    if (!workspaceCreateError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceCreateError_.c_str());
    }

    if (ImGui::Button(creatingScript ? "Create Script" : "Create Folder"))
    {
      std::string errorMessage;
      if (create_workspace_item(
              pendingWorkspaceCreateParentPath_,
              std::string(workspaceCreateNameBuffer_.data()),
              pendingWorkspaceCreateKind_,
              &errorMessage))
      {
        workspaceCreateError_.clear();
        pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
        pendingWorkspaceCreateParentPath_.clear();
        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceCreateError_ = std::move(errorMessage);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceCreateError_.clear();
      pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
      pendingWorkspaceCreateParentPath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::render_workspace_import_dialog()
  {
    if (openWorkspaceImportDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_IMPORT_POPUP_TITLE);
      openWorkspaceImportDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_IMPORT_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    ImGui::TextWrapped("Import File");
    ImGui::TextWrapped("%s", pendingWorkspaceImportParentPath_.string().c_str());
    ImGui::InputText("Source File", workspaceImportSourcePathBuffer_.data(), workspaceImportSourcePathBuffer_.size());

    if (ImGui::Button("Browse File..."))
    {
      std::string pickerError;
      const auto pickedFile = hades::pick_file_with_native_dialog("Select a file to import", &pickerError);
      if (pickedFile.has_value())
      {
        set_buffer_text(workspaceImportSourcePathBuffer_, pickedFile->string());
        workspaceImportError_.clear();
      }
      else if (!pickerError.empty())
      {
        workspaceImportError_ = std::move(pickerError);
      }
    }

    if (!workspaceImportError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceImportError_.c_str());
    }

    const bool canImportFile = !trim(workspaceImportSourcePathBuffer_.data()).empty();
    if (!canImportFile)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Import"))
    {
      std::string errorMessage;
      if (copy_file_to_directory(
              std::filesystem::path(workspaceImportSourcePathBuffer_.data()),
              pendingWorkspaceImportParentPath_,
              nullptr,
              &errorMessage))
      {
        workspaceImportError_.clear();
        pendingWorkspaceImportParentPath_.clear();
        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceImportError_ = std::move(errorMessage);
      }
    }
    if (!canImportFile)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceImportError_.clear();
      pendingWorkspaceImportParentPath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::render_workspace_delete_dialog(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (openWorkspaceDeleteDialog_)
    {
      ImGui::OpenPopup(WORKSPACE_DELETE_POPUP_TITLE);
      openWorkspaceDeleteDialog_ = false;
    }

    if (!ImGui::BeginPopupModal(WORKSPACE_DELETE_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    std::error_code errorCode;
    const bool deletingDirectory = std::filesystem::is_directory(pendingWorkspaceDeletePath_, errorCode);

    ImGui::TextWrapped("%s", deletingDirectory ? "Delete Folder" : "Delete File");
    ImGui::TextWrapped("%s", pendingWorkspaceDeletePath_.string().c_str());

    if (!workspaceDeleteError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceDeleteError_.c_str());
    }

    if (ImGui::Button(workspace_delete_button_label(pendingWorkspaceDeletePath_).c_str()))
    {
      WorkspaceDeleteResult deleteResult;
      std::string errorMessage;
      if (delete_workspace_item(
              activeWorkspacePath_,
              pendingWorkspaceDeletePath_,
              entityManager,
              componentManager,
              &deleteResult,
              &errorMessage))
      {
        for (const auto &relativeScriptPath : deleteResult.removedScriptPaths)
        {
          scriptModTimes_.erase(relativeScriptPath);
          const std::string pathKey = (activeWorkspacePath_ / relativeScriptPath).string();
          parsedFieldsCache_.erase(pathKey);
          parsedFieldsModTimes_.erase(pathKey);
        }

        workspaceDeleteError_.clear();
        const std::filesystem::path deletedPath = pendingWorkspaceDeletePath_;
        pendingWorkspaceDeletePath_.clear();

        if (activeScriptEditorPath_.has_value() &&
            path_is_same_or_within(deletedPath, *activeScriptEditorPath_))
        {
          activeScriptEditorPath_.reset();
          activeScriptEditorRelativePath_.clear();
          activeScriptEditorContents_.clear();
          activeScriptEditorSavedWriteTime_.reset();
          activeScriptEditorDirty_ = false;
          scriptEditorStatusMessage_ = "Closed the script that was deleted from the workspace.";
          scriptEditorStatusIsError_ = false;
        }

        if (pendingScriptEditorPath_.has_value() &&
            path_is_same_or_within(deletedPath, *pendingScriptEditorPath_))
        {
          pendingScriptEditorPath_.reset();
          pendingScriptEditorRelativePath_.clear();
          openScriptEditorUnsavedChangesDialog_ = false;
        }

        invalidate_workspace_cache();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        workspaceDeleteError_ = std::move(errorMessage);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      workspaceDeleteError_.clear();
      pendingWorkspaceDeletePath_.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::render_workspace_tree_node(const WorkspaceTreeNode &node)
  {
    const std::string label = path_display_name(node.path);
    const std::string treeNodeId = label + "##" + node.path.string();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!node.directory || node.children.empty())
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (!node.directory &&
        activeScriptEditorPath_.has_value() &&
        activeScriptEditorPath_->lexically_normal() == node.path.lexically_normal())
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open = ImGui::TreeNodeEx(treeNodeId.c_str(), flags);
    if (!node.directory &&
        node.path.extension() == ".cs" &&
        ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
      request_script_editor_open(node.path, relative_workspace_path(activeWorkspacePath_, node.path));
    }

    if (ImGui::BeginPopupContextItem())
    {
      const std::filesystem::path destinationDirectory =
          node.directory ? node.path : node.path.parent_path();
      const bool isWorkspaceRoot = node.path == activeWorkspacePath_;

      if (ImGui::MenuItem("New Folder"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Folder, destinationDirectory);
      }
      if (ImGui::MenuItem("New Script"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Script, destinationDirectory);
      }
      if (ImGui::MenuItem("Import"))
      {
        request_workspace_item_import(destinationDirectory);
      }
      if (!node.directory && node.path.extension() == ".cs")
      {
        if (ImGui::MenuItem("Open in Script Editor"))
        {
          request_script_editor_open(node.path, relative_workspace_path(activeWorkspacePath_, node.path));
        }
      }

      if (!isWorkspaceRoot)
      {
        ImGui::Separator();
        if (ImGui::MenuItem(workspace_delete_button_label(node.path).c_str()))
        {
          request_workspace_item_deletion(node.path);
        }
      }
      ImGui::EndPopup();
    }

    if (!node.directory || !open || node.children.empty())
    {
      return;
    }

    for (const auto &child : node.children)
    {
      render_workspace_tree_node(child);
    }

    ImGui::TreePop();
  }

  void Editor::request_script_editor_open(
      const std::filesystem::path &scriptPath,
      const std::string &relativePath)
  {
    if (scriptPath.extension() != ".cs")
    {
      return;
    }

    const std::filesystem::path normalizedPath = scriptPath.lexically_normal();
    if (activeScriptEditorPath_.has_value() &&
        activeScriptEditorPath_->lexically_normal() == normalizedPath)
    {
      focusScriptEditorWindow_ = true;
      return;
    }

    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;

    if (activeScriptEditorDirty_)
    {
      pendingScriptEditorPath_ = normalizedPath;
      pendingScriptEditorRelativePath_ = relativePath;
      openScriptEditorUnsavedChangesDialog_ = true;
      return;
    }

    std::string errorMessage;
    if (!open_script_document(normalizedPath, relativePath, &errorMessage))
    {
      scriptEditorStatusMessage_ = std::move(errorMessage);
      scriptEditorStatusIsError_ = true;
    }
  }

  bool Editor::open_script_document(
      const std::filesystem::path &scriptPath,
      const std::string &relativePath,
      std::string *errorMessage)
  {
    ScriptDocumentSnapshot snapshot;
    if (!load_script_document(scriptPath, snapshot, errorMessage))
    {
      return false;
    }

    activeScriptEditorPath_ = scriptPath;
    activeScriptEditorRelativePath_ = relativePath;
    activeScriptEditorContents_ = std::move(snapshot.contents);
    if (snapshot.hasLastWriteTime)
    {
      activeScriptEditorSavedWriteTime_ = snapshot.lastWriteTime;
    }
    else
    {
      activeScriptEditorSavedWriteTime_.reset();
    }
    activeScriptEditorDirty_ = false;
    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;
    focusScriptEditorWindow_ = true;
    return true;
  }

  bool Editor::save_active_script_document(bool triggerCompile, std::string *errorMessage)
  {
    if (!activeScriptEditorPath_.has_value())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No script is currently open in the editor.";
      }
      return false;
    }

    ScriptDocumentSnapshot snapshot;
    if (!save_script_document(*activeScriptEditorPath_, activeScriptEditorContents_, &snapshot, errorMessage))
    {
      return false;
    }

    activeScriptEditorDirty_ = false;
    if (snapshot.hasLastWriteTime)
    {
      activeScriptEditorSavedWriteTime_ = snapshot.lastWriteTime;
      if (!activeScriptEditorRelativePath_.empty())
      {
        scriptModTimes_[activeScriptEditorRelativePath_] = snapshot.lastWriteTime;
      }
    }
    else
    {
      activeScriptEditorSavedWriteTime_.reset();
    }

    scriptEditorStatusMessage_ = "Saved " +
        (activeScriptEditorRelativePath_.empty() ? activeScriptEditorPath_->filename().string() : activeScriptEditorRelativePath_) +
        ".";
    scriptEditorStatusIsError_ = false;

    if (triggerCompile)
    {
      queue_workspace_script_compile();
    }

    return true;
  }

  void Editor::queue_workspace_script_compile()
  {
    if (activeWorkspacePath_.empty())
    {
      return;
    }

    if (workspaceScriptFiles_.empty())
    {
      lastCompileError_.clear();
      lastCompileSucceeded_ = true;
      workspaceScriptListDirty_ = false;
      return;
    }

    if (backgroundCompileInProgress_)
    {
      workspaceScriptListDirty_ = true;
      return;
    }

    std::vector<std::filesystem::path> sourceFiles;
    sourceFiles.reserve(workspaceScriptFiles_.size());
    for (const auto &relPath : workspaceScriptFiles_)
    {
      sourceFiles.push_back(activeWorkspacePath_ / relPath);
    }

    backgroundCompileInProgress_ = true;
    workspaceScriptListDirty_ = false;
    backgroundCompileResult_ = std::async(std::launch::async,
        [files = std::move(sourceFiles)]() -> std::string
        {
          std::string error;
          ScriptRuntime::compile(files, &error);
          return error;
        });
  }

  void Editor::render_script_editor()
  {
    if (focusScriptEditorWindow_)
    {
      ImGui::SetNextWindowFocus();
    }

    ImGui::Begin(SCRIPT_EDITOR_WINDOW_TITLE);
    focusScriptEditorWindow_ = false;

    if (openScriptEditorUnsavedChangesDialog_)
    {
      ImGui::OpenPopup(SCRIPT_EDITOR_UNSAVED_POPUP_TITLE);
      openScriptEditorUnsavedChangesDialog_ = false;
    }

    if (ImGui::BeginPopupModal(SCRIPT_EDITOR_UNSAVED_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      const std::string label =
          activeScriptEditorRelativePath_.empty() ? "the current script" : activeScriptEditorRelativePath_;
      ImGui::TextWrapped("Save changes to %s before opening another script?", label.c_str());

      if (scriptEditorStatusIsError_ && !scriptEditorStatusMessage_.empty())
      {
        ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", scriptEditorStatusMessage_.c_str());
      }

      if (ImGui::Button("Save and Open"))
      {
        std::string errorMessage;
        if (save_active_script_document(true, &errorMessage) &&
            pendingScriptEditorPath_.has_value() &&
            open_script_document(*pendingScriptEditorPath_, pendingScriptEditorRelativePath_, &errorMessage))
        {
          pendingScriptEditorPath_.reset();
          pendingScriptEditorRelativePath_.clear();
          ImGui::CloseCurrentPopup();
        }
        else
        {
          scriptEditorStatusMessage_ = std::move(errorMessage);
          scriptEditorStatusIsError_ = true;
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Discard Changes"))
      {
        std::string errorMessage;
        if (pendingScriptEditorPath_.has_value() &&
            open_script_document(*pendingScriptEditorPath_, pendingScriptEditorRelativePath_, &errorMessage))
        {
          pendingScriptEditorPath_.reset();
          pendingScriptEditorRelativePath_.clear();
          ImGui::CloseCurrentPopup();
        }
        else
        {
          scriptEditorStatusMessage_ = std::move(errorMessage);
          scriptEditorStatusIsError_ = true;
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        pendingScriptEditorPath_.reset();
        pendingScriptEditorRelativePath_.clear();
        scriptEditorStatusMessage_.clear();
        scriptEditorStatusIsError_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (!activeScriptEditorPath_.has_value())
    {
      ImGui::TextDisabled("Select a .cs file in the Workspace panel to start editing.");
      if (!scriptEditorStatusMessage_.empty())
      {
        const ImVec4 color = scriptEditorStatusIsError_
                                 ? ImVec4(0.88f, 0.42f, 0.42f, 1.0f)
                                 : ImVec4(0.42f, 0.88f, 0.42f, 1.0f);
        ImGui::TextColored(color, "%s", scriptEditorStatusMessage_.c_str());
      }
      ImGui::End();
      return;
    }

    ImGui::TextWrapped("%s", activeScriptEditorRelativePath_.empty()
                               ? activeScriptEditorPath_->string().c_str()
                               : activeScriptEditorRelativePath_.c_str());
    ImGui::Separator();

    const bool canSave = activeScriptEditorDirty_;
    if (!canSave)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save"))
    {
      std::string errorMessage;
      if (!save_active_script_document(true, &errorMessage))
      {
        scriptEditorStatusMessage_ = std::move(errorMessage);
        scriptEditorStatusIsError_ = true;
      }
    }
    if (!canSave)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Compile Workspace"))
    {
      std::string errorMessage;
      bool compileQueuedBySave = false;
      if (activeScriptEditorDirty_)
      {
        if (!save_active_script_document(true, &errorMessage))
        {
          scriptEditorStatusMessage_ = std::move(errorMessage);
          scriptEditorStatusIsError_ = true;
        }
        else
        {
          compileQueuedBySave = true;
        }
      }

      if (errorMessage.empty() && !compileQueuedBySave)
      {
        queue_workspace_script_compile();
      }
    }

    ImGui::SameLine();
    if (!canSave)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Revert"))
    {
      std::string errorMessage;
      if (!open_script_document(*activeScriptEditorPath_, activeScriptEditorRelativePath_, &errorMessage))
      {
        scriptEditorStatusMessage_ = std::move(errorMessage);
        scriptEditorStatusIsError_ = true;
      }
      else
      {
        scriptEditorStatusMessage_ = "Reverted unsaved changes.";
        scriptEditorStatusIsError_ = false;
      }
    }
    if (!canSave)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", activeScriptEditorDirty_ ? "Unsaved changes" : "Saved");

    if (!scriptEditorStatusMessage_.empty())
    {
      const ImVec4 color = scriptEditorStatusIsError_
                               ? ImVec4(0.88f, 0.42f, 0.42f, 1.0f)
                               : ImVec4(0.42f, 0.88f, 0.42f, 1.0f);
      ImGui::TextColored(color, "%s", scriptEditorStatusMessage_.c_str());
    }

    if (backgroundCompileInProgress_)
    {
      ImGui::TextDisabled("Compiling scripts...");
    }
    else if (!lastCompileSucceeded_)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", lastCompileError_.c_str());
    }
    else if (!workspaceScriptFiles_.empty())
    {
      ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Workspace scripts compiled successfully.");
    }

    ImGui::Separator();
    ImVec2 editorSize = ImGui::GetContentRegionAvail();
    editorSize.y = std::max(editorSize.y, 200.0f);
    if (ImGui::InputTextMultiline(
            "##ScriptEditorContents",
            &activeScriptEditorContents_,
            editorSize,
            ImGuiInputTextFlags_AllowTabInput))
    {
      activeScriptEditorDirty_ = true;
      if (!scriptEditorStatusIsError_)
      {
        scriptEditorStatusMessage_.clear();
      }
    }

    ImGui::End();
  }

  void Editor::workspace(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(WORKSPACE_WINDOW_TITLE);
    render_workspace_create_dialog();
    render_workspace_import_dialog();
    render_workspace_delete_dialog(entityManager, componentManager);

    if (activeWorkspacePath_.empty())
    {
      ImGui::TextDisabled("No workspace.");
      ImGui::End();
      return;
    }

    ImGui::TextWrapped("%s", activeWorkspacePath_.string().c_str());
    ImGui::Separator();

    if (!workspaceScanError_.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", workspaceScanError_.c_str());
      ImGui::Separator();
    }

    if (!workspaceTreeRoot_.has_value())
    {
      ImGui::TextDisabled("No files.");
      ImGui::End();
      return;
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    render_workspace_tree_node(*workspaceTreeRoot_);
    if (ImGui::BeginPopupContextWindow("WorkspaceRootContext", ImGuiPopupFlags_NoOpenOverItems))
    {
      if (ImGui::MenuItem("New Folder"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Folder, activeWorkspacePath_);
      }
      if (ImGui::MenuItem("New Script"))
      {
        request_workspace_item_creation(WorkspaceCreateKind::Script, activeWorkspacePath_);
      }
      if (ImGui::MenuItem("Import"))
      {
        request_workspace_item_import(activeWorkspacePath_);
      }
      ImGui::EndPopup();
    }
    ImGui::End();
  }

  void Editor::ensure_world_state(EntityManager &entityManager, ComponentManager &componentManager)
  {
    const auto defaultWorld = normalize_default_world(entityManager, componentManager);
    if (!defaultWorld.has_value())
    {
      const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
      load_world(world, componentManager);
      state.playModeMessage.clear();
      return;
    }

    if (!state.loadedWorld.has_value() || !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      load_world(*defaultWorld, componentManager);
      return;
    }

    if (state.selectedEntity.has_value())
    {
      const auto selectedWorld = world_for_entity(*state.selectedEntity, componentManager);
      if (!selectedWorld.has_value() || *selectedWorld != *state.loadedWorld)
      {
        state.selectedEntity = *state.loadedWorld;
      }
    }
  }

  Entity::EntityId Editor::create_world(EntityManager &entityManager, ComponentManager &componentManager)
  {
    const std::size_t worldIndex = find_world_entities(entityManager, componentManager).size() + 1U;
    const bool shouldBeDefault = !find_default_world(entityManager, componentManager).has_value();
    const auto world = EntityFactory::createWorld(
        entityManager,
        componentManager,
        "World" + std::to_string(worldIndex),
        shouldBeDefault);
    if (shouldBeDefault)
    {
      set_default_world(world, entityManager, componentManager);
    }

    load_world(world, componentManager);
    state.playModeMessage.clear();
    return world;
  }

  void Editor::load_world(Entity::EntityId world, ComponentManager &componentManager)
  {
    state.loadedWorld = world;
    state.selectedEntity = world;
  }

  void Editor::set_default_world(
      Entity::EntityId world,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    hades::set_default_world(entityManager, componentManager, world);
    state.playModeMessage.clear();
  }

  void Editor::request_entity_creation(EditorEntityPreset preset, Entity::EntityId parent)
  {
    state.selectedEntity = parent;
    state.pendingEntityPreset = preset;
  }

  void Editor::request_model_import(Entity::EntityId parent)
  {
    state.selectedEntity = parent;
    if (importModelPathBuffer[0] == '\0')
    {
      std::snprintf(
          importModelPathBuffer.data(),
          importModelPathBuffer.size(),
          "%s",
          "src/tests/backpack/12305_backpack_v2_l3.obj");
    }

    importModelError.clear();
    openImportModelDialog = true;
  }

  void Editor::request_entity_deletion(Entity::EntityId entity)
  {
    pendingEntityDeletion_ = entity;
  }

  void Editor::handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (state.pendingEntityPreset == EditorEntityPreset::None)
    {
      return;
    }

    const auto parent = get_selected_parent(entityManager, componentManager);
    Entity::EntityId createdEntity = Entity::INVALID;

    switch (state.pendingEntityPreset)
    {
    case EditorEntityPreset::Camera:
      createdEntity = EntityFactory::createCamera(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::Cube:
      createdEntity = EntityFactory::createCube(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::AudioEmitter:
      createdEntity = EntityFactory::createAudioEmitter(entityManager, componentManager, parent);
      break;
    case EditorEntityPreset::None:
      break;
    }

    if (createdEntity != Entity::INVALID)
    {
      state.selectedEntity = createdEntity;
      state.playModeMessage.clear();
    }

    state.pendingEntityPreset = EditorEntityPreset::None;
  }

  void Editor::handle_entity_deletion_requests(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    if (!pendingEntityDeletion_.has_value())
    {
      return;
    }

    const Entity::EntityId entity = *pendingEntityDeletion_;
    pendingEntityDeletion_.reset();

    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    if (state.isPlaying)
    {
      stop_play_mode(scriptRuntime);
      state.playModeMessage = "Play mode stopped because an entity hierarchy was deleted.";
    }

    const auto hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    const std::optional<Entity::EntityId> fallbackParent =
        hierarchy.parent.has_value() &&
                componentManager.hasComponent<TransformHierarchyComponent>(*hierarchy.parent)
            ? hierarchy.parent
            : std::nullopt;
    const bool deletedSelectedEntity =
        state.selectedEntity.has_value() &&
        hierarchy_contains_entity(entity, *state.selectedEntity, componentManager);
    const bool deletedLoadedWorld =
        state.loadedWorld.has_value() &&
        hierarchy_contains_entity(entity, *state.loadedWorld, componentManager);

    destroy_entity_subtree(entity, entityManager, componentManager);

    const auto defaultWorld = normalize_default_world(entityManager, componentManager);
    if (deletedLoadedWorld || !state.loadedWorld.has_value() ||
        !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      if (defaultWorld.has_value())
      {
        load_world(*defaultWorld, componentManager);
      }
      else
      {
        state.loadedWorld.reset();
        state.selectedEntity.reset();
      }
    }
    else if (deletedSelectedEntity)
    {
      if (fallbackParent.has_value())
      {
        state.selectedEntity = fallbackParent;
      }
      else
      {
        state.selectedEntity = state.loadedWorld;
      }
    }
  }

  void Editor::import_model(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (openImportModelDialog)
    {
      ImGui::OpenPopup(IMPORT_MODEL_POPUP_TITLE);
      openImportModelDialog = false;
    }

    if (!ImGui::BeginPopupModal(IMPORT_MODEL_POPUP_TITLE, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    ImGui::TextWrapped("Import Model");
    ImGui::InputText("Path", importModelPathBuffer.data(), importModelPathBuffer.size());

    if (!importModelError.empty())
    {
      ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", importModelError.c_str());
    }

    if (ImGui::Button("Import"))
    {
      std::string errorMessage;
      const auto parent = get_selected_parent(entityManager, componentManager);
      const auto createdEntity = EntityFactory::createImportedModel(
          entityManager,
          componentManager,
          std::filesystem::path(importModelPathBuffer.data()),
          parent,
          &errorMessage);

      if (createdEntity.has_value())
      {
        state.selectedEntity = *createdEntity;
        importModelError.clear();
        ImGui::CloseCurrentPopup();
      }
      else
      {
        importModelError = errorMessage.empty() ? "Model import failed." : errorMessage;
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      importModelError.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  void Editor::handle_play_mode_requests(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    switch (state.pendingPlayAction)
    {
    case EditorPlayAction::None:
      return;
    case EditorPlayAction::Start:
      start_play_mode(entityManager, componentManager, scriptRuntime);
      break;
    case EditorPlayAction::Stop:
      stop_play_mode(scriptRuntime);
      break;
    }

    state.pendingPlayAction = EditorPlayAction::None;
  }

  void Editor::start_play_mode(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    const auto startupWorld = normalize_default_world(entityManager, componentManager);
    if (!startupWorld.has_value())
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      return;
    }

    const auto selection = select_main_camera(entityManager, componentManager, startupWorld);
    if (selection.status != MainCameraSelectionStatus::Ready || !selection.entity.has_value())
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      return;
    }

    std::string scriptError;
    if (!scriptRuntime.start(componentManager, entityManager, activeWorkspacePath_, startupWorld, &scriptError))
    {
      state.isPlaying = false;
      state.activeWorld.reset();
      state.activeCamera.reset();
      state.playModeMessage = scriptError;
      return;
    }

    state.isPlaying = true;
    state.activeWorld = startupWorld;
    state.activeCamera = selection.entity;
    state.playModeMessage.clear();
  }

  void Editor::stop_play_mode(ScriptRuntime &scriptRuntime)
  {
    scriptRuntime.stop();
    state.isPlaying = false;
    state.activeWorld.reset();
    state.activeCamera.reset();
    state.playModeMessage.clear();
  }

  void Editor::set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager)
  {
    const auto targetWorld = world_for_entity(entity, componentManager);
    for (Entity::EntityId current : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<CameraComponent>(current))
      {
        continue;
      }

      if (targetWorld.has_value() && world_for_entity(current, componentManager) != targetWorld)
      {
        continue;
      }

      auto &camera = componentManager.getComponent<CameraComponent>(current);
      camera.isMainCamera = (current == entity);
    }
  }

  std::optional<Entity::EntityId> Editor::get_selected_parent(
      EntityManager &entityManager,
      ComponentManager &componentManager) const
  {
    if (state.selectedEntity.has_value() &&
        componentManager.hasComponent<TransformHierarchyComponent>(*state.selectedEntity))
    {
      const auto selectedWorld = world_for_entity(*state.selectedEntity, componentManager);
      if (selectedWorld.has_value() &&
          state.loadedWorld.has_value() &&
          *selectedWorld == *state.loadedWorld)
      {
        return state.selectedEntity;
      }
    }

    if (state.loadedWorld.has_value())
    {
      return state.loadedWorld;
    }

    return find_default_world(entityManager, componentManager);
  }

  std::string Editor::entity_label(Entity::EntityId entity, ComponentManager &componentManager) const
  {
    return entity_display_label(entity, componentManager);
  }

  void Editor::entities(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(ENTITY_WINDOW_TITLE);

    render_hierarchies(entityManager, componentManager);

    ImGui::End();
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

    const int visibleRenderableCount = draw_world_preview(
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

  void Editor::properties(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(PROPERTIES_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      ImGui::TextDisabled("No selection.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    const bool isWorld = componentManager.hasComponent<WorldComponent>(entity);
    ImGui::Text("%s %u", isWorld ? "World" : "Entity", entity);
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      auto &name = componentManager.getComponent<NameComponent>(entity);
      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", name.value.c_str());
      if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
      {
        name.value = nameBuffer.data();
      }
    }
    else
    {
      ImGui::TextDisabled("Unnamed");
    }

    if (isWorld)
    {
      const auto &world = componentManager.getComponent<WorldComponent>(entity);
      ImGui::Text("Startup World: %s", world.isDefault ? "Yes" : "No");
    }

    ImGui::End();
  }

  void Editor::components(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(COMPONENTS_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      ImGui::TextDisabled("No selection.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    const bool isWorld = componentManager.hasComponent<WorldComponent>(entity);

    ImGui::Text("%s %u", isWorld ? "World" : "Entity", entity);
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      ImGui::TextDisabled("%s", componentManager.getComponent<NameComponent>(entity).value.c_str());
    }
    ImGui::Separator();

    if (isWorld)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add Script Component"))
    {
      if (!componentManager.hasComponent<ScriptComponent>(entity))
      {
        ScriptComponent scriptComponent;
        scriptComponent.attachments.push_back(ScriptAttachment());
        componentManager.addComponent(entity, scriptComponent);
      }
      else
      {
        componentManager.getComponent<ScriptComponent>(entity).attachments.push_back(ScriptAttachment());
      }
    }
    if (isWorld)
    {
      ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (componentManager.hasComponent<WorldComponent>(entity) && ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen))
    {
      const auto &world = componentManager.getComponent<WorldComponent>(entity);
      ImGui::Text("Startup World: %s", world.isDefault ? "Yes" : "No");
      if (!world.isDefault && ImGui::Button("Set As Default World"))
      {
        set_default_world(entity, entityManager, componentManager);
      }
    }

    if (componentManager.hasComponent<PositionComponent3D>(entity) && ImGui::CollapsingHeader("Transform"))
    {
      auto &position = componentManager.getComponent<PositionComponent3D>(entity);
      ImGui::DragFloat3("Position", &position.x, 0.1f);
    }

    if (componentManager.hasComponent<CameraComponent>(entity) && ImGui::CollapsingHeader("Camera"))
    {
      auto &camera = componentManager.getComponent<CameraComponent>(entity);
      bool isMainCamera = camera.isMainCamera;

      if (ImGui::Checkbox("Main Camera", &isMainCamera))
      {
        if (isMainCamera)
        {
          set_main_camera(entity, entityManager, componentManager);
        }
        else
        {
          camera.isMainCamera = false;
        }

        state.playModeMessage.clear();
      }

      ImGui::DragFloat("Field of view", &camera.fovY, 0.5f, 1.0f, 179.0f);
      ImGui::DragFloat("Near clip", &camera.nearClip, 0.01f, 0.001f, camera.farClip);
      ImGui::DragFloat("Far clip", &camera.farClip, 1.0f, camera.nearClip, 10000.0f);

      if (camera.fovY < 1.0f)
      {
        camera.fovY = 1.0f;
      }
      else if (camera.fovY > 179.0f)
      {
        camera.fovY = 179.0f;
      }
      if (camera.nearClip < 0.001f)
      {
        camera.nearClip = 0.001f;
      }
      if (camera.farClip <= camera.nearClip)
      {
        camera.farClip = camera.nearClip + 0.001f;
      }
    }

    if (componentManager.hasComponent<AudioListenerComponent>(entity) && ImGui::CollapsingHeader("Audio Listener"))
    {
      auto &listener = componentManager.getComponent<AudioListenerComponent>(entity);
      ImGui::Checkbox("Listener Enabled", &listener.enabled);
      ImGui::DragFloat3("Listener Forward", &listener.forwardX, 0.01f, -1.0f, 1.0f);
      ImGui::DragFloat3("Listener Up", &listener.upX, 0.01f, -1.0f, 1.0f);
    }

    if (componentManager.hasComponent<PrimitiveComponent>(entity) && ImGui::CollapsingHeader("Primitive"))
    {
      const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
      ImGui::Text("Type: %s", primitive_type_label(primitive.type));
    }

    if (componentManager.hasComponent<AudioSourceComponent>(entity) && ImGui::CollapsingHeader("Audio Source"))
    {
      auto &source = componentManager.getComponent<AudioSourceComponent>(entity);
      std::array<char, 260> assetPathBuffer{};
      std::snprintf(assetPathBuffer.data(), assetPathBuffer.size(), "%s", source.assetPath.c_str());

      if (ImGui::InputText("Audio Clip", assetPathBuffer.data(), assetPathBuffer.size()))
      {
        source.assetPath = assetPathBuffer.data();
      }

      int selectedBus = static_cast<int>(source.bus);
      const char *busLabels[] = {"Master", "Music", "SFX", "Voice"};
      if (ImGui::Combo("Audio Bus", &selectedBus, busLabels, IM_ARRAYSIZE(busLabels)))
      {
        source.bus = static_cast<AudioBus>(selectedBus);
      }

      ImGui::Checkbox("Play On Start", &source.playOnStart);
      ImGui::Checkbox("Looping", &source.looping);
      ImGui::Checkbox("Streaming", &source.streaming);
      ImGui::Checkbox("Spatialized", &source.spatialized);
      ImGui::SliderFloat("Volume", &source.volume, 0.0f, 2.0f);
      ImGui::SliderFloat("Pitch", &source.pitch, 0.1f, 4.0f);

      if (source.spatialized)
      {
        ImGui::DragFloat("Min Distance", &source.minDistance, 0.1f, 0.1f, 1000.0f);
        ImGui::DragFloat("Max Distance", &source.maxDistance, 0.5f, source.minDistance, 5000.0f);
        ImGui::SliderFloat("Rolloff", &source.rolloff, 0.1f, 4.0f);
      }

      if (source.pitch < 0.1f)
      {
        source.pitch = 0.1f;
      }
      if (source.minDistance < 0.1f)
      {
        source.minDistance = 0.1f;
      }
      if (source.maxDistance < source.minDistance)
      {
        source.maxDistance = source.minDistance;
      }
      if (source.rolloff < 0.1f)
      {
        source.rolloff = 0.1f;
      }

      ImGui::TextDisabled("Audio paths resolve relative to the engine process working directory.");
    }

    if (componentManager.hasComponent<ModelComponent>(entity) && ImGui::CollapsingHeader("Imported Model"))
    {
      const auto &modelComponent = componentManager.getComponent<ModelComponent>(entity);
      const auto &model = modelComponent.model;

      ImGui::TextWrapped("%s", model.sourcePath.c_str());
      ImGui::Text("Format: %s", model.formatHint.empty() ? "Unknown" : model.formatHint.c_str());
      ImGui::Text("Meshes: %zu", model.meshes.size());
      ImGui::Text("Materials: %zu", model.materials.size());
      ImGui::Text("Vertices: %zu", model.totalVertexCount);
      ImGui::Text("Faces: %zu", model.totalFaceCount);

      if (ImGui::CollapsingHeader("Mesh Details"))
      {
        for (std::size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
          const auto &mesh = model.meshes[meshIndex];
          ImGui::PushID(static_cast<int>(meshIndex));
          ImGui::SeparatorText(mesh.name.c_str());
          ImGui::Text("Vertices: %zu", mesh.vertexCount);
          ImGui::Text("Faces: %zu", mesh.faceCount);
          ImGui::Text("Material Slot: %zu", mesh.materialIndex);
          ImGui::PopID();
        }
      }

      if (ImGui::CollapsingHeader("Materials"))
      {
        for (const auto &material : model.materials)
        {
          ImGui::BulletText("%s", material.name.c_str());
        }
      }
    }

    if (componentManager.hasComponent<RenderComponent>(entity) && ImGui::CollapsingHeader("Render"))
    {
      const auto &render = componentManager.getComponent<RenderComponent>(entity);
      ImGui::Text("Program: %d", render.program);
    }

    if (componentManager.hasComponent<TransformHierarchyComponent>(entity) &&
        ImGui::CollapsingHeader("Transform Hierarchy"))
    {
      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);

      if (hierarchy.parent.has_value())
      {
        const Entity::EntityId parent = *hierarchy.parent;
        if (ImGui::Selectable(entity_label(parent, componentManager).c_str(), false))
        {
          state.selectedEntity = parent;
        }
      }
      else
      {
        ImGui::TextDisabled("%s", isWorld ? "World Root" : "Parent: Root");
      }

      if (hierarchy.children.empty())
      {
        ImGui::TextDisabled("No child entities.");
      }
      else
      {
        ImGui::Text("Children (%zu)", hierarchy.children.size());
        for (const Entity::EntityId child : hierarchy.children)
        {
          if (ImGui::Selectable(entity_label(child, componentManager).c_str(), false))
          {
            state.selectedEntity = child;
          }
        }
      }
    }

    if (componentManager.hasComponent<ScriptComponent>(entity))
    {
      auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
      std::optional<std::size_t> removeAttachmentIndex;

      if (scriptComponent.attachments.empty() && ImGui::CollapsingHeader("Scripts"))
      {
        ImGui::TextDisabled("No script components attached.");
      }

      for (std::size_t index = 0; index < scriptComponent.attachments.size(); ++index)
      {
        auto &attachment = scriptComponent.attachments[index];
        ImGui::PushID(static_cast<int>(index));

        const std::string label = script_component_label(attachment, index);
        if (ImGui::CollapsingHeader(label.c_str()))
        {
          std::array<char, 160> classBuffer{};
          std::snprintf(classBuffer.data(), classBuffer.size(), "%s", attachment.className.c_str());

          ImGui::Checkbox("Enabled", &attachment.enabled);
          std::vector<std::string> scriptOptions = workspaceScriptFiles_;
          if (!attachment.scriptPath.empty() &&
              std::find(scriptOptions.begin(), scriptOptions.end(), attachment.scriptPath) == scriptOptions.end())
          {
            scriptOptions.push_back(attachment.scriptPath);
            std::sort(scriptOptions.begin(), scriptOptions.end());
          }

          const std::string previousScriptPath = attachment.scriptPath;
          const std::string previewValue = attachment.scriptPath.empty() ? "<Select a workspace script>" : attachment.scriptPath;
          if (ImGui::BeginCombo("Script", previewValue.c_str()))
          {
            const bool noneSelected = attachment.scriptPath.empty();
            if (ImGui::Selectable("<None>", noneSelected))
            {
              attachment.scriptPath.clear();
            }
            if (noneSelected)
            {
              ImGui::SetItemDefaultFocus();
            }

            for (const auto &scriptPath : scriptOptions)
            {
              const bool selected = (attachment.scriptPath == scriptPath);
              if (ImGui::Selectable(scriptPath.c_str(), selected))
              {
                attachment.scriptPath = scriptPath;
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          if (previousScriptPath != attachment.scriptPath)
          {
            const std::string previousStem = std::filesystem::path(previousScriptPath).stem().string();
            if (attachment.className.empty() || attachment.className == previousStem)
            {
              attachment.className = std::filesystem::path(attachment.scriptPath).stem().string();
            }
          }

          if (scriptOptions.empty())
          {
            ImGui::TextDisabled("No scripts.");
          }
          else if (!attachment.scriptPath.empty())
          {
            ImGui::TextDisabled("%s", attachment.scriptPath.c_str());
            if (!activeWorkspacePath_.empty() && ImGui::Button("Open in Script Editor"))
            {
              request_script_editor_open(activeWorkspacePath_ / attachment.scriptPath, attachment.scriptPath);
            }
          }

          if (ImGui::InputText("Class Name", classBuffer.data(), classBuffer.size()))
          {
            attachment.className = classBuffer.data();
          }

          if (ImGui::Button("Use File Name"))
          {
            attachment.className = std::filesystem::path(attachment.scriptPath).stem().string();
          }
          ImGui::SameLine();
          if (ImGui::Button("Remove Script Component"))
          {
            removeAttachmentIndex = index;
          }

          // Display public fields parsed from the script file.
          if (!attachment.scriptPath.empty() && !activeWorkspacePath_.empty())
          {
            const auto resolvedPath = activeWorkspacePath_ / attachment.scriptPath;
            const std::string pathKey = resolvedPath.string();

            // Check if we need to re-parse (file changed or not yet cached).
            std::error_code modEc;
            const auto modTime = std::filesystem::last_write_time(resolvedPath, modEc);
            bool needsParse = false;
            if (!modEc)
            {
              auto modIt = parsedFieldsModTimes_.find(pathKey);
              if (modIt == parsedFieldsModTimes_.end() || modIt->second != modTime)
              {
                needsParse = true;
                parsedFieldsModTimes_[pathKey] = modTime;
              }
            }

            if (needsParse)
            {
              parsedFieldsCache_[pathKey] = parse_public_fields(resolvedPath);
            }

            auto cacheIt = parsedFieldsCache_.find(pathKey);
            if (cacheIt != parsedFieldsCache_.end() && !cacheIt->second.empty())
            {
              const auto &fields = cacheIt->second;

              // Remove stale entries from publicFieldValues.
              std::set<std::string> currentFieldNames;
              for (const auto &[type, name] : fields)
              {
                currentFieldNames.insert(name);
              }
              for (auto it = attachment.publicFieldValues.begin(); it != attachment.publicFieldValues.end();)
              {
                if (currentFieldNames.find(it->first) == currentFieldNames.end())
                {
                  it = attachment.publicFieldValues.erase(it);
                }
                else
                {
                  ++it;
                }
              }

              ImGui::Separator();
              ImGui::TextDisabled("Public Fields:");
              for (const auto &[type, name] : fields)
              {
                auto &value = attachment.publicFieldValues[name];
                std::array<char, 256> fieldBuffer{};
                std::snprintf(fieldBuffer.data(), fieldBuffer.size(), "%s", value.c_str());

                const std::string fieldLabel = name + " (" + type + ")";
                if (ImGui::InputText(fieldLabel.c_str(), fieldBuffer.data(), fieldBuffer.size()))
                {
                  value = fieldBuffer.data();
                }
              }
            }
          }
        }

        ImGui::PopID();
      }

      if (removeAttachmentIndex.has_value())
      {
        scriptComponent.attachments.erase(scriptComponent.attachments.begin() + static_cast<std::ptrdiff_t>(*removeAttachmentIndex));
      }

      if (backgroundCompileInProgress_)
      {
        ImGui::TextDisabled("Compiling scripts...");
      }
      else if (!lastCompileSucceeded_)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Compile error: %s", lastCompileError_.c_str());
        ImGui::PopStyleColor();
      }
      else if (!scriptComponent.attachments.empty())
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextDisabled("Scripts compiled successfully.");
        ImGui::PopStyleColor();
      }
    }

    ImGui::End();
  }

  void Editor::render_hierarchy(
      Entity::EntityId entity,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (state.selectedEntity.has_value() && *state.selectedEntity == entity)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (hierarchy.children.empty())
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const std::string label = entity_label(entity, componentManager);
    ImGui::PushID(static_cast<int>(entity));
    if (componentManager.hasComponent<WorldComponent>(entity))
    {
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    const bool open = ImGui::TreeNodeEx("entity", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked())
    {
      state.selectedEntity = entity;
    }

    if (ImGui::BeginPopupContextItem())
    {
      if (ImGui::BeginMenu("Add Child"))
      {
        if (ImGui::MenuItem("Camera"))
        {
          request_entity_creation(EditorEntityPreset::Camera, entity);
        }
        if (ImGui::MenuItem("Cube"))
        {
          request_entity_creation(EditorEntityPreset::Cube, entity);
        }
        if (ImGui::MenuItem("Audio Emitter"))
        {
          request_entity_creation(EditorEntityPreset::AudioEmitter, entity);
        }
        if (ImGui::MenuItem("Import Model..."))
        {
          request_model_import(entity);
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Delete Entity and Children"))
      {
        request_entity_deletion(entity);
      }

      ImGui::EndPopup();
    }

    if (open && !hierarchy.children.empty())
    {
      for (const auto &child : hierarchy.children)
      {
        render_hierarchy(child, entityManager, componentManager);
      }
      ImGui::TreePop();
    }

    ImGui::PopID();
  }

  void Editor::render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager)
  {
    (void)entityManager;

    if (!state.loadedWorld.has_value() || !componentManager.hasComponent<WorldComponent>(*state.loadedWorld))
    {
      ImGui::TextDisabled("No world.");
      return;
    }

    render_hierarchy(*state.loadedWorld, entityManager, componentManager);
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
