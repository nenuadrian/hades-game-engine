#include "engine/hades.hpp"

class S1 : public hades::HadesScript
{
public:
  void onStart(hades::ScriptContext &ctx) override
  {
  }

  void onUpdate(hades::ScriptContext &ctx, float deltaTime) override
  {
  }
};

HADES_REGISTER_SCRIPT(S1)
