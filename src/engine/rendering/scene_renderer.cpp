#include "scene_renderer.hpp"

#include <algorithm>
#include <cmath>

#include "../components/camera_component.hpp"
#include "../components/light_component.hpp"
#include "../components/mesh_renderer_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../components/primitive_component.hpp"
#include "../components/rotation_component_3d.hpp"
#include "../components/scale_component_3d.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../core/ecs/world_utils.hpp"

namespace hades
{
  namespace
  {
    constexpr float DEFAULT_CUBE_HALF_EXTENT = 0.5f;
    constexpr float DEFAULT_PLANE_HALF_EXTENT = 0.5f;
  }

  RenderCamera SceneRenderer::buildCamera(
      Entity::EntityId cameraEntity,
      float aspectRatio,
      ComponentManager &componentManager) const
  {
    math::Vec3 position;
    if (componentManager.hasComponent<PositionComponent3D>(cameraEntity))
    {
      const auto &pos = componentManager.getComponent<PositionComponent3D>(cameraEntity);
      position = {pos.x, pos.y, pos.z};
    }

    math::Quat rotation;
    if (componentManager.hasComponent<RotationComponent3D>(cameraEntity))
    {
      const auto &rot = componentManager.getComponent<RotationComponent3D>(cameraEntity);
      rotation = {rot.qx, rot.qy, rot.qz, rot.qw};
    }

    float fovY = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    if (componentManager.hasComponent<CameraComponent>(cameraEntity))
    {
      const auto &cam = componentManager.getComponent<CameraComponent>(cameraEntity);
      fovY = cam.fovY;
      nearClip = cam.nearClip;
      farClip = cam.farClip;
    }

    // Camera forward is +Z in local space, rotated by quaternion.
    math::Vec3 forward = rotation.rotate({0.0f, 0.0f, 1.0f});
    math::Vec3 target = position + forward;

    return buildCamera(position, target, fovY, aspectRatio, nearClip, farClip);
  }

  RenderCamera SceneRenderer::buildCamera(
      const math::Vec3 &position,
      const math::Vec3 &target,
      float fovY,
      float aspectRatio,
      float nearClip,
      float farClip) const
  {
    RenderCamera cam;
    cam.position = position;
    cam.fovY = fovY;
    cam.nearClip = nearClip;
    cam.farClip = farClip;
    cam.aspectRatio = aspectRatio;

    cam.forward = (target - position).normalized();
    math::Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    cam.right = worldUp.cross(cam.forward).normalized();
    cam.up = cam.forward.cross(cam.right);

    cam.view = math::Mat4::lookAt(position, target, worldUp);
    cam.projection = math::Mat4::perspective(fovY, aspectRatio, nearClip, farClip);
    cam.viewProjection = cam.projection * cam.view;
    cam.frustum = math::Frustum::fromViewProjection(cam.viewProjection);

    return cam;
  }

  RenderList SceneRenderer::buildRenderList(
      const RenderCamera &camera,
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter)
  {
    RenderList list;
    list.camera = camera;

    collectLights(list, componentManager, entityManager, worldFilter);
    collectRenderables(list, camera, componentManager, entityManager, worldFilter);

    // Sort opaque front-to-back (closer first, for early-Z).
    std::sort(list.opaqueItems.begin(), list.opaqueItems.end(),
              [](const RenderItem &a, const RenderItem &b)
              {
                return a.distanceToCamera < b.distanceToCamera;
              });

    // Sort transparent back-to-front (farther first, for alpha blending).
    std::sort(list.transparentItems.begin(), list.transparentItems.end(),
              [](const RenderItem &a, const RenderItem &b)
              {
                return a.distanceToCamera > b.distanceToCamera;
              });

    list.totalVisibleEntities = list.opaqueItems.size() + list.transparentItems.size();
    return list;
  }

