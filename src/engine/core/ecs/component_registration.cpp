#include "component_registry.hpp"

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../log.hpp"

#include "../../components/audio_listener_component.hpp"
#include "../../components/audio_source_component.hpp"
#include "../../components/camera_component.hpp"
#include "../../components/collider_component.hpp"
#include "../../components/light_component.hpp"
#include "../../components/mesh_renderer_component.hpp"
#include "../../components/name_component.hpp"
#include "../../components/position_component_3d.hpp"
#include "../../components/primitive_component.hpp"
#include "../../components/render_component.hpp"
#include "../../components/rigid_body_component.hpp"
#include "../../components/rotation_component_3d.hpp"
#include "../../components/scale_component_3d.hpp"
#include "../../components/script_component.hpp"
#include "../../components/text_component.hpp"
#include "../../components/transform_hierarchy_component.hpp"
#include "../../components/world_component.hpp"
#include "component_manager.hpp"

using json = nlohmann::json;

namespace hades
{
  void register_builtin_components()
  {
    static bool registered = false;
    if (registered)
    {
      return;
    }
    registered = true;

    {
        auto &registry = ComponentRegistry::instance();

        // NameComponent
        registry.registerComponent<NameComponent>(
            "name",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<NameComponent>(entity))
                return false;
              const auto &c = cm.getComponent<NameComponent>(entity);
              out = {{"value", c.value}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              NameComponent c;
              c.value = in["value"].get<std::string>();
              cm.addComponent(entity, c);
              return true;
            });

        // PositionComponent3D
        registry.registerComponent<PositionComponent3D>(
            "position3d",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<PositionComponent3D>(entity))
                return false;
              const auto &c = cm.getComponent<PositionComponent3D>(entity);
              out = {{"x", c.x}, {"y", c.y}, {"z", c.z}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              PositionComponent3D c(
                  in["x"].get<float>(),
                  in["y"].get<float>(),
                  in["z"].get<float>());
              cm.addComponent(entity, c);
              return true;
            });

