#include "editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "imgui.h"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/components/scale_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/profiling/frame_metrics.hpp"
#include "../engine/rendering/model_preview.hpp"
#include "../engine/rendering/scene_renderer.hpp"
#include "../engine/rendering/vector_text.hpp"

namespace hades
{
  namespace
  {
    constexpr char SCENE_WINDOW_TITLE[] = "World";
    constexpr char SETTINGS_WINDOW_TITLE[] = "Settings";
    constexpr char DEBUG_CONSOLE_WINDOW_TITLE[] = "Debug Console";
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
    constexpr float SCENE_PICK_THRESHOLD_PIXELS = 12.0f;
    constexpr float SCENE_MARKER_RADIUS = 4.0f;
    constexpr float SCENE_SELECTED_MARKER_RADIUS = 6.0f;
    constexpr float SCENE_GIZMO_MIN_AXIS_LENGTH = 0.85f;
    constexpr float SCENE_GIZMO_MAX_AXIS_LENGTH = 4.0f;
    constexpr float SCENE_GIZMO_AXIS_LENGTH_SCALE = 0.18f;
    constexpr float SCENE_GIZMO_HIT_THRESHOLD_PIXELS = 10.0f;
    constexpr float SCENE_GIZMO_MIN_SCREEN_LENGTH_PIXELS = 18.0f;
    constexpr float SCENE_GIZMO_ARROW_SIZE = 8.0f;
    constexpr int SCENE_GIZMO_RING_SEGMENTS = 64;
    constexpr float SCENE_GIZMO_RING_HIT_THRESHOLD_PIXELS = 8.0f;
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

    struct SceneHitCandidate
    {
      std::optional<Entity::EntityId> entity;
      float distanceSquared = std::numeric_limits<float>::max();
      float depth = std::numeric_limits<float>::max();
    };

    struct SceneRect
    {
      ImVec2 min;
      ImVec2 max;
    };

    struct SceneGizmoAxisProjection
    {
      SceneGizmoAxis axis = SceneGizmoAxis::None;
      Vec3 direction{0.0f, 0.0f, 0.0f};
      ImVec2 originScreen{};
      ImVec2 endScreen{};
      float pixelsPerWorldUnit = 0.0f;
      bool visible = false;
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

