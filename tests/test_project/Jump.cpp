#include "engine/hades.hpp"

class Jump : public hades::HadesScript
{
public:
  float jumpForce = 8.0f;
  float gravity = -20.0f;

  void onStart(hades::ScriptContext &ctx) override
  {
    auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    groundY = pos.y;
  }

  void onKeyDown(hades::ScriptContext &ctx, int keyCode) override
  {
    if (keyCode == hades::HADES_KEY_SPACE && onGround)
    {
      verticalVelocity = jumpForce;
      onGround = false;
    }
  }

  void onUpdate(hades::ScriptContext &ctx, float deltaTime) override
  {
    if (!onGround)
    {
      auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);

      verticalVelocity += gravity * deltaTime;
      pos.y += verticalVelocity * deltaTime;

      if (pos.y <= groundY)
      {
        pos.y = groundY;
        verticalVelocity = 0.0f;
        onGround = true;
      }
    }

    hades::HadesAPI::observe("onGround", onGround);
    hades::HadesAPI::observe("verticalVelocity", verticalVelocity);
  }

private:
  float verticalVelocity = 0.0f;
  float groundY = 0.0f;
  bool onGround = true;
};

HADES_REGISTER_SCRIPT(Jump)
