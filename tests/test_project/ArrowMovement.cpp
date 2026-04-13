#include "engine/hades.hpp"

class ArrowMovement : public hades::HadesScript
{
public:
  float speed = 5.0f;

  void onKeyDown(hades::ScriptContext &ctx, int keyCode) override
  {
    if (keyCode == hades::HADES_KEY_LEFT) left = true;
    if (keyCode == hades::HADES_KEY_RIGHT) right = true;
    if (keyCode == hades::HADES_KEY_UP) forward = true;
    if (keyCode == hades::HADES_KEY_DOWN) backward = true;
  }

  void onKeyUp(hades::ScriptContext &ctx, int keyCode) override
  {
    if (keyCode == hades::HADES_KEY_LEFT) left = false;
    if (keyCode == hades::HADES_KEY_RIGHT) right = false;
    if (keyCode == hades::HADES_KEY_UP) forward = false;
    if (keyCode == hades::HADES_KEY_DOWN) backward = false;
  }

  void onUpdate(hades::ScriptContext &ctx, float deltaTime) override
  {
    auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);

    if (left) pos.x -= speed * deltaTime;
    if (right) pos.x += speed * deltaTime;
    if (forward) pos.z += speed * deltaTime;
    if (backward) pos.z -= speed * deltaTime;

    hades::HadesAPI::observe("playerX", pos.x);
    hades::HadesAPI::observe("playerZ", pos.z);
  }

private:
  bool left = false;
  bool right = false;
  bool forward = false;
  bool backward = false;
};

HADES_REGISTER_SCRIPT(ArrowMovement)
