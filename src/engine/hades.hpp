#ifndef HADES_ENGINE_HADES_HPP
#define HADES_ENGINE_HADES_HPP

// Scripting API
#include "runtime/hades_script.hpp"
#include "runtime/hades_script_registration.hpp"
#include "runtime/hades_keycodes.hpp"

// Procedural audio facade
#include "audio/script_audio.hpp"
#include "audio/audio_bus.hpp"

// Skeletal animation facade
#include "animation/script_animation.hpp"
#include "animation/animation_types.hpp"

// Blueprint facade -- send events to and read/write variables on the graphs
// attached to an entity. The other direction (a graph calling into C++) lands
// on HadesScript::onMessage.
#include "blueprint/script_blueprint.hpp"

// UI facade -- drive UICanvasComponent widget trees (HUD, menus, world-space
// health bars). Clicks arrive as onMessage("ui.clicked", widgetId).
#include "ui/script_ui.hpp"

// ECS
#include "core/ecs/entity.hpp"
#include "core/ecs/component_manager.hpp"
#include "core/ecs/entity_manager.hpp"

// Math
#include "rendering/math3d.hpp"

// Components
#include "components/animation_component.hpp"
#include "components/animator_component.hpp"
#include "components/audio_listener_component.hpp"
#include "components/audio_source_component.hpp"
#include "components/blueprint_component.hpp"
#include "components/camera_component.hpp"
#include "components/collider_component.hpp"
#include "components/light_component.hpp"
#include "components/mesh_renderer_component.hpp"
#include "components/model_component.hpp"
#include "components/name_component.hpp"
#include "components/position_component_2d.hpp"
#include "components/position_component_3d.hpp"
#include "components/primitive_component.hpp"
#include "components/render_component.hpp"
#include "components/rigid_body_component.hpp"
#include "components/rotation_component_3d.hpp"
#include "components/scale_component_3d.hpp"
#include "components/script_component.hpp"
#include "components/text_component.hpp"
#include "components/transform_hierarchy_component.hpp"
#include "components/ui_canvas_component.hpp"
#include "components/world_component.hpp"

// Neural / RL scripting API
#include "hades_neural.hpp"

#endif