  void SceneRenderer::collectLights(
      RenderList &list,
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter)
  {
    for (Entity::EntityId entity : query<LightComponent>(entityManager, componentManager, worldFilter))
    {
      const auto &light = componentManager.getComponent<LightComponent>(entity);
      if (!light.enabled)
      {
        continue;
      }

      RenderLight rl;
      switch (light.type)
      {
      case LightType::Directional:
        rl.type = 0;
        break;
      case LightType::Point:
        rl.type = 1;
        break;
      case LightType::Spot:
        rl.type = 2;
        break;
      }

      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        const auto &pos = componentManager.getComponent<PositionComponent3D>(entity);
        rl.position = {pos.x, pos.y, pos.z};
      }

      rl.direction = {light.directionX, light.directionY, light.directionZ};
      rl.colorR = light.colorR;
      rl.colorG = light.colorG;
      rl.colorB = light.colorB;
      rl.intensity = light.intensity;
      rl.range = light.range;
      rl.innerConeAngle = light.innerConeAngle;
      rl.outerConeAngle = light.outerConeAngle;
      rl.ambientContribution = light.ambientContribution;

      list.lights.push_back(rl);
    }
  }

  void SceneRenderer::collectRenderables(
      RenderList &list,
      const RenderCamera &camera,
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter)
  {
    // Collect entities with a PrimitiveComponent.
    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (worldFilter.has_value() &&
          !entity_belongs_to_world(entity, *worldFilter, componentManager))
      {
        continue;
      }

      if (!componentManager.hasComponent<PrimitiveComponent>(entity))
      {
        continue;
      }

      // Read transform.
      math::Vec3 position;
      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        const auto &pos = componentManager.getComponent<PositionComponent3D>(entity);
        position = {pos.x, pos.y, pos.z};
      }

      math::Quat rotation;
      if (componentManager.hasComponent<RotationComponent3D>(entity))
      {
        const auto &rot = componentManager.getComponent<RotationComponent3D>(entity);
        rotation = {rot.qx, rot.qy, rot.qz, rot.qw};
      }

      math::Vec3 scale = {1.0f, 1.0f, 1.0f};
      if (componentManager.hasComponent<ScaleComponent3D>(entity))
      {
        const auto &sc = componentManager.getComponent<ScaleComponent3D>(entity);
        scale = {sc.x, sc.y, sc.z};
      }

      // Build render item.
      RenderItem item;
      item.entity = entity;
      item.worldPosition = position;
      item.worldTransform = math::buildModelMatrix(position, rotation, scale);

      const auto &pc = componentManager.getComponent<PrimitiveComponent>(entity);
      item.primitiveType = pc.type;

      // Read material if present.
      if (componentManager.hasComponent<MeshRendererComponent>(entity))
      {
        item.material = componentManager.getComponent<MeshRendererComponent>(entity).material;
      }

      // Compute bounds.
      item.boundsRadius = computeBoundsRadius(scale);

      // Frustum culling.
      if (!camera.frustum.containsSphere(position, item.boundsRadius))
      {
        ++list.totalCulledEntities;
        continue;
      }

      // Distance to camera for sorting.
      item.distanceToCamera = (position - camera.position).length();

      // Count triangles. Cube: 12, Plane: 2, Sphere: ~224 (16 segments × 14 rings).
      switch (item.primitiveType)
      {
      case PrimitiveType::Cube:
        list.totalTriangles += 12;
        break;
      case PrimitiveType::Sphere:
        list.totalTriangles += 224;
        break;
      case PrimitiveType::Plane:
      default:
        list.totalTriangles += 2;
        break;
      }

      // Route to opaque or transparent list.
      if (item.isTransparent())
      {
        list.transparentItems.push_back(item);
      }
      else
      {
        list.opaqueItems.push_back(item);
      }
    }
  }

  float SceneRenderer::computeBoundsRadius(const math::Vec3 &scale) const
  {
    float maxScale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    // Cube diagonal (sqrt(3) * 0.5) covers both Cube and the thinner Plane conservatively.
    return DEFAULT_CUBE_HALF_EXTENT * 1.732f * maxScale;
  }
}