      if (componentManager.hasComponent<LightComponent>(entity))
      {
        name += " [Light]";
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

    Vec3 rotate_vec3_by_quaternion(const Vec3 &v, float qx, float qy, float qz, float qw)
    {
      const float tx = 2.0f * ((qy * v.z) - (qz * v.y));
      const float ty = 2.0f * ((qz * v.x) - (qx * v.z));
      const float tz = 2.0f * ((qx * v.y) - (qy * v.x));
      return make_vec3(
          v.x + (qw * tx) + ((qy * tz) - (qz * ty)),
          v.y + (qw * ty) + ((qz * tx) - (qx * tz)),
          v.z + (qw * tz) + ((qx * ty) - (qy * tx)));
    }

    Vec3 rotate_vec3_by_quaternion(const Vec3 &v, const RotationComponent3D &rot)
    {
      return rotate_vec3_by_quaternion(v, rot.qx, rot.qy, rot.qz, rot.qw);
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
        const ImVec2 &canvasSize,
        float cameraDistance)
    {
      constexpr int GRID_MIN_HALF_EXTENT = 12;
      constexpr int GRID_MAX_HALF_EXTENT = 200;
      constexpr float GRID_HEIGHT = 0.0f;
      constexpr ImU32 GRID_MINOR_COLOR = IM_COL32(128, 128, 128, 96);
      constexpr ImU32 GRID_AXIS_X_COLOR = IM_COL32(172, 172, 172, 168);
      constexpr ImU32 GRID_AXIS_Z_COLOR = IM_COL32(172, 172, 172, 168);

      // Compute a grid extent that covers the camera frustum at the grid plane.
      // Use the camera distance and FOV to estimate visible radius on the ground.
      const float halfFovRadians = degrees_to_radians(camera.fovY * 0.5f);
      const float tanHalfFov = std::tan(halfFovRadians);
      const float visibleRadius = cameraDistance * tanHalfFov * 2.0f;
      const int gridHalfExtent = std::clamp(
          static_cast<int>(std::ceil(visibleRadius)) + 2,
          GRID_MIN_HALF_EXTENT,
          GRID_MAX_HALF_EXTENT);

      for (int x = -gridHalfExtent; x <= gridHalfExtent; ++x)
      {
        ImVec2 screenStart;
        ImVec2 screenEnd;
        if (!project_line_segment(
                make_vec3(static_cast<float>(x), GRID_HEIGHT, -static_cast<float>(gridHalfExtent)),
                make_vec3(static_cast<float>(x), GRID_HEIGHT, static_cast<float>(gridHalfExtent)),
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

      for (int z = -gridHalfExtent; z <= gridHalfExtent; ++z)
      {
        ImVec2 screenStart;
        ImVec2 screenEnd;
        if (!project_line_segment(
                make_vec3(-static_cast<float>(gridHalfExtent), GRID_HEIGHT, static_cast<float>(z)),
                make_vec3(static_cast<float>(gridHalfExtent), GRID_HEIGHT, static_cast<float>(z)),
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
        const PositionComponent3D &position,
        const RotationComponent3D *rotation = nullptr,
        const ScaleComponent3D *scale = nullptr)
    {
      const Vec3 localCorners[8] = {
          make_vec3(minCorner.x, minCorner.y, minCorner.z),
          make_vec3(maxCorner.x, minCorner.y, minCorner.z),
          make_vec3(maxCorner.x, maxCorner.y, minCorner.z),
          make_vec3(minCorner.x, maxCorner.y, minCorner.z),
          make_vec3(minCorner.x, minCorner.y, maxCorner.z),
          make_vec3(maxCorner.x, minCorner.y, maxCorner.z),
          make_vec3(maxCorner.x, maxCorner.y, maxCorner.z),
          make_vec3(minCorner.x, maxCorner.y, maxCorner.z),
      };

      const Vec3 pos = make_vec3(position.x, position.y, position.z);
      std::array<Vec3, 8> result{};
      for (int i = 0; i < 8; ++i)
      {
        Vec3 corner = localCorners[i];
        if (scale != nullptr)
        {
          corner = make_vec3(corner.x * scale->x, corner.y * scale->y, corner.z * scale->z);
        }
        if (rotation != nullptr)
        {
          corner = rotate_vec3_by_quaternion(corner, *rotation);
        }
        result[i] = add_vec3(pos, corner);
      }
      return result;
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

    bool is_entity_potentially_visible(
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        float boundsRadius)
    {
      const Vec3 toEntity = subtract_vec3(
          make_vec3(position.x, position.y, position.z),
          make_vec3(sceneCamera.position));
      const float depthAlongForward = dot_vec3(toEntity, sceneCamera.forward);
      if (depthAlongForward + boundsRadius < camera.nearClip ||
          depthAlongForward - boundsRadius > camera.farClip)
      {
        return false;
      }

      return true;
    }

    float entity_bounds_radius(const ImportedModel &model)
    {
      if (!model.hasBounds)
      {
        return CUBE_HALF_EXTENT * 1.732f;
      }

      const float dx = std::max(std::abs(model.minX), std::abs(model.maxX));
      const float dy = std::max(std::abs(model.minY), std::abs(model.maxY));
      const float dz = std::max(std::abs(model.minZ), std::abs(model.maxZ));
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    struct ModelProjectionCacheKey
    {
      float cameraX, cameraY, cameraZ;
      float cameraDistance, cameraYaw, cameraPitch;
      float canvasW, canvasH;
      float posX, posY, posZ;
      float rotQx, rotQy, rotQz, rotQw;
      float scaleX, scaleY, scaleZ;
      const void *modelPtr;
      std::uint64_t lightGeneration = 0;

      bool operator==(const ModelProjectionCacheKey &other) const
      {
        return cameraX == other.cameraX && cameraY == other.cameraY && cameraZ == other.cameraZ &&
               cameraDistance == other.cameraDistance && cameraYaw == other.cameraYaw &&
               cameraPitch == other.cameraPitch && canvasW == other.canvasW && canvasH == other.canvasH &&
               posX == other.posX && posY == other.posY && posZ == other.posZ &&
               rotQx == other.rotQx && rotQy == other.rotQy && rotQz == other.rotQz && rotQw == other.rotQw &&
               scaleX == other.scaleX && scaleY == other.scaleY && scaleZ == other.scaleZ &&
               modelPtr == other.modelPtr && lightGeneration == other.lightGeneration;
      }
    };

    struct ModelProjectionCacheEntry
    {
      ModelProjectionCacheKey key{};
      std::vector<hades::preview::ProjectedTriangle> triangles;
    };

    std::unordered_map<Entity::EntityId, ModelProjectionCacheEntry> modelProjectionCache_;

    std::uint64_t sceneLightGeneration_ = 0;
    hades::preview::LightData sceneLightData_;

    const std::vector<hades::preview::ProjectedTriangle> &get_or_project_model(
        Entity::EntityId entity,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const ImportedModel &model,
        float cameraDistance,
        float cameraYaw,
        float cameraPitch,
        const RotationComponent3D *rotation = nullptr,
        const ScaleComponent3D *scale = nullptr)
    {
      ModelProjectionCacheKey key{
          sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z,
          cameraDistance, cameraYaw, cameraPitch,
          canvasSize.x, canvasSize.y,
          position.x, position.y, position.z,
          rotation ? rotation->qx : 0.0f, rotation ? rotation->qy : 0.0f,
          rotation ? rotation->qz : 0.0f, rotation ? rotation->qw : 1.0f,
          scale ? scale->x : 1.0f, scale ? scale->y : 1.0f, scale ? scale->z : 1.0f,
          static_cast<const void *>(&model),
          sceneLightGeneration_};

      auto &entry = modelProjectionCache_[entity];
      if (entry.key == key)
      {
        return entry.triangles;
      }

      entry.key = key;
      const hades::preview::LightData *lightPtr = sceneLightData_.lights.empty() ? nullptr : &sceneLightData_;
      entry.triangles = hades::preview::project_model_triangles(
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
          },
          lightPtr,
          rotation,
          scale);

      return entry.triangles;
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
        ImU32 labelColor = IM_COL32(205, 210, 218, 255),
        Entity::EntityId entity = Entity::INVALID,
        float cameraDistance = 0.0f,
        float cameraYaw = 0.0f,
        float cameraPitch = 0.0f,
        const RotationComponent3D *rotation = nullptr,
        const ScaleComponent3D *scale = nullptr)
    {
      const auto &projectedTriangles = (entity != Entity::INVALID)
          ? get_or_project_model(entity, sceneCamera, camera, canvasOrigin, canvasSize,
                                 position, model, cameraDistance, cameraYaw, cameraPitch, rotation, scale)
          : get_or_project_model(Entity::INVALID, sceneCamera, camera, canvasOrigin, canvasSize,
                                 position, model, cameraDistance, cameraYaw, cameraPitch, rotation, scale);

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
        const ImU32 fillColor = IM_COL32(
            hades::preview::scale_color_channel(182, triangle.shadeR),
            hades::preview::scale_color_channel(194, triangle.shadeG),
            hades::preview::scale_color_channel(208, triangle.shadeB),
            230);
        drawList->AddConvexPolyFilled(points, 3, fillColor);
        const ImU32 wireColor = IM_COL32(
            hades::preview::scale_color_channel(227, std::min(triangle.shadeR + 0.1f, 1.0f)),
            hades::preview::scale_color_channel(232, std::min(triangle.shadeG + 0.1f, 1.0f)),
            hades::preview::scale_color_channel(238, std::min(triangle.shadeB + 0.1f, 1.0f)),
            255);
        drawList->AddPolyline(points, 3, wireColor, ImDrawFlags_Closed, 1.0f);
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

      const float frustumPreviewMaxDepth = std::max(
          CAMERA_FRUSTUM_PREVIEW_MAX_DEPTH,
          camera.farClip * 0.05f);
      const float farDepth = std::min(
          camera.farClip,
          std::max(frustumPreviewMaxDepth, camera.nearClip + 0.001f));
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
        ImU32 labelColor = IM_COL32(205, 210, 218, 255),
        const RotationComponent3D *rotation = nullptr,
        const ScaleComponent3D *scale = nullptr)
    {
      const auto corners = box_corners(minCorner, maxCorner, position, rotation, scale);
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

    float squared_distance(const ImVec2 &lhs, const ImVec2 &rhs)
    {
      const float deltaX = lhs.x - rhs.x;
      const float deltaY = lhs.y - rhs.y;
      return (deltaX * deltaX) + (deltaY * deltaY);
    }

    float point_to_segment_distance_squared(
        const ImVec2 &point,
        const ImVec2 &start,
        const ImVec2 &end)
    {
      const float segmentX = end.x - start.x;
      const float segmentY = end.y - start.y;
      const float segmentLengthSquared = (segmentX * segmentX) + (segmentY * segmentY);
      if (segmentLengthSquared <= 1e-5f)
      {
        return squared_distance(point, start);
      }

      const float t = std::clamp(
          (((point.x - start.x) * segmentX) + ((point.y - start.y) * segmentY)) / segmentLengthSquared,
          0.0f,
          1.0f);
      return squared_distance(
          point,
          ImVec2(start.x + (segmentX * t), start.y + (segmentY * t)));
    }

    float point_to_rect_distance_squared(const ImVec2 &point, const SceneRect &rect)
    {
      const float nearestX = std::clamp(point.x, rect.min.x, rect.max.x);
      const float nearestY = std::clamp(point.y, rect.min.y, rect.max.y);
      return squared_distance(point, ImVec2(nearestX, nearestY));
    }

    Vec3 gizmo_axis_direction(SceneGizmoAxis axis)
    {
      switch (axis)
      {
      case SceneGizmoAxis::X:
        return make_vec3(1.0f, 0.0f, 0.0f);
      case SceneGizmoAxis::Y:
        return make_vec3(0.0f, 1.0f, 0.0f);
      case SceneGizmoAxis::Z:
        return make_vec3(0.0f, 0.0f, 1.0f);
      case SceneGizmoAxis::None:
        break;
      }

      return make_vec3(0.0f, 0.0f, 0.0f);
    }

    ImU32 gizmo_axis_color(SceneGizmoAxis axis, bool highlighted)
    {
      switch (axis)
      {
      case SceneGizmoAxis::X:
        return highlighted ? IM_COL32(255, 118, 118, 255) : IM_COL32(228, 77, 77, 255);
      case SceneGizmoAxis::Y:
        return highlighted ? IM_COL32(131, 242, 151, 255) : IM_COL32(87, 204, 108, 255);
      case SceneGizmoAxis::Z:
        return highlighted ? IM_COL32(117, 171, 255, 255) : IM_COL32(68, 122, 232, 255);
      case SceneGizmoAxis::None:
        break;
      }

      return IM_COL32(214, 220, 228, 255);
    }

    void register_scene_hit(
        SceneHitCandidate &candidate,
        Entity::EntityId entity,
        float distanceSquared,
        float depth,
        float maxDistanceSquared = std::numeric_limits<float>::max())
    {
      if (distanceSquared > maxDistanceSquared)
      {
        return;
      }

      if (!candidate.entity.has_value() ||
          distanceSquared < candidate.distanceSquared - 0.25f ||
          (std::abs(distanceSquared - candidate.distanceSquared) <= 0.25f && depth < candidate.depth))
      {
        candidate.entity = entity;
        candidate.distanceSquared = distanceSquared;
        candidate.depth = depth;
      }
    }

    float projected_entity_depth(
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera)
    {
      return std::max(world_to_camera_space(make_vec3(position), sceneCamera).z, 0.0f);
    }

    std::optional<SceneRect> project_box_screen_rect(
        const std::array<Vec3, 8> &corners,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize)
    {
      bool hasProjectedPoint = false;
      ImVec2 minPoint(0.0f, 0.0f);
      ImVec2 maxPoint(0.0f, 0.0f);
      for (const Vec3 &corner : corners)
      {
        ImVec2 projectedPoint;
        if (!project_point(corner, sceneCamera, camera, canvasOrigin, canvasSize, projectedPoint))
        {
          continue;
        }

        if (!hasProjectedPoint)
        {
          minPoint = projectedPoint;
          maxPoint = projectedPoint;
          hasProjectedPoint = true;
          continue;
        }

        minPoint.x = std::min(minPoint.x, projectedPoint.x);
        minPoint.y = std::min(minPoint.y, projectedPoint.y);
        maxPoint.x = std::max(maxPoint.x, projectedPoint.x);
        maxPoint.y = std::max(maxPoint.y, projectedPoint.y);
      }

      if (!hasProjectedPoint)
      {
        return std::nullopt;
      }

      return SceneRect{minPoint, maxPoint};
    }

    void draw_position_marker(
        ImDrawList *drawList,
        const ImVec2 &screenPoint,
        bool selected,
        const std::string *label = nullptr,
        ImU32 labelColor = IM_COL32(225, 231, 239, 255))
    {
      const float radius = selected ? SCENE_SELECTED_MARKER_RADIUS : SCENE_MARKER_RADIUS;
      const ImU32 fillColor = selected
                                  ? IM_COL32(255, 196, 96, 255)
                                  : IM_COL32(104, 116, 132, 240);
      const ImU32 outlineColor = selected
                                     ? IM_COL32(255, 232, 182, 255)
                                     : IM_COL32(217, 223, 230, 220);

      drawList->AddCircleFilled(screenPoint, radius, fillColor, 18);
      drawList->AddCircle(screenPoint, radius, outlineColor, 18, 1.5f);
      drawList->AddLine(
          ImVec2(screenPoint.x - radius - 2.0f, screenPoint.y),
          ImVec2(screenPoint.x + radius + 2.0f, screenPoint.y),
          outlineColor,
          1.0f);
      drawList->AddLine(
          ImVec2(screenPoint.x, screenPoint.y - radius - 2.0f),
          ImVec2(screenPoint.x, screenPoint.y + radius + 2.0f),
          outlineColor,
          1.0f);

      if (label != nullptr)
      {
        drawList->AddText(
            ImVec2(screenPoint.x + radius + 6.0f, screenPoint.y - radius - 8.0f),
            labelColor,
            label->c_str());
      }
    }

    float scene_gizmo_axis_length(
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera)
    {
      return std::clamp(
          length_vec3(subtract_vec3(make_vec3(position), make_vec3(sceneCamera.position))) *
              SCENE_GIZMO_AXIS_LENGTH_SCALE,
          SCENE_GIZMO_MIN_AXIS_LENGTH,
          SCENE_GIZMO_MAX_AXIS_LENGTH);
    }

    std::array<SceneGizmoAxisProjection, 3> build_gizmo_axis_projections(
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize)
    {
      const float axisLength = scene_gizmo_axis_length(position, sceneCamera);
      const Vec3 origin = make_vec3(position);
      std::array<SceneGizmoAxisProjection, 3> projections = {
          SceneGizmoAxisProjection{SceneGizmoAxis::X, gizmo_axis_direction(SceneGizmoAxis::X)},
          SceneGizmoAxisProjection{SceneGizmoAxis::Y, gizmo_axis_direction(SceneGizmoAxis::Y)},
          SceneGizmoAxisProjection{SceneGizmoAxis::Z, gizmo_axis_direction(SceneGizmoAxis::Z)}};

      for (SceneGizmoAxisProjection &projection : projections)
      {
        const Vec3 endpoint = add_vec3(origin, scale_vec3(projection.direction, axisLength));
        if (!project_point(origin, sceneCamera, camera, canvasOrigin, canvasSize, projection.originScreen) ||
            !project_point(endpoint, sceneCamera, camera, canvasOrigin, canvasSize, projection.endScreen))
        {
          continue;
        }

        const float lineLength = std::sqrt(squared_distance(projection.originScreen, projection.endScreen));
        if (lineLength < SCENE_GIZMO_MIN_SCREEN_LENGTH_PIXELS)
        {
          continue;
        }

        projection.pixelsPerWorldUnit = lineLength / axisLength;
        projection.visible = projection.pixelsPerWorldUnit > 1e-5f;
      }

      return projections;
    }

    const SceneGizmoAxisProjection *find_gizmo_axis_projection(
        const std::array<SceneGizmoAxisProjection, 3> &projections,
        SceneGizmoAxis axis)
    {
      for (const SceneGizmoAxisProjection &projection : projections)
      {
        if (projection.axis == axis)
        {
          return &projection;
        }
      }

      return nullptr;
    }

    SceneGizmoAxis hit_test_gizmo_axes(
        const std::array<SceneGizmoAxisProjection, 3> &projections,
        const ImVec2 &mousePosition)
    {
      SceneGizmoAxis hoveredAxis = SceneGizmoAxis::None;
      float bestDistanceSquared = SCENE_GIZMO_HIT_THRESHOLD_PIXELS * SCENE_GIZMO_HIT_THRESHOLD_PIXELS;

      for (const SceneGizmoAxisProjection &projection : projections)
      {
        if (!projection.visible)
        {
          continue;
        }

        const float distanceSquared = point_to_segment_distance_squared(
            mousePosition,
            projection.originScreen,
            projection.endScreen);
        if (distanceSquared <= bestDistanceSquared)
        {
          hoveredAxis = projection.axis;
          bestDistanceSquared = distanceSquared;
        }
      }

      return hoveredAxis;
    }

    void draw_translation_gizmo(
        ImDrawList *drawList,
        const std::array<SceneGizmoAxisProjection, 3> &projections,
        SceneGizmoAxis highlightedAxis)
    {
      const SceneGizmoAxisProjection *originProjection = nullptr;
      for (const SceneGizmoAxisProjection &projection : projections)
      {
        if (!projection.visible)
        {
          continue;
        }

        if (originProjection == nullptr)
        {
          originProjection = &projection;
        }

        const bool highlighted = projection.axis == highlightedAxis;
        const ImU32 color = gizmo_axis_color(projection.axis, highlighted);
        const float thickness = highlighted ? 3.5f : 2.5f;
        drawList->AddLine(projection.originScreen, projection.endScreen, color, thickness);

        const float lineLength = std::sqrt(squared_distance(projection.originScreen, projection.endScreen));
        if (lineLength <= SCENE_GIZMO_ARROW_SIZE + 1.0f)
        {
          continue;
        }

        const ImVec2 direction(
            (projection.endScreen.x - projection.originScreen.x) / lineLength,
            (projection.endScreen.y - projection.originScreen.y) / lineLength);
        const ImVec2 perpendicular(-direction.y, direction.x);
        const ImVec2 arrowBase(
            projection.endScreen.x - (direction.x * SCENE_GIZMO_ARROW_SIZE),
            projection.endScreen.y - (direction.y * SCENE_GIZMO_ARROW_SIZE));
        const ImVec2 arrowOffset(
            perpendicular.x * (SCENE_GIZMO_ARROW_SIZE * 0.35f),
            perpendicular.y * (SCENE_GIZMO_ARROW_SIZE * 0.35f));
        drawList->AddTriangleFilled(
            projection.endScreen,
            ImVec2(arrowBase.x + arrowOffset.x, arrowBase.y + arrowOffset.y),
            ImVec2(arrowBase.x - arrowOffset.x, arrowBase.y - arrowOffset.y),
            color);
      }

      if (originProjection != nullptr)
      {
        drawList->AddCircleFilled(originProjection->originScreen, 4.5f, IM_COL32(246, 247, 250, 255), 16);
        drawList->AddCircle(originProjection->originScreen, 4.5f, IM_COL32(36, 39, 45, 255), 16, 1.5f);
      }
    }

    void draw_scale_gizmo(
        ImDrawList *drawList,
        const std::array<SceneGizmoAxisProjection, 3> &projections,
        SceneGizmoAxis highlightedAxis)
    {
      const SceneGizmoAxisProjection *originProjection = nullptr;
      for (const SceneGizmoAxisProjection &projection : projections)
      {
        if (!projection.visible)
        {
          continue;
        }

        if (originProjection == nullptr)
        {
          originProjection = &projection;
        }

        const bool highlighted = projection.axis == highlightedAxis;
        const ImU32 color = gizmo_axis_color(projection.axis, highlighted);
        const float thickness = highlighted ? 3.5f : 2.5f;
        drawList->AddLine(projection.originScreen, projection.endScreen, color, thickness);

        const float boxHalf = highlighted ? 4.0f : 3.0f;
        drawList->AddRectFilled(
            ImVec2(projection.endScreen.x - boxHalf, projection.endScreen.y - boxHalf),
            ImVec2(projection.endScreen.x + boxHalf, projection.endScreen.y + boxHalf),
            color);
      }

      if (originProjection != nullptr)
      {
        drawList->AddCircleFilled(originProjection->originScreen, 4.5f, IM_COL32(246, 247, 250, 255), 16);
        drawList->AddCircle(originProjection->originScreen, 4.5f, IM_COL32(36, 39, 45, 255), 16, 1.5f);
      }
    }

    void draw_rotation_gizmo(
        ImDrawList *drawList,
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        SceneGizmoAxis highlightedAxis)
    {
      const float ringRadius = scene_gizmo_axis_length(position, sceneCamera);
      const Vec3 origin = make_vec3(position);

      struct RingDef
      {
        SceneGizmoAxis axis;
        Vec3 basisA;
        Vec3 basisB;
      };

      const RingDef rings[3] = {
          {SceneGizmoAxis::X, make_vec3(0, 1, 0), make_vec3(0, 0, 1)},
          {SceneGizmoAxis::Y, make_vec3(1, 0, 0), make_vec3(0, 0, 1)},
          {SceneGizmoAxis::Z, make_vec3(1, 0, 0), make_vec3(0, 1, 0)},
      };

      for (const RingDef &ring : rings)
      {
        const bool highlighted = ring.axis == highlightedAxis;
        const ImU32 color = gizmo_axis_color(ring.axis, highlighted);
        const float thickness = highlighted ? 3.0f : 2.0f;

        ImVec2 prevScreen{};
        bool prevValid = false;

        for (int i = 0; i <= SCENE_GIZMO_RING_SEGMENTS; ++i)
        {
          const float angle = (static_cast<float>(i) / static_cast<float>(SCENE_GIZMO_RING_SEGMENTS)) * 2.0f * PI;
          const float cosA = std::cos(angle);
          const float sinA = std::sin(angle);
          const Vec3 point = add_vec3(
              origin,
              add_vec3(
                  scale_vec3(ring.basisA, cosA * ringRadius),
                  scale_vec3(ring.basisB, sinA * ringRadius)));

          ImVec2 screenPoint;
          if (!project_point(point, sceneCamera, camera, canvasOrigin, canvasSize, screenPoint))
          {
            prevValid = false;
            continue;
          }

          if (prevValid)
          {
            drawList->AddLine(prevScreen, screenPoint, color, thickness);
          }

          prevScreen = screenPoint;
          prevValid = true;
        }
      }

      ImVec2 originScreen;
      if (project_point(origin, sceneCamera, camera, canvasOrigin, canvasSize, originScreen))
      {
        drawList->AddCircleFilled(originScreen, 4.5f, IM_COL32(246, 247, 250, 255), 16);
        drawList->AddCircle(originScreen, 4.5f, IM_COL32(36, 39, 45, 255), 16, 1.5f);
      }
    }

    SceneGizmoAxis hit_test_rotation_gizmo(
        const PositionComponent3D &position,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const ImVec2 &mousePosition)
    {
      const float ringRadius = scene_gizmo_axis_length(position, sceneCamera);
      const Vec3 origin = make_vec3(position);

      struct RingDef
      {
        SceneGizmoAxis axis;
        Vec3 basisA;
        Vec3 basisB;
      };

      const RingDef rings[3] = {
          {SceneGizmoAxis::X, make_vec3(0, 1, 0), make_vec3(0, 0, 1)},
          {SceneGizmoAxis::Y, make_vec3(1, 0, 0), make_vec3(0, 0, 1)},
          {SceneGizmoAxis::Z, make_vec3(1, 0, 0), make_vec3(0, 1, 0)},
      };

      SceneGizmoAxis bestAxis = SceneGizmoAxis::None;
      float bestDistSq = SCENE_GIZMO_RING_HIT_THRESHOLD_PIXELS * SCENE_GIZMO_RING_HIT_THRESHOLD_PIXELS;

      for (const RingDef &ring : rings)
      {
        ImVec2 prevScreen{};
        bool prevValid = false;

        for (int i = 0; i <= SCENE_GIZMO_RING_SEGMENTS; ++i)
        {
          const float angle = (static_cast<float>(i) / static_cast<float>(SCENE_GIZMO_RING_SEGMENTS)) * 2.0f * PI;
          const float cosA = std::cos(angle);
          const float sinA = std::sin(angle);
          const Vec3 point = add_vec3(
              origin,
              add_vec3(
                  scale_vec3(ring.basisA, cosA * ringRadius),
                  scale_vec3(ring.basisB, sinA * ringRadius)));

          ImVec2 screenPoint;
          if (!project_point(point, sceneCamera, camera, canvasOrigin, canvasSize, screenPoint))
          {
            prevValid = false;
            continue;
          }

          if (prevValid)
          {
            const float distSq = point_to_segment_distance_squared(mousePosition, prevScreen, screenPoint);
            if (distSq < bestDistSq)
            {
              bestDistSq = distSq;
              bestAxis = ring.axis;
            }
          }

          prevScreen = screenPoint;
          prevValid = true;
        }
      }

      return bestAxis;
    }

    SceneHitCandidate pick_entity_in_scene(
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> world,
        const ImVec2 &mousePosition)
    {
      SceneHitCandidate hitCandidate;

      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (world.has_value() && !entity_belongs_to_world(entity, *world, componentManager))
        {
          continue;
        }

        if (!componentManager.hasComponent<PositionComponent3D>(entity))
        {
          continue;
        }

        const auto &position = componentManager.getComponent<PositionComponent3D>(entity);
        const RotationComponent3D *pickRotation = componentManager.hasComponent<RotationComponent3D>(entity)
                                                      ? &componentManager.getComponent<RotationComponent3D>(entity)
                                                      : nullptr;
        const float entityDepth = projected_entity_depth(position, sceneCamera);
        bool hasPreviewGeometry = false;

        if (componentManager.hasComponent<PrimitiveComponent>(entity))
        {
          const auto &primitive = componentManager.getComponent<PrimitiveComponent>(entity);
          if (primitive.type == PrimitiveType::Cube)
          {
            hasPreviewGeometry = true;
            if (const auto rect = project_box_screen_rect(
                    box_corners(
                        make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT),
                        make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT),
                        position,
                        pickRotation),
                    sceneCamera,
                    camera,
                    canvasOrigin,
                    canvasSize);
                rect.has_value())
            {
              register_scene_hit(
                  hitCandidate,
                  entity,
                  point_to_rect_distance_squared(mousePosition, *rect),
                  entityDepth,
                  SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
            }
          }
        }

        if (componentManager.hasComponent<ModelComponent>(entity))
        {
          const auto *model = componentManager.getComponent<ModelComponent>(entity).modelAsset.get();
          if (model != nullptr)
          {
          hasPreviewGeometry = true;
          const Vec3 minCorner = model->hasBounds
                                     ? make_vec3(model->minX, model->minY, model->minZ)
                                     : make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT);
          const Vec3 maxCorner = model->hasBounds
                                     ? make_vec3(model->maxX, model->maxY, model->maxZ)
                                     : make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT);
          if (const auto rect = project_box_screen_rect(
                  box_corners(minCorner, maxCorner, position, pickRotation),
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize);
              rect.has_value())
          {
            register_scene_hit(
                hitCandidate,
                entity,
                point_to_rect_distance_squared(mousePosition, *rect),
                entityDepth,
                SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
          }
          }
        }

        if (componentManager.hasComponent<CameraComponent>(entity))
        {
          hasPreviewGeometry = true;
          const auto &entityCamera = componentManager.getComponent<CameraComponent>(entity);
          if (canvasSize.x > 0.0f &&
              canvasSize.y > 0.0f &&
              entityCamera.fovY > 0.0f &&
              entityCamera.nearClip > 0.0f &&
              entityCamera.farClip > entityCamera.nearClip)
          {
            const float previewDepth = std::min(
                entityCamera.farClip,
                std::max(CAMERA_FRUSTUM_PREVIEW_MAX_DEPTH, entityCamera.nearClip + 0.001f));
            const float aspectRatio = canvasSize.x / canvasSize.y;
            const float halfFovRadians = degrees_to_radians(entityCamera.fovY * 0.5f);
            const float tanHalfFov = std::tan(halfFovRadians);

            if (aspectRatio > 0.0f && tanHalfFov > 0.0f)
            {
              const float nearHalfHeight = tanHalfFov * entityCamera.nearClip;
              const float nearHalfWidth = nearHalfHeight * aspectRatio;
              const float farHalfHeight = tanHalfFov * previewDepth;
              const float farHalfWidth = farHalfHeight * aspectRatio;
              const auto nearCorners = frustum_plane_corners(position, entityCamera.nearClip, nearHalfWidth, nearHalfHeight);
              const auto farCorners = frustum_plane_corners(position, previewDepth, farHalfWidth, farHalfHeight);

              for (std::size_t index = 0; index < nearCorners.size(); ++index)
              {
                const std::size_t nextIndex = (index + 1) % nearCorners.size();
                ImVec2 lineStart;
                ImVec2 lineEnd;

                if (project_line_segment(
                        nearCorners[index],
                        nearCorners[nextIndex],
                        sceneCamera,
                        camera,
                        canvasOrigin,
                        canvasSize,
                        lineStart,
                        lineEnd))
                {
                  register_scene_hit(
                      hitCandidate,
                      entity,
                      point_to_segment_distance_squared(mousePosition, lineStart, lineEnd),
                      entityDepth,
                      SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
                }

                if (project_line_segment(
                        farCorners[index],
                        farCorners[nextIndex],
                        sceneCamera,
                        camera,
                        canvasOrigin,
                        canvasSize,
                        lineStart,
                        lineEnd))
                {
                  register_scene_hit(
                      hitCandidate,
                      entity,
                      point_to_segment_distance_squared(mousePosition, lineStart, lineEnd),
                      entityDepth,
                      SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
                }

                if (project_line_segment(
                        nearCorners[index],
                        farCorners[index],
                        sceneCamera,
                        camera,
                        canvasOrigin,
                        canvasSize,
                        lineStart,
                        lineEnd))
                {
                  register_scene_hit(
                      hitCandidate,
                      entity,
                      point_to_segment_distance_squared(mousePosition, lineStart, lineEnd),
                      entityDepth,
                      SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
                }
              }
            }
          }
        }

        if (componentManager.hasComponent<TextComponent>(entity))
        {
          hasPreviewGeometry = true;
          const auto &text = componentManager.getComponent<TextComponent>(entity);
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

          for (const auto &segment : geometry.segments)
          {
            ImVec2 lineStart;
            ImVec2 lineEnd;
            if (!project_line_segment(
                    make_vec3(segment.start.x, segment.start.y, segment.start.z),
                    make_vec3(segment.end.x, segment.end.y, segment.end.z),
                    sceneCamera,
                    camera,
                    canvasOrigin,
                    canvasSize,
                    lineStart,
                    lineEnd))
            {
              continue;
            }

            register_scene_hit(
                hitCandidate,
                entity,
                point_to_segment_distance_squared(mousePosition, lineStart, lineEnd),
                entityDepth,
                SCENE_PICK_THRESHOLD_PIXELS * SCENE_PICK_THRESHOLD_PIXELS);
          }
        }

        ImVec2 screenPoint;
        if (project_point(make_vec3(position), sceneCamera, camera, canvasOrigin, canvasSize, screenPoint))
        {
          const float markerRadius = hasPreviewGeometry
                                         ? SCENE_PICK_THRESHOLD_PIXELS * 0.75f
                                         : SCENE_PICK_THRESHOLD_PIXELS;
          register_scene_hit(
              hitCandidate,
              entity,
              squared_distance(mousePosition, screenPoint),
              entityDepth,
              markerRadius * markerRadius);
        }
      }

      return hitCandidate;
    }

    void draw_light_gizmo(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        const PositionComponent3D &position,
        const LightComponent &light,
        bool isSelected)
    {
      ImVec2 centerPoint;
      if (!project_point(make_vec3(position), sceneCamera, camera, canvasOrigin, canvasSize, centerPoint))
      {
        return;
      }

      const ImU32 lightColor = isSelected
                                   ? IM_COL32(255, 235, 120, 255)
                                   : IM_COL32(255, 210, 80, 240);
      const float outerRadius = isSelected ? 10.0f : 7.0f;

      drawList->AddCircleFilled(centerPoint, outerRadius * 0.6f, lightColor, 16);
      drawList->AddCircle(centerPoint, outerRadius, lightColor, 16, 1.5f);

      if (light.type == LightType::Directional || light.type == LightType::Spot)
      {
        constexpr float rayLength = 14.0f;
        constexpr int rayCount = 8;
        for (int i = 0; i < rayCount; ++i)
        {
          float angle = static_cast<float>(i) * (2.0f * PI / static_cast<float>(rayCount));
          float cosA = std::cos(angle);
          float sinA = std::sin(angle);
          ImVec2 rayStart(centerPoint.x + cosA * (outerRadius + 2.0f),
                          centerPoint.y + sinA * (outerRadius + 2.0f));
          ImVec2 rayEnd(centerPoint.x + cosA * rayLength,
                        centerPoint.y + sinA * rayLength);
          drawList->AddLine(rayStart, rayEnd, lightColor, 1.0f);
        }
      }

      if (light.type == LightType::Point)
      {
        const float rangeRadius = outerRadius + 4.0f;
        drawList->AddCircle(centerPoint, rangeRadius, IM_COL32(255, 210, 80, 100), 24, 1.0f);
      }
    }

    void build_light_data_from_render_list(
        const RenderList &renderList,
        hades::preview::LightData &outLightData)
    {
      outLightData.lights.clear();
      outLightData.globalAmbient = renderList.globalAmbient;

      for (const auto &rl : renderList.lights)
      {
        hades::preview::LightData::Light l;
        l.type = rl.type;
        l.posX = rl.position.x;
        l.posY = rl.position.y;
        l.posZ = rl.position.z;
        l.dirX = rl.direction.x;
        l.dirY = rl.direction.y;
        l.dirZ = rl.direction.z;
        l.colorR = rl.colorR;
        l.colorG = rl.colorG;
        l.colorB = rl.colorB;
        l.intensity = rl.intensity;
        l.range = rl.range;
        l.innerConeAngle = rl.innerConeAngle;
        l.outerConeAngle = rl.outerConeAngle;
        l.ambientContribution = rl.ambientContribution;
        outLightData.lights.push_back(l);
      }
    }

    int draw_world_preview(
        ImDrawList *drawList,
        const EditorSceneViewCamera &sceneCamera,
        const CameraComponent &camera,
        const ImVec2 &canvasOrigin,
        const ImVec2 &canvasSize,
        EntityManager &entityManager,
        ComponentManager &componentManager,
        const RenderList &renderList,
        std::optional<Entity::EntityId> world,
        std::optional<Entity::EntityId> excludedEntity,
        std::optional<Entity::EntityId> selectedEntity,
        float cameraDistance = 0.0f,
        float cameraYaw = 0.0f,
        float cameraPitch = 0.0f)
    {
      hades::preview::LightData previousLightData = sceneLightData_;
      build_light_data_from_render_list(renderList, sceneLightData_);
      if (sceneLightData_.lights.size() != previousLightData.lights.size())
      {
        ++sceneLightGeneration_;
      }
      else
      {
        for (std::size_t i = 0; i < sceneLightData_.lights.size(); ++i)
        {
          const auto &a = sceneLightData_.lights[i];
          const auto &b = previousLightData.lights[i];
          if (a.posX != b.posX || a.posY != b.posY || a.posZ != b.posZ ||
              a.dirX != b.dirX || a.dirY != b.dirY || a.dirZ != b.dirZ ||
              a.colorR != b.colorR || a.colorG != b.colorG || a.colorB != b.colorB ||
              a.intensity != b.intensity || a.range != b.range ||
              a.type != b.type || a.innerConeAngle != b.innerConeAngle ||
              a.outerConeAngle != b.outerConeAngle || a.ambientContribution != b.ambientContribution)
          {
            ++sceneLightGeneration_;
            break;
          }
        }
      }

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
        const RotationComponent3D *rotation = componentManager.hasComponent<RotationComponent3D>(entity)
                                                  ? &componentManager.getComponent<RotationComponent3D>(entity)
                                                  : nullptr;
        const ScaleComponent3D *scale = componentManager.hasComponent<ScaleComponent3D>(entity)
                                            ? &componentManager.getComponent<ScaleComponent3D>(entity)
                                            : nullptr;
        const std::string label = entity_display_label(entity, componentManager);
        const bool isSelected = selectedEntity.has_value() && *selectedEntity == entity;
        const ImU32 selectedColor = IM_COL32(255, 205, 107, 255);
        const ImU32 selectedLabelColor = IM_COL32(255, 235, 186, 255);
        bool previewDrawn = false;

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
                  isSelected ? selectedColor : IM_COL32(255, 165, 0, 255),
                  isSelected ? 2.5f : 1.5f))
          {
            ++visibleRenderableCount;
            previewDrawn = true;
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
                  isSelected ? selectedColor : IM_COL32(223, 228, 235, 255),
                  isSelected ? 2.5f : 1.5f,
                  &label,
                  isSelected ? selectedLabelColor : IM_COL32(205, 210, 218, 255),
                  rotation,
                  scale))
          {
            ++visibleRenderableCount;
            previewDrawn = true;
          }
          continue;
        }

        if (componentManager.hasComponent<ModelComponent>(entity))
        {
          const auto *model = componentManager.getComponent<ModelComponent>(entity).modelAsset.get();
          if (model != nullptr)
          {
          // Frustum culling is handled by the RenderList — use the pre-computed
          // camera frustum for a fast sphere check.
          const bool modelInFrustum = renderList.camera.frustum.containsSphere(
              {position.x, position.y, position.z}, entity_bounds_radius(*model));
          const bool modelDrawn = modelInFrustum &&
                                  hades::preview::has_renderable_geometry(*model) &&
                                  draw_model_mesh(
                                      drawList,
                                      sceneCamera,
                                      camera,
                                      canvasOrigin,
                                      canvasSize,
                                      position,
                                      *model,
                                      &label,
                                      isSelected ? selectedLabelColor : IM_COL32(205, 210, 218, 255),
                                      entity,
                                      cameraDistance,
                                      cameraYaw,
                                      cameraPitch,
                                      rotation,
                                      scale);
          if (modelDrawn)
          {
            ++visibleRenderableCount;
            previewDrawn = true;
          }

          const Vec3 minCorner = model->hasBounds
                                     ? make_vec3(model->minX, model->minY, model->minZ)
                                     : make_vec3(-CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT, -CUBE_HALF_EXTENT);
          const Vec3 maxCorner = model->hasBounds
                                     ? make_vec3(model->maxX, model->maxY, model->maxZ)
                                     : make_vec3(CUBE_HALF_EXTENT, CUBE_HALF_EXTENT, CUBE_HALF_EXTENT);

          if ((!modelDrawn || isSelected) &&
              draw_wire_box(
                  drawList,
                  sceneCamera,
                  camera,
                  canvasOrigin,
                  canvasSize,
                  position,
                  minCorner,
                  maxCorner,
                  isSelected ? selectedColor : IM_COL32(179, 189, 202, 255),
                  isSelected ? 2.5f : 1.5f,
                  modelDrawn ? nullptr : &label,
                  isSelected ? selectedLabelColor : IM_COL32(205, 210, 218, 255),
                  rotation,
                  scale))
          {
            if (!modelDrawn)
            {
              ++visibleRenderableCount;
            }
            previewDrawn = true;
          }
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
                  isSelected ? selectedColor : IM_COL32(227, 233, 240, 255),
                  isSelected ? 2.5f : 1.5f))
          {
            ++visibleRenderableCount;
            previewDrawn = true;
          }
        }

        if (componentManager.hasComponent<LightComponent>(entity))
        {
          const auto &light = componentManager.getComponent<LightComponent>(entity);
          draw_light_gizmo(drawList, sceneCamera, camera, canvasOrigin, canvasSize, position, light, isSelected);
          ++visibleRenderableCount;
          previewDrawn = true;
        }

        ImVec2 screenPoint;
        if (project_point(make_vec3(position), sceneCamera, camera, canvasOrigin, canvasSize, screenPoint) &&
            (!previewDrawn || isSelected || componentManager.hasComponent<CameraComponent>(entity)))
        {
          const bool showLabel = !previewDrawn || componentManager.hasComponent<CameraComponent>(entity);
          draw_position_marker(
              drawList,
              screenPoint,
              isSelected,
              showLabel ? &label : nullptr,
              isSelected ? selectedLabelColor : IM_COL32(225, 231, 239, 255));
          if (!previewDrawn)
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(SCENE_WINDOW_TITLE);
    ImGui::PopStyleVar();
    ImGuiIO &io = ImGui::GetIO();

    const auto sceneWorld = state.isPlaying
                                ? state.activeWorld
                                : (state.loadedWorld.has_value() &&
                                           componentManager.hasComponent<WorldComponent>(*state.loadedWorld)
                                       ? state.loadedWorld
                                       : normalize_default_world(entityManager, componentManager));

    if (!state.playModeMessage.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", state.playModeMessage.c_str());
    }

    if (!sceneWorld.has_value())
    {
      ImGui::End();
      return;
    }

    {
      const bool translateActive = sceneGizmoMode_ == SceneGizmoMode::Translate;
      const bool rotateActive = sceneGizmoMode_ == SceneGizmoMode::Rotate;

      if (translateActive)
      {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.60f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.80f));
      }
      if (ImGui::SmallButton("Move"))
      {
        sceneGizmoMode_ = SceneGizmoMode::Translate;
      }
      if (translateActive)
      {
        ImGui::PopStyleColor(2);
      }

      ImGui::SameLine();

      if (rotateActive)
      {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.60f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.80f));
      }
      if (ImGui::SmallButton("Rotate"))
      {
        sceneGizmoMode_ = SceneGizmoMode::Rotate;
      }
      if (rotateActive)
      {
        ImGui::PopStyleColor(2);
      }

      ImGui::SameLine();

      const bool scaleActive = sceneGizmoMode_ == SceneGizmoMode::Scale;
      if (scaleActive)
      {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.60f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.80f));
      }
      if (ImGui::SmallButton("Size"))
      {
        sceneGizmoMode_ = SceneGizmoMode::Scale;
      }
      if (scaleActive)
      {
        ImGui::PopStyleColor(2);
      }
    }

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

