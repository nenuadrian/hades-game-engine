#include "engine/hades.hpp"

class ClickDetection : public hades::HadesScript
{
public:
  float clickRadius = 1.5f;

  void onMouseDown(hades::ScriptContext &ctx, int button, float screenX, float screenY) override
  {
    if (button != hades::HADES_MOUSE_LEFT)
    {
      return;
    }

    // Find the main camera.
    hades::Entity::EntityId cameraEntity = hades::Entity::INVALID;
    for (hades::Entity::EntityId entity : ctx.entityManager.getAllEntities())
    {
      if (!ctx.componentManager.hasComponent<hades::CameraComponent>(entity))
      {
        continue;
      }
      const auto &cam = ctx.componentManager.getComponent<hades::CameraComponent>(entity);
      if (cam.isMainCamera)
      {
        cameraEntity = entity;
        break;
      }
    }

    if (cameraEntity == hades::Entity::INVALID)
    {
      return;
    }

    // Build camera matrices.
    const auto &camPos = ctx.componentManager.getComponent<hades::PositionComponent3D>(cameraEntity);
    const auto &camRot = ctx.componentManager.getComponent<hades::RotationComponent3D>(cameraEntity);
    const auto &camComp = ctx.componentManager.getComponent<hades::CameraComponent>(cameraEntity);

    hades::math::Vec3 eye(camPos.x, camPos.y, camPos.z);
    hades::math::Quat q(camRot.qx, camRot.qy, camRot.qz, camRot.qw);

    hades::math::Vec3 forward = q.rotate({0.0f, 0.0f, 1.0f});
    hades::math::Vec3 up = q.rotate({0.0f, 1.0f, 0.0f});

    float aspect = (ctx.viewportHeight > 0.0f) ? (ctx.viewportWidth / ctx.viewportHeight) : 1.0f;
    hades::math::Mat4 view = hades::math::Mat4::lookAt(eye, eye + forward, up);
    hades::math::Mat4 proj = hades::math::Mat4::perspective(camComp.fovY, aspect, camComp.nearClip, camComp.farClip);

    // Cast ray from screen point.
    hades::HadesAPI::Ray ray = hades::HadesAPI::screenToWorldRay(
        screenX, screenY,
        ctx.viewportWidth, ctx.viewportHeight,
        eye, view, proj);

    // Test distance from ray to this entity's position.
    const auto &myPos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    hades::math::Vec3 entityPosition(myPos.x, myPos.y, myPos.z);

    float distance = hades::HadesAPI::rayDistanceToPoint(ray, entityPosition);

    if (distance < clickRadius)
    {
      clicked = !clicked;
      hades::HadesAPI::observe("entityClicked", clicked);
    }
  }

private:
  bool clicked = false;
};

HADES_REGISTER_SCRIPT(ClickDetection)