        // TransformHierarchyComponent
        registry.registerComponent<TransformHierarchyComponent>(
            "hierarchy",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<TransformHierarchyComponent>(entity))
                return false;
              const auto &c = cm.getComponent<TransformHierarchyComponent>(entity);
              out["parent"] = c.parent.has_value() ? json(*c.parent) : json(nullptr);
              out["children"] = c.children;
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in,
               const std::unordered_map<Entity::EntityId, Entity::EntityId> &idMap) -> bool
            {
              TransformHierarchyComponent c;

              if (!in["parent"].is_null())
              {
                Entity::EntityId oldParent = in["parent"].get<Entity::EntityId>();
                auto it = idMap.find(oldParent);
                if (it != idMap.end())
                {
                  c.parent = it->second;
                }
              }

              for (const auto &childId : in["children"])
              {
                Entity::EntityId oldChild = childId.get<Entity::EntityId>();
                auto it = idMap.find(oldChild);
                if (it != idMap.end())
                {
                  c.children.push_back(it->second);
                }
              }

              cm.addComponent(entity, c);
              return true;
            });

        // WorldComponent
        registry.registerComponent<WorldComponent>(
            "world",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<WorldComponent>(entity))
                return false;
              const auto &c = cm.getComponent<WorldComponent>(entity);
              out = {{"isDefault", c.isDefault}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              WorldComponent c;
              c.isDefault = in["isDefault"].get<bool>();
              cm.addComponent(entity, c);
              return true;
            });

        // CameraComponent
        registry.registerComponent<CameraComponent>(
            "camera",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<CameraComponent>(entity))
                return false;
              const auto &c = cm.getComponent<CameraComponent>(entity);
              out = {
                  {"isMainCamera", c.isMainCamera},
                  {"fovY", c.fovY},
                  {"nearClip", c.nearClip},
                  {"farClip", c.farClip}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              CameraComponent c;
              c.isMainCamera = in["isMainCamera"].get<bool>();
              c.fovY = in["fovY"].get<float>();
              c.nearClip = in["nearClip"].get<float>();
              c.farClip = in["farClip"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // PrimitiveComponent
        registry.registerComponent<PrimitiveComponent>(
            "primitive",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<PrimitiveComponent>(entity))
                return false;
              const auto &c = cm.getComponent<PrimitiveComponent>(entity);
              out = {{"type", static_cast<int>(c.type)}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              PrimitiveComponent c;
              c.type = static_cast<PrimitiveType>(in["type"].get<int>());
              cm.addComponent(entity, c);
              return true;
            });

        // TextComponent
        registry.registerComponent<TextComponent>(
            "text",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<TextComponent>(entity))
                return false;
              const auto &c = cm.getComponent<TextComponent>(entity);
              out = {
                  {"content", c.content},
                  {"fontSize", c.fontSize},
                  {"wrapWidth", c.wrapWidth},
                  {"lineSpacing", c.lineSpacing},
                  {"yawDegrees", c.yawDegrees},
                  {"pitchDegrees", c.pitchDegrees},
                  {"rollDegrees", c.rollDegrees}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              TextComponent c;
              c.content = in["content"].get<std::string>();
              c.fontSize = in["fontSize"].get<float>();
              c.wrapWidth = in["wrapWidth"].get<float>();
              c.lineSpacing = in["lineSpacing"].get<float>();
              c.yawDegrees = in["yawDegrees"].get<float>();
              c.pitchDegrees = in["pitchDegrees"].get<float>();
              c.rollDegrees = in["rollDegrees"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // AudioListenerComponent
        registry.registerComponent<AudioListenerComponent>(
            "audioListener",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<AudioListenerComponent>(entity))
                return false;
              const auto &c = cm.getComponent<AudioListenerComponent>(entity);
              out = {
                  {"enabled", c.enabled},
                  {"forwardX", c.forwardX},
                  {"forwardY", c.forwardY},
                  {"forwardZ", c.forwardZ},
                  {"upX", c.upX},
                  {"upY", c.upY},
                  {"upZ", c.upZ}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              AudioListenerComponent c;
              c.enabled = in["enabled"].get<bool>();
              c.forwardX = in["forwardX"].get<float>();
              c.forwardY = in["forwardY"].get<float>();
              c.forwardZ = in["forwardZ"].get<float>();
              c.upX = in["upX"].get<float>();
              c.upY = in["upY"].get<float>();
              c.upZ = in["upZ"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // AudioSourceComponent
        registry.registerComponent<AudioSourceComponent>(
            "audioSource",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<AudioSourceComponent>(entity))
                return false;
              const auto &c = cm.getComponent<AudioSourceComponent>(entity);
              out = {
                  {"assetPath", c.assetPath},
                  {"bus", static_cast<int>(c.bus)},
                  {"playOnStart", c.playOnStart},
                  {"looping", c.looping},
                  {"streaming", c.streaming},
                  {"spatialized", c.spatialized},
                  {"volume", c.volume},
                  {"pitch", c.pitch},
                  {"minDistance", c.minDistance},
                  {"maxDistance", c.maxDistance},
                  {"rolloff", c.rolloff}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              AudioSourceComponent c;
              c.assetPath = in["assetPath"].get<std::string>();
              c.bus = static_cast<AudioBus>(in["bus"].get<int>());
              c.playOnStart = in["playOnStart"].get<bool>();
              c.looping = in["looping"].get<bool>();
              c.streaming = in["streaming"].get<bool>();
              c.spatialized = in["spatialized"].get<bool>();
              c.volume = in["volume"].get<float>();
              c.pitch = in["pitch"].get<float>();
              c.minDistance = in["minDistance"].get<float>();
              c.maxDistance = in["maxDistance"].get<float>();
              c.rolloff = in["rolloff"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // ScriptComponent
        registry.registerComponent<ScriptComponent>(
            "script",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<ScriptComponent>(entity))
                return false;
              const auto &c = cm.getComponent<ScriptComponent>(entity);
              json attachments = json::array();
              for (const auto &a : c.attachments)
              {
                json attachment;
                attachment["scriptPath"] = a.scriptPath;
                attachment["className"] = a.className;
                attachment["enabled"] = a.enabled;
                attachment["publicFieldValues"] = a.publicFieldValues;
                attachments.push_back(attachment);
              }
              out = {{"attachments", attachments}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              ScriptComponent c;
              for (const auto &a : in["attachments"])
              {
                ScriptAttachment attachment;
                attachment.scriptPath = a["scriptPath"].get<std::string>();
                attachment.className = a["className"].get<std::string>();
                attachment.enabled = a["enabled"].get<bool>();
                attachment.publicFieldValues = a["publicFieldValues"].get<std::map<std::string, std::string>>();
                c.attachments.push_back(std::move(attachment));
              }
              cm.addComponent(entity, c);
              return true;
            });

        // RotationComponent3D
        registry.registerComponent<RotationComponent3D>(
            "rotation3d",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<RotationComponent3D>(entity))
                return false;
              const auto &c = cm.getComponent<RotationComponent3D>(entity);
              out = {{"qx", c.qx}, {"qy", c.qy}, {"qz", c.qz}, {"qw", c.qw}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              RotationComponent3D c;
              c.qx = in["qx"].get<float>();
              c.qy = in["qy"].get<float>();
              c.qz = in["qz"].get<float>();
              c.qw = in["qw"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // ScaleComponent3D
        registry.registerComponent<ScaleComponent3D>(
            "scale3d",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<ScaleComponent3D>(entity))
                return false;
              const auto &c = cm.getComponent<ScaleComponent3D>(entity);
              out = {{"x", c.x}, {"y", c.y}, {"z", c.z}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              ScaleComponent3D c;
              c.x = in["x"].get<float>();
              c.y = in["y"].get<float>();
              c.z = in["z"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // RigidBodyComponent
        registry.registerComponent<RigidBodyComponent>(
            "rigidBody",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<RigidBodyComponent>(entity))
                return false;
              const auto &c = cm.getComponent<RigidBodyComponent>(entity);
              out = {
                  {"type", static_cast<int>(c.type)},
                  {"mass", c.mass},
                  {"linearDamping", c.linearDamping},
                  {"angularDamping", c.angularDamping},
                  {"friction", c.friction},
                  {"restitution", c.restitution},
                  {"gravityScale", c.gravityScale}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              RigidBodyComponent c;
              c.type = static_cast<RigidBodyType>(in["type"].get<int>());
              c.mass = in["mass"].get<float>();
              c.linearDamping = in["linearDamping"].get<float>();
              c.angularDamping = in["angularDamping"].get<float>();
              c.friction = in["friction"].get<float>();
              c.restitution = in["restitution"].get<float>();
              c.gravityScale = in["gravityScale"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // ColliderComponent
        registry.registerComponent<ColliderComponent>(
            "collider",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<ColliderComponent>(entity))
                return false;
              const auto &c = cm.getComponent<ColliderComponent>(entity);
              out = {
                  {"shape", static_cast<int>(c.shape)},
                  {"halfExtentX", c.halfExtentX},
                  {"halfExtentY", c.halfExtentY},
                  {"halfExtentZ", c.halfExtentZ},
                  {"radius", c.radius},
                  {"capsuleHalfHeight", c.capsuleHalfHeight},
                  {"capsuleRadius", c.capsuleRadius}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              ColliderComponent c;
              c.shape = static_cast<ColliderShape>(in["shape"].get<int>());
              c.halfExtentX = in["halfExtentX"].get<float>();
              c.halfExtentY = in["halfExtentY"].get<float>();
              c.halfExtentZ = in["halfExtentZ"].get<float>();
              c.radius = in["radius"].get<float>();
              c.capsuleHalfHeight = in["capsuleHalfHeight"].get<float>();
              c.capsuleRadius = in["capsuleRadius"].get<float>();
              cm.addComponent(entity, c);
              return true;
            });

        // LightComponent
        registry.registerComponent<LightComponent>(
            "light",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<LightComponent>(entity))
                return false;
              const auto &c = cm.getComponent<LightComponent>(entity);
              out = {
                  {"type", static_cast<int>(c.type)},
                  {"colorR", c.colorR},
                  {"colorG", c.colorG},
                  {"colorB", c.colorB},
                  {"intensity", c.intensity},
                  {"range", c.range},
                  {"directionX", c.directionX},
                  {"directionY", c.directionY},
                  {"directionZ", c.directionZ},
                  {"innerConeAngle", c.innerConeAngle},
                  {"outerConeAngle", c.outerConeAngle},
                  {"ambientContribution", c.ambientContribution},
                  {"castShadows", c.castShadows},
                  {"enabled", c.enabled}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              LightComponent c;
              c.type = static_cast<LightType>(in["type"].get<int>());
              c.colorR = in["colorR"].get<float>();
              c.colorG = in["colorG"].get<float>();
              c.colorB = in["colorB"].get<float>();
              c.intensity = in["intensity"].get<float>();
              c.range = in["range"].get<float>();
              c.directionX = in["directionX"].get<float>();
              c.directionY = in["directionY"].get<float>();
              c.directionZ = in["directionZ"].get<float>();
              c.innerConeAngle = in["innerConeAngle"].get<float>();
              c.outerConeAngle = in["outerConeAngle"].get<float>();
              c.ambientContribution = in["ambientContribution"].get<float>();
              c.castShadows = in["castShadows"].get<bool>();
              c.enabled = in["enabled"].get<bool>();
              cm.addComponent(entity, c);
              return true;
            });

        // MeshRendererComponent
        registry.registerComponent<MeshRendererComponent>(
            "meshRenderer",
            [](Entity::EntityId entity, ComponentManager &cm, json &out) -> bool
            {
              if (!cm.hasComponent<MeshRendererComponent>(entity))
                return false;
              const auto &c = cm.getComponent<MeshRendererComponent>(entity);
              out = {
                  {"baseColorR", c.material.baseColorR},
                  {"baseColorG", c.material.baseColorG},
                  {"baseColorB", c.material.baseColorB},
                  {"metallic", c.material.metallic},
                  {"roughness", c.material.roughness},
                  {"opacity", c.material.opacity},
                  {"wireframe", c.material.wireframe}};
              return true;
            },
            [](Entity::EntityId entity, ComponentManager &cm, const json &in, const auto &) -> bool
            {
              MeshRendererComponent c;
              c.material.baseColorR = in.value("baseColorR", 0.72f);
              c.material.baseColorG = in.value("baseColorG", 0.76f);
              c.material.baseColorB = in.value("baseColorB", 0.82f);
              c.material.metallic = in.value("metallic", 0.0f);
              c.material.roughness = in.value("roughness", 0.5f);
              c.material.opacity = in.value("opacity", 1.0f);
              c.material.wireframe = in.value("wireframe", false);
              cm.addComponent(entity, c);
              return true;
            });
    }
  }
}