    // Build the render list using the engine's SceneRenderer.
    {
      const float editorAspect = (canvasSize.y > 0.0f) ? (canvasSize.x / canvasSize.y) : 1.0f;
      const math::Vec3 camPos = {sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z};
      const math::Vec3 camTarget = {
          sceneCamera.position.x + sceneCamera.forward.x,
          sceneCamera.position.y + sceneCamera.forward.y,
          sceneCamera.position.z + sceneCamera.forward.z};
      const RenderCamera renderCamera = sceneRenderer_.buildCamera(
          camPos, camTarget, camera.fovY, editorAspect, camera.nearClip, camera.farClip);
      sceneRenderList_ = sceneRenderer_.buildRenderList(
          renderCamera, componentManager, entityManager, sceneWorld);
    }

    const bool selectedEntityIsEditableInScene =
        !state.isPlaying &&
        state.selectedEntity.has_value() &&
        sceneWorld.has_value() &&
        entity_belongs_to_world(*state.selectedEntity, *sceneWorld, componentManager) &&
        componentManager.hasComponent<PositionComponent3D>(*state.selectedEntity);

    if (activeSceneGizmoAxis_ != SceneGizmoAxis::None)
    {
      const bool activeGizmoStillValid =
          !state.isPlaying &&
          activeSceneGizmoEntity_ != Entity::INVALID &&
          state.selectedEntity.has_value() &&
          *state.selectedEntity == activeSceneGizmoEntity_ &&
          sceneWorld.has_value() &&
          entity_belongs_to_world(activeSceneGizmoEntity_, *sceneWorld, componentManager) &&
          componentManager.hasComponent<PositionComponent3D>(activeSceneGizmoEntity_);

      if (!activeGizmoStillValid)
      {
        activeSceneGizmoAxis_ = SceneGizmoAxis::None;
        activeSceneGizmoEntity_ = Entity::INVALID;
      }
    }

