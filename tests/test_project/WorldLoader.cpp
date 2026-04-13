#include "engine/hades.hpp"

class WorldLoader : public hades::HadesScript
{
public:
  void onKeyDown(hades::ScriptContext &ctx, int keyCode) override
  {
    if (keyCode == hades::HADES_KEY_L)
    {
      hades::HadesAPI::loadWorld("World2");
    }
  }
};

HADES_REGISTER_SCRIPT(WorldLoader)
