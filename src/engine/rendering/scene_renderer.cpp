#include "scene_renderer.hpp"

#include <algorithm>
#include <cmath>

#include "../assets/model_asset.hpp"
#include "../assets/model_asset_cache.hpp"
#include "../components/animation_component.hpp"
#include "../components/camera_component.hpp"
#include "../components/light_component.hpp"
#include "../components/mesh_renderer_component.hpp"
#include "../components/model_component.hpp"
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

    // Scenes without any enabled light get a default headlight so geometry
    // is still visibly shaded instead of rendering at flat ambient. Tilted
    // slightly downward from the view direction so surfaces gain contrast.
    if (list.lights.empty())
    {
      RenderLight headlight;
      headlight.type = 0; // directional
      headlight.direction = (camera.forward + math::Vec3{0.0f, -0.6f, 0.0f}).normalized();
      headlight.intensity = 0.9f;
      list.lights.push_back(headlight);
    }

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
    // Collect entities with a PrimitiveComponent or a ModelComponent.
    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (worldFilter.has_value() &&
          !entity_belongs_to_world(entity, *worldFilter, componentManager))
      {
        continue;
      }

      const bool hasPrimitive = componentManager.hasComponent<PrimitiveComponent>(entity);
      const bool hasModel = componentManager.hasComponent<ModelComponent>(entity);
      if (!hasPrimitive && !hasModel)
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

      const float maxScale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
      std::size_t itemTriangles = 0;

      if (hasModel)
      {
        auto &cache = ModelAssetCache::instance();
        const auto &mc = componentManager.getComponent<ModelComponent>(entity);
        const ModelAsset *asset = cache.get(mc.assetPath);
        if (asset == nullptr)
        {
          // Missing or broken asset (already logged by the cache); nothing
          // to draw for this entity.
          continue;
        }

        item.model = asset;
        item.modelKey = cache.resolvePath(mc.assetPath).string();
        item.boundsRadius = asset->boundsRadius() * maxScale;
        itemTriangles = asset->triangleCount();

        // Pose: sample the animation clip when one is playing/scrubbed,
        // otherwise fall back to the bind pose.
        if (asset->hasAnimations() &&
            componentManager.hasComponent<AnimationComponent>(entity))
        {
          const auto &anim = componentManager.getComponent<AnimationComponent>(entity);
          asset->samplePose(anim.clipIndex, anim.time, item.boneMatrices);
        }
        else
        {
          item.boneMatrices = asset->bindPose();
        }

        if (componentManager.hasComponent<MeshRendererComponent>(entity))
        {
          item.material = componentManager.getComponent<MeshRendererComponent>(entity).material;
          item.overrideMaterial = true;
        }
      }
      else
      {
        const auto &pc = componentManager.getComponent<PrimitiveComponent>(entity);
        item.primitiveType = pc.type;

        // Read material if present.
        if (componentManager.hasComponent<MeshRendererComponent>(entity))
        {
          item.material = componentManager.getComponent<MeshRendererComponent>(entity).material;
        }

        // Compute bounds.
        item.boundsRadius = computeBoundsRadius(scale);

        // Count triangles. Cube: 12, Plane: 2, Sphere: ~224 (16 segments × 14 rings).
        switch (item.primitiveType)
        {
        case PrimitiveType::Cube:
          itemTriangles = 12;
          break;
        case PrimitiveType::Sphere:
          itemTriangles = 224;
          break;
        case PrimitiveType::Plane:
        default:
          itemTriangles = 2;
          break;
        }
      }

      // Frustum culling.
      if (!camera.frustum.containsSphere(position, item.boundsRadius))
      {
        ++list.totalCulledEntities;
        continue;
      }

      list.totalTriangles += itemTriangles;

      // Distance to camera for sorting.
      item.distanceToCamera = (position - camera.position).length();

      // Route to opaque or transparent list.
      if (item.isTransparent())
      {
        list.transparentItems.push_back(std::move(item));
      }
      else
      {
        list.opaqueItems.push_back(std::move(item));
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