    if (activeSceneGizmoAxis_ != SceneGizmoAxis::None)
    {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        activeSceneGizmoAxis_ = SceneGizmoAxis::None;
        activeSceneGizmoEntity_ = Entity::INVALID;
      }
      else if (sceneGizmoMode_ == SceneGizmoMode::Translate &&
               componentManager.hasComponent<PositionComponent3D>(activeSceneGizmoEntity_))
      {
        auto &position = componentManager.getComponent<PositionComponent3D>(activeSceneGizmoEntity_);
        const float mouseDeltaAlongAxis =
            ((io.MousePos.x - sceneGizmoDragStartMouseX_) * sceneGizmoAxisScreenDirectionX_) +
            ((io.MousePos.y - sceneGizmoDragStartMouseY_) * sceneGizmoAxisScreenDirectionY_);
        const float worldDelta = sceneGizmoPixelsPerWorldUnit_ > 1e-5f
                                     ? (mouseDeltaAlongAxis / sceneGizmoPixelsPerWorldUnit_)
                                     : 0.0f;

        position.x = sceneGizmoDragStartPositionX_;
        position.y = sceneGizmoDragStartPositionY_;
        position.z = sceneGizmoDragStartPositionZ_;

        switch (activeSceneGizmoAxis_)
        {
        case SceneGizmoAxis::X:
          position.x += worldDelta;
          break;
        case SceneGizmoAxis::Y:
          position.y += worldDelta;
          break;
        case SceneGizmoAxis::Z:
          position.z += worldDelta;
          break;
        case SceneGizmoAxis::None:
          break;
        }
      }
      else if (sceneGizmoMode_ == SceneGizmoMode::Rotate &&
               componentManager.hasComponent<RotationComponent3D>(activeSceneGizmoEntity_))
      {
        const float mouseDeltaX = io.MousePos.x - sceneGizmoDragStartMouseX_;
        const float mouseDeltaY = io.MousePos.y - sceneGizmoDragStartMouseY_;
        const float dragPixels = mouseDeltaX + mouseDeltaY;
        const float rotationAngle = dragPixels * 0.01f;

        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;
        switch (activeSceneGizmoAxis_)
        {
        case SceneGizmoAxis::X:
          ax = 1.0f;
          break;
        case SceneGizmoAxis::Y:
          ay = 1.0f;
          break;
        case SceneGizmoAxis::Z:
          az = 1.0f;
          break;
        case SceneGizmoAxis::None:
          break;
        }

        const float halfAngle = rotationAngle * 0.5f;
        const float sinHalf = std::sin(halfAngle);
        const float cosHalf = std::cos(halfAngle);
        const float dqx = ax * sinHalf;
        const float dqy = ay * sinHalf;
        const float dqz = az * sinHalf;
        const float dqw = cosHalf;

        auto &rot = componentManager.getComponent<RotationComponent3D>(activeSceneGizmoEntity_);
        const float sq = sceneGizmoDragStartRotationQx_;
        const float sy = sceneGizmoDragStartRotationQy_;
        const float sz = sceneGizmoDragStartRotationQz_;
        const float sw = sceneGizmoDragStartRotationQw_;
        rot.qx = (dqw * sq) + (dqx * sw) + (dqy * sz) - (dqz * sy);
        rot.qy = (dqw * sy) - (dqx * sz) + (dqy * sw) + (dqz * sq);
        rot.qz = (dqw * sz) + (dqx * sy) - (dqy * sq) + (dqz * sw);
        rot.qw = (dqw * sw) - (dqx * sq) - (dqy * sy) - (dqz * sz);

        const float qlen = std::sqrt(
            (rot.qx * rot.qx) + (rot.qy * rot.qy) + (rot.qz * rot.qz) + (rot.qw * rot.qw));
        if (qlen > 1e-5f)
        {
          rot.qx /= qlen;
          rot.qy /= qlen;
          rot.qz /= qlen;
          rot.qw /= qlen;
        }
      }
      else if (sceneGizmoMode_ == SceneGizmoMode::Scale)
      {
        const float mouseDeltaAlongAxis =
            ((io.MousePos.x - sceneGizmoDragStartMouseX_) * sceneGizmoAxisScreenDirectionX_) +
            ((io.MousePos.y - sceneGizmoDragStartMouseY_) * sceneGizmoAxisScreenDirectionY_);
        const float scaleDelta = sceneGizmoPixelsPerWorldUnit_ > 1e-5f
                                     ? (mouseDeltaAlongAxis / sceneGizmoPixelsPerWorldUnit_)
                                     : 0.0f;

        if (componentManager.hasComponent<CameraComponent>(activeSceneGizmoEntity_))
        {
          auto &cam = componentManager.getComponent<CameraComponent>(activeSceneGizmoEntity_);
          switch (activeSceneGizmoAxis_)
          {
          case SceneGizmoAxis::X:
            cam.nearClip = std::max(0.001f, sceneGizmoDragStartScaleX_ + scaleDelta * 0.1f);
            break;
          case SceneGizmoAxis::Y:
            cam.fovY = std::clamp(sceneGizmoDragStartScaleY_ + scaleDelta * 5.0f, 1.0f, 179.0f);
            break;
          case SceneGizmoAxis::Z:
            cam.farClip = std::max(1.0f, sceneGizmoDragStartScaleZ_ + scaleDelta * 50.0f);
            break;
          case SceneGizmoAxis::None:
            break;
          }
        }
        else if (componentManager.hasComponent<ScaleComponent3D>(activeSceneGizmoEntity_))
        {
          auto &scale = componentManager.getComponent<ScaleComponent3D>(activeSceneGizmoEntity_);
          scale.x = sceneGizmoDragStartScaleX_;
          scale.y = sceneGizmoDragStartScaleY_;
          scale.z = sceneGizmoDragStartScaleZ_;

          switch (activeSceneGizmoAxis_)
          {
          case SceneGizmoAxis::X:
            scale.x = std::max(0.01f, sceneGizmoDragStartScaleX_ + scaleDelta);
            break;
          case SceneGizmoAxis::Y:
            scale.y = std::max(0.01f, sceneGizmoDragStartScaleY_ + scaleDelta);
            break;
          case SceneGizmoAxis::Z:
            scale.z = std::max(0.01f, sceneGizmoDragStartScaleZ_ + scaleDelta);
            break;
          case SceneGizmoAxis::None:
            break;
          }
        }
      }
    }

