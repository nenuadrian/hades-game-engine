// Auto-generated script registry. Do not edit.
#include "engine/runtime/hades_script_registration.hpp"

extern "C" hades::HadesScript *hades_create_ArrowMovement();
extern "C" hades::HadesScript *hades_create_Jump();
extern "C" hades::HadesScript *hades_create_ClickDetection();
extern "C" hades::HadesScript *hades_create_WorldLoader();

extern "C" const hades::ScriptFactoryEntry hades_script_factories[] = {
    {"ArrowMovement", hades_create_ArrowMovement},
    {"Jump", hades_create_Jump},
    {"ClickDetection", hades_create_ClickDetection},
    {"WorldLoader", hades_create_WorldLoader},
    {nullptr, nullptr}
};
extern "C" const int hades_script_factory_count = 4;
