#ifndef HADES_ENGINE_RENDERING_RENDER_TYPES_HPP
#define HADES_ENGINE_RENDERING_RENDER_TYPES_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "../components/primitive_component.hpp"
#include "../core/ecs/entity.hpp"
#include "math3d.hpp"

namespace hades
{
  class ModelAsset;

  // -------------------------------------------------------------------------
  // RenderCamera — fully resolved camera for rendering
  // -------------------------------------------------------------------------
  struct RenderCamera
  {
    math::Vec3 position;
    math::Vec3 forward;
    math::Vec3 right;
    math::Vec3 up;
    math::Mat4 view;
    math::Mat4 projection;
    math::Mat4 viewProjection;
    math::Frustum frustum;
    float fovY = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float aspectRatio = 1.0f;
  };

  // -------------------------------------------------------------------------
  // RenderLight — flat light data for rendering
  // -------------------------------------------------------------------------
  struct RenderLight
  {
    int type = 0; // 0=Directional, 1=Point, 2=Spot

    math::Vec3 position;
    math::Vec3 direction{0.0f, -1.0f, 0.0f};

    float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 25.0f;
    float outerConeAngle = 35.0f;
    float ambientContribution = 0.05f;
  };

  // -------------------------------------------------------------------------
  // Material — surface properties for rendering
  // -------------------------------------------------------------------------
  struct Material
  {
    float baseColorR = 0.72f;
    float baseColorG = 0.76f;
    float baseColorB = 0.82f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float opacity = 1.0f;
    bool wireframe = false;
  };

  // -------------------------------------------------------------------------
  // RenderItem — one renderable entity in the scene
  // -------------------------------------------------------------------------
  struct RenderItem
  {
    Entity::EntityId entity = Entity::INVALID;
    math::Mat4 worldTransform;
    math::Vec3 worldPosition;

    /// Primitive shape to render (used when `model` is null).
    PrimitiveType primitiveType = PrimitiveType::Cube;

    /// Imported model asset to render instead of a primitive. The pointer is
    /// owned by ModelAssetCache and stays valid for the frame.
    const ModelAsset *model = nullptr;

    /// Cache key (resolved asset path) for backend GPU mesh caches.
    std::string modelKey;

    /// Bone palette for this frame's pose; empty means bind pose.
    std::vector<math::Mat4> boneMatrices;

    /// Material for this item. For models this is an override applied to all
    /// meshes when `overrideMaterial` is set; otherwise the imported
    /// per-mesh materials are used.
    Material material;

    /// True when a MeshRendererComponent overrides the model's materials.
    bool overrideMaterial = false;

    /// Distance to camera (for sorting).
    float distanceToCamera = 0.0f;

    /// Bounding sphere radius in world space.
    float boundsRadius = 0.0f;

    /// Whether this item is transparent (opacity < 1).
    bool isTransparent() const { return material.opacity < 1.0f; }
  };

  // -------------------------------------------------------------------------
  // UIDrawData — resolved UI geometry riding along with the frame
  // -------------------------------------------------------------------------
  struct UIVertex
  {
    /// World batches: world position. Screen batches: x/y in viewport
    /// pixels (origin top-left), z unused.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
  };

  struct UIDrawData
  {
    /// World-space widgets (health bars over entities): drawn after the 3D
    /// scene with depth test on / depth write off, back-to-front.
    std::vector<UIVertex> worldTriangles;
    std::vector<UIVertex> worldLines;

    /// Screen-space widgets (HUD, menus): drawn last, no depth.
    std::vector<UIVertex> screenTriangles;
    std::vector<UIVertex> screenLines;

    bool empty() const
    {
      return worldTriangles.empty() && worldLines.empty() &&
             screenTriangles.empty() && screenLines.empty();
    }

    void clear()
    {
      worldTriangles.clear();
      worldLines.clear();
      screenTriangles.clear();
      screenLines.clear();
    }
  };

  // -------------------------------------------------------------------------
  // RenderList — complete output of the render pipeline
  // -------------------------------------------------------------------------
  struct RenderList
  {
    RenderCamera camera;
    std::vector<RenderLight> lights;
    float globalAmbient = 0.15f;

    /// Opaque items sorted front-to-back (for early-Z efficiency).
    std::vector<RenderItem> opaqueItems;

    /// Transparent items sorted back-to-front (for alpha blending).
    std::vector<RenderItem> transparentItems;

    /// Resolved UI geometry for this frame's viewport.
    UIDrawData ui;

    /// Stats.
    std::size_t totalVisibleEntities = 0;
    std::size_t totalCulledEntities = 0;
    std::size_t totalTriangles = 0;

    void clear()
    {
      lights.clear();
      opaqueItems.clear();
      transparentItems.clear();
      ui.clear();
      totalVisibleEntities = 0;
      totalCulledEntities = 0;
      totalTriangles = 0;
    }
  };
}

#endif