    std::array<SceneGizmoAxisProjection, 3> gizmoAxes{};
    if (selectedEntityIsEditableInScene)
    {
      gizmoAxes = build_gizmo_axis_projections(
          componentManager.getComponent<PositionComponent3D>(*state.selectedEntity),
          sceneCamera,
          camera,
          canvasOrigin,
          canvasSize);
    }

    SceneGizmoAxis hoveredGizmoAxis = SceneGizmoAxis::None;
    if (selectedEntityIsEditableInScene && sceneCanvasHovered && !rotateModifierDown)
    {
      if (sceneGizmoMode_ == SceneGizmoMode::Translate)
      {
        hoveredGizmoAxis = hit_test_gizmo_axes(gizmoAxes, io.MousePos);
      }
      else if (sceneGizmoMode_ == SceneGizmoMode::Rotate)
      {
        hoveredGizmoAxis = hit_test_rotation_gizmo(
            componentManager.getComponent<PositionComponent3D>(*state.selectedEntity),
            sceneCamera,
            camera,
            canvasOrigin,
            canvasSize,
            io.MousePos);
      }
      else if (sceneGizmoMode_ == SceneGizmoMode::Scale)
      {
        hoveredGizmoAxis = hit_test_gizmo_axes(gizmoAxes, io.MousePos);
      }
    }

