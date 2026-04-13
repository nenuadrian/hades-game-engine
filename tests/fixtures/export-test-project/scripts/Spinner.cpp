#include "engine/runtime/hades_script.hpp"
#include "engine/runtime/hades_script_registration.hpp"
#include "engine/components/position_component_3d.hpp"

#include <cmath>

class Spinner : public hades::HadesScript
{
public:
  float speed = 90.0f;

  void onUpdate(hades::ScriptContext &ctx, float deltaTime) override
  {
    auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.y += std::sin(deltaTime * speed * 3.14159265f / 180.0f) * deltaTime;
  }
};

HADES_REGISTER_SCRIPT(Spinner)