    if (sceneCanvasHovered && !rotateModifierDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      bool handledClick = false;

      if (selectedEntityIsEditableInScene && hoveredGizmoAxis != SceneGizmoAxis::None &&
          state.selectedEntity.has_value())
      {
        if (sceneGizmoMode_ == SceneGizmoMode::Translate)
        {
          const SceneGizmoAxisProjection *projection = find_gizmo_axis_projection(gizmoAxes, hoveredGizmoAxis);
          if (projection != nullptr && projection->visible)
          {
            const float axisScreenLength = std::sqrt(squared_distance(projection->originScreen, projection->endScreen));
            if (axisScreenLength > 1e-5f)
            {
              const auto &position = componentManager.getComponent<PositionComponent3D>(*state.selectedEntity);
              activeSceneGizmoAxis_ = hoveredGizmoAxis;
              activeSceneGizmoEntity_ = *state.selectedEntity;
              sceneGizmoDragStartMouseX_ = io.MousePos.x;
              sceneGizmoDragStartMouseY_ = io.MousePos.y;
              sceneGizmoDragStartPositionX_ = position.x;
              sceneGizmoDragStartPositionY_ = position.y;
              sceneGizmoDragStartPositionZ_ = position.z;
              sceneGizmoAxisScreenDirectionX_ = (projection->endScreen.x - projection->originScreen.x) / axisScreenLength;
              sceneGizmoAxisScreenDirectionY_ = (projection->endScreen.y - projection->originScreen.y) / axisScreenLength;
              sceneGizmoPixelsPerWorldUnit_ = projection->pixelsPerWorldUnit;
              handledClick = true;
            }
          }
        }
        else if (sceneGizmoMode_ == SceneGizmoMode::Rotate)
        {
          if (!componentManager.hasComponent<RotationComponent3D>(*state.selectedEntity))
          {
            componentManager.addComponent(*state.selectedEntity, RotationComponent3D{});
          }
          const auto &rot = componentManager.getComponent<RotationComponent3D>(*state.selectedEntity);
          activeSceneGizmoAxis_ = hoveredGizmoAxis;
          activeSceneGizmoEntity_ = *state.selectedEntity;
          sceneGizmoDragStartMouseX_ = io.MousePos.x;
          sceneGizmoDragStartMouseY_ = io.MousePos.y;
          sceneGizmoDragStartRotationQx_ = rot.qx;
          sceneGizmoDragStartRotationQy_ = rot.qy;
          sceneGizmoDragStartRotationQz_ = rot.qz;
          sceneGizmoDragStartRotationQw_ = rot.qw;
          handledClick = true;
        }
        else if (sceneGizmoMode_ == SceneGizmoMode::Scale)
        {
          const SceneGizmoAxisProjection *projection = find_gizmo_axis_projection(gizmoAxes, hoveredGizmoAxis);
          if (projection != nullptr && projection->visible)
          {
            const float axisScreenLength = std::sqrt(squared_distance(projection->originScreen, projection->endScreen));
            if (axisScreenLength > 1e-5f)
            {
              activeSceneGizmoAxis_ = hoveredGizmoAxis;
              activeSceneGizmoEntity_ = *state.selectedEntity;
              sceneGizmoDragStartMouseX_ = io.MousePos.x;
              sceneGizmoDragStartMouseY_ = io.MousePos.y;
              sceneGizmoAxisScreenDirectionX_ = (projection->endScreen.x - projection->originScreen.x) / axisScreenLength;
              sceneGizmoAxisScreenDirectionY_ = (projection->endScreen.y - projection->originScreen.y) / axisScreenLength;
              sceneGizmoPixelsPerWorldUnit_ = projection->pixelsPerWorldUnit;

              if (componentManager.hasComponent<CameraComponent>(*state.selectedEntity))
              {
                const auto &cam = componentManager.getComponent<CameraComponent>(*state.selectedEntity);
                sceneGizmoDragStartScaleX_ = cam.nearClip;
                sceneGizmoDragStartScaleY_ = cam.fovY;
                sceneGizmoDragStartScaleZ_ = cam.farClip;
              }
              else
              {
                if (!componentManager.hasComponent<ScaleComponent3D>(*state.selectedEntity))
                {
                  componentManager.addComponent(*state.selectedEntity, ScaleComponent3D{});
                }
                const auto &scale = componentManager.getComponent<ScaleComponent3D>(*state.selectedEntity);
                sceneGizmoDragStartScaleX_ = scale.x;
                sceneGizmoDragStartScaleY_ = scale.y;
                sceneGizmoDragStartScaleZ_ = scale.z;
              }
              handledClick = true;
            }
          }
        }
      }

      if (!handledClick)
      {
        const SceneHitCandidate hitCandidate = pick_entity_in_scene(
            sceneCamera,
            camera,
            canvasOrigin,
            canvasSize,
            entityManager,
            componentManager,
            sceneWorld,
            io.MousePos);
        if (hitCandidate.entity.has_value())
        {
          state.selectedEntity = *hitCandidate.entity;
          activeSceneGizmoAxis_ = SceneGizmoAxis::None;
          activeSceneGizmoEntity_ = Entity::INVALID;
        }
      }
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasOrigin, canvasMax, IM_COL32(17, 20, 24, 255));
    drawList->AddRect(canvasOrigin, canvasMax, IM_COL32(70, 76, 86, 255));
    drawList->PushClipRect(canvasOrigin, canvasMax, true);

    if (state.isPlaying)
    {
      // During play mode, skip all 3D editor rendering and show a message.
      const char *playingMsg = "Game is playing...";
      const ImVec2 textSize = ImGui::CalcTextSize(playingMsg);
      drawList->AddText(
          ImVec2(canvasOrigin.x + (canvasSize.x - textSize.x) * 0.5f,
                 canvasOrigin.y + (canvasSize.y - textSize.y) * 0.5f),
          IM_COL32(139, 143, 163, 200),
          playingMsg);
    }
    else
    {
      draw_editor_grid(drawList, sceneCamera, camera, canvasOrigin, canvasSize, sceneCameraDistance_);

      draw_world_preview(
          drawList,
          sceneCamera,
          camera,
          canvasOrigin,
          canvasSize,
          entityManager,
          componentManager,
          sceneRenderList_,
          sceneWorld,
          std::nullopt,
          state.selectedEntity,
          sceneCameraDistance_,
          sceneCameraYawDegrees_,
          sceneCameraPitchDegrees_);

      if (selectedEntityIsEditableInScene)
      {
        const SceneGizmoAxis highlightedAxis =
            activeSceneGizmoAxis_ != SceneGizmoAxis::None
                ? activeSceneGizmoAxis_
                : hoveredGizmoAxis;

        if (sceneGizmoMode_ == SceneGizmoMode::Translate)
        {
          draw_translation_gizmo(drawList, gizmoAxes, highlightedAxis);
        }
        else if (sceneGizmoMode_ == SceneGizmoMode::Rotate)
        {
          draw_rotation_gizmo(
              drawList,
              componentManager.getComponent<PositionComponent3D>(*state.selectedEntity),
              sceneCamera,
              camera,
              canvasOrigin,
              canvasSize,
              highlightedAxis);
        }
        else if (sceneGizmoMode_ == SceneGizmoMode::Scale)
        {
          draw_scale_gizmo(drawList, gizmoAxes, highlightedAxis);
        }

        if (highlightedAxis != SceneGizmoAxis::None)
        {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
      }
    }

    drawList->PopClipRect();
    ImGui::End();
  }

  void Editor::render_settings_window()
  {
    if (!openSettingsWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_FirstUseEver);
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

    if (ImGui::BeginTable(
            "SettingsLayout",
            2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 180.0f);
      ImGui::TableSetupColumn("Values", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextColumn();
      ImGui::BeginChild("SettingsCategories", ImVec2(0.0f, 0.0f), true);
      if (ImGui::Selectable("Editor", selectedSettingsCategory_ == SettingsCategory::Editor))
      {
        selectedSettingsCategory_ = SettingsCategory::Editor;
      }
      if (ImGui::Selectable("Plugins", selectedSettingsCategory_ == SettingsCategory::Plugins))
      {
        selectedSettingsCategory_ = SettingsCategory::Plugins;
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("SettingsValues", ImVec2(0.0f, 0.0f), false);

      if (selectedSettingsCategory_ == SettingsCategory::Editor)
      {
        ImGui::TextDisabled("Editor");
        ImGui::Separator();
        ImGui::Checkbox("Show Stats for Nerds", &state.showDebugInfo);

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
      }
      else
      {
        ImGui::TextDisabled("Plugins");
        ImGui::Separator();

        bool hasPluginSettings = false;
        for (const auto &plugin : plugins_)
        {
          if (!should_expose_plugin_setting(*plugin))
          {
            continue;
          }

          hasPluginSettings = true;
          bool visible = plugin->visible(*this);
          const std::string label(plugin->display_name());
          if (ImGui::Checkbox(label.c_str(), &visible))
          {
            plugin->set_visible(*this, visible);
          }
        }

        if (!hasPluginSettings)
        {
          ImGui::TextDisabled("No plugins available.");
        }
      }

      ImGui::EndChild();
      ImGui::EndTable();
    }

    ImGui::End();
  }

  void Editor::render_debug_console_window()
  {
    if (!openDebugConsoleWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(600.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (focusDebugConsoleWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusDebugConsoleWindow_ = false;
    }

    if (!ImGui::Begin(DEBUG_CONSOLE_WINDOW_TITLE, &openDebugConsoleWindow_))
    {
      ImGui::End();
      return;
    }

    if (ImGui::Button("Clear"))
    {
      state.debugConsoleMessages.clear();
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy All"))
    {
      std::string allText;
      for (const auto &message : state.debugConsoleMessages)
      {
        const char *prefix = "[INFO]";
        if (message.level == DebugMessageLevel::Error)
        {
          prefix = "[ERROR]";
        }
        else if (message.level == DebugMessageLevel::Warning)
        {
          prefix = "[WARNING]";
        }
        allText += prefix;
        allText += ' ';
        allText += message.text;
        allText += '\n';
      }
      ImGui::SetClipboardText(allText.c_str());
    }

    ImGui::Separator();

    ImGui::BeginChild("DebugConsoleMessages", ImVec2(0.0f, 0.0f), false);
    ImGui::PushTextWrapPos(0.0f);
    for (std::size_t i = 0; i < state.debugConsoleMessages.size(); ++i)
    {
      const auto &message = state.debugConsoleMessages[i];
      ImVec4 color;
      const char *prefix;
      switch (message.level)
      {
      case DebugMessageLevel::Error:
        color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        prefix = "[ERROR]";
        break;
      case DebugMessageLevel::Warning:
        color = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
        prefix = "[WARNING]";
        break;
      case DebugMessageLevel::Info:
      default:
        color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        prefix = "[INFO]";
        break;
      }

      const std::string fullText = std::string(prefix) + " " + message.text;
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::PushID(static_cast<int>(i));
      ImGui::Selectable("", false, ImGuiSelectableFlags_AllowOverlap);
      if (ImGui::BeginPopupContextItem())
      {
        if (ImGui::MenuItem("Copy"))
        {
          ImGui::SetClipboardText(message.text.c_str());
        }
        ImGui::EndPopup();
      }
      ImGui::SameLine(0.0f, 0.0f);
      ImGui::TextWrapped("%s", fullText.c_str());
      ImGui::PopID();
      ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    ImGui::End();
  }

  void Editor::debug(float deltaTime)
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    ImGui::Begin("Stats for Nerds");
    ImGui::Text("FPS: %.1f  (%.2f ms)", 1.0f / deltaTime, deltaTime * 1000.0f);
    ImGui::Text("Play Mode: %s", state.isPlaying ? "Playing" : "Stopped");
    if (state.activeCamera.has_value())
    {
      ImGui::Text("Active Camera: %u", *state.activeCamera);
    }
    if (!state.playModeMessage.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Play error: %s", state.playModeMessage.c_str());
    }

#ifdef HADES_ENABLE_FRAME_METRICS
    ImGui::Separator();
    ImGui::Text("Frame Metrics");
    if (ImGui::BeginTable("##metrics", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Section");
      ImGui::TableSetupColumn("Last (ms)");
      ImGui::TableSetupColumn("Avg (ms)");
      ImGui::TableSetupColumn("Count");
      ImGui::TableHeadersRow();

      const auto &entries = FrameMetrics::instance().entries();
      for (const auto &entry : entries)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.lastMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.frames > 0 ? entry.totalMs / entry.frames : 0.0);
        ImGui::TableNextColumn();
        ImGui::Text("%u", entry.count);
      }
      ImGui::EndTable();
    }
#endif

    ImGui::End();
  }

  void Editor::render_about_window()
  {
    if (!openAboutWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (focusAboutWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusAboutWindow_ = false;
    }

    if (!ImGui::Begin("About", &openAboutWindow_))
    {
      ImGui::End();
      return;
    }

    ImGui::TextUnformatted("Hades Game Engine");
    ImGui::Separator();
    ImGui::TextUnformatted("An open-source game engine editor and runtime.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Use Help -> Stats for Nerds to show performance stats.");

    ImGui::End();
  }
}
