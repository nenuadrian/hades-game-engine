#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

#include "test_support.hpp"

#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  TEST(EntityFactoryTest, CreateRootCameraAddsDefaultComponents)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto entity = EntityFactory::createCamera(entityManager, componentManager);

    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(entity));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(entity).value, "Camera");
    EXPECT_TRUE(componentManager.hasComponent<TransformHierarchyComponent>(entity));
    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(entity).hasParent());
    EXPECT_TRUE(componentManager.hasComponent<PositionComponent3D>(entity));

    const auto &position = componentManager.getComponent<PositionComponent3D>(entity);
    EXPECT_FLOAT_EQ(position.x, 0.0f);
    EXPECT_FLOAT_EQ(position.y, 0.0f);
    EXPECT_FLOAT_EQ(position.z, 0.0f);

    EXPECT_TRUE(componentManager.hasComponent<CameraComponent>(entity));
    const auto &camera = componentManager.getComponent<CameraComponent>(entity);
    EXPECT_TRUE(camera.isMainCamera);
    EXPECT_FLOAT_EQ(camera.fovY, 60.0f);
    EXPECT_FLOAT_EQ(camera.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(camera.farClip, 1000.0f);
    EXPECT_TRUE(componentManager.hasComponent<RotationComponent3D>(entity));
    const auto &rotation = componentManager.getComponent<RotationComponent3D>(entity);
    EXPECT_FLOAT_EQ(rotation.qx, 0.0f);
    EXPECT_FLOAT_EQ(rotation.qy, 0.0f);
    EXPECT_FLOAT_EQ(rotation.qz, 0.0f);
    EXPECT_FLOAT_EQ(rotation.qw, 1.0f);
    EXPECT_TRUE(componentManager.hasComponent<AudioListenerComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<PrimitiveComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<AudioSourceComponent>(entity));
  }

  TEST(EntityFactoryTest, CreateRootCubeAddsDefaultComponents)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto entity = EntityFactory::createCube(entityManager, componentManager);

    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(entity));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(entity).value, "Cube");
    EXPECT_TRUE(componentManager.hasComponent<TransformHierarchyComponent>(entity));
    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(entity).hasParent());
    EXPECT_TRUE(componentManager.hasComponent<PositionComponent3D>(entity));
    EXPECT_TRUE(componentManager.hasComponent<PrimitiveComponent>(entity));
    EXPECT_EQ(componentManager.getComponent<PrimitiveComponent>(entity).type, PrimitiveType::Cube);
    EXPECT_FALSE(componentManager.hasComponent<CameraComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<AudioListenerComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<AudioSourceComponent>(entity));
  }

  TEST(EntityFactoryTest, CreateAudioEmitterAddsAudioSourceComponent)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto entity = EntityFactory::createAudioEmitter(entityManager, componentManager);

    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(entity));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(entity).value, "Audio Emitter");
    EXPECT_TRUE(componentManager.hasComponent<TransformHierarchyComponent>(entity));
    EXPECT_TRUE(componentManager.hasComponent<PositionComponent3D>(entity));
    EXPECT_TRUE(componentManager.hasComponent<AudioSourceComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<AudioListenerComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<CameraComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<PrimitiveComponent>(entity));

    const auto &audioSource = componentManager.getComponent<AudioSourceComponent>(entity);
    EXPECT_EQ(audioSource.bus, AudioBus::Sfx);
    EXPECT_TRUE(audioSource.playOnStart);
    EXPECT_TRUE(audioSource.spatialized);
  }

  TEST(EntityFactoryTest, CreateTextAddsTextComponent)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto entity = EntityFactory::createText(entityManager, componentManager);

    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(entity));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(entity).value, "Text");
    EXPECT_TRUE(componentManager.hasComponent<TransformHierarchyComponent>(entity));
    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(entity).hasParent());
    EXPECT_TRUE(componentManager.hasComponent<PositionComponent3D>(entity));
    EXPECT_TRUE(componentManager.hasComponent<TextComponent>(entity));
    EXPECT_FALSE(componentManager.hasComponent<PrimitiveComponent>(entity));

    const auto &text = componentManager.getComponent<TextComponent>(entity);
    EXPECT_EQ(text.content, "Text");
    EXPECT_FLOAT_EQ(text.fontSize, 1.0f);
    EXPECT_FLOAT_EQ(text.wrapWidth, 4.0f);
    EXPECT_FLOAT_EQ(text.lineSpacing, 1.25f);
    EXPECT_FLOAT_EQ(text.yawDegrees, 0.0f);
    EXPECT_FLOAT_EQ(text.pitchDegrees, 0.0f);
    EXPECT_FLOAT_EQ(text.rollDegrees, 0.0f);
  }

  TEST(EntityFactoryTest, CreateWorldAddsWorldRootComponents)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);

    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(world));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(world).value, "World1");
    EXPECT_TRUE(componentManager.hasComponent<WorldComponent>(world));
    EXPECT_TRUE(componentManager.getComponent<WorldComponent>(world).isDefault);
    EXPECT_TRUE(componentManager.hasComponent<TransformHierarchyComponent>(world));
    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(world).hasParent());
    EXPECT_FALSE(componentManager.hasComponent<PositionComponent3D>(world));
  }

  TEST(EntityFactoryTest, CreateChildEntityUpdatesParentAndChildHierarchy)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto parent = EntityFactory::createCamera(entityManager, componentManager);
    const auto child = EntityFactory::createCube(entityManager, componentManager, parent);

    const auto &childHierarchy = componentManager.getComponent<TransformHierarchyComponent>(child);
    ASSERT_TRUE(childHierarchy.hasParent());
    EXPECT_EQ(childHierarchy.parent.value(), parent);

    const auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(parent);
    const auto childIt = std::find(parentHierarchy.children.begin(), parentHierarchy.children.end(), child);
    EXPECT_NE(childIt, parentHierarchy.children.end());
  }

  TEST(EntityFactoryTest, CreateCubeWithoutParentFallsBackToRoot)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto cube = EntityFactory::createCube(entityManager, componentManager, std::nullopt);

    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(cube).hasParent());
  }

  TEST(EntityFactoryTest, CreateChildWithInvalidParentFallsBackToRoot)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto invalidParent = entityManager.createEntity();
    const auto cube = EntityFactory::createCube(entityManager, componentManager, invalidParent);

    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(cube).hasParent());
    EXPECT_FALSE(componentManager.hasComponent<TransformHierarchyComponent>(invalidParent));
  }

  TEST(EntityFactoryTest, CreateImportedModelAddsModelComponent)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    std::string errorMessage;
    const auto entity = EntityFactory::createImportedModel(
        entityManager,
        componentManager,
        test_support::backpack_model_path(),
        std::nullopt,
        &errorMessage);

    ASSERT_TRUE(entity.has_value()) << errorMessage;
    EXPECT_TRUE(componentManager.hasComponent<NameComponent>(*entity));
    EXPECT_EQ(componentManager.getComponent<NameComponent>(*entity).value, "12305_backpack_v2_l3");
    EXPECT_TRUE(componentManager.hasComponent<ModelComponent>(*entity));
    EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(*entity).hasParent());

    const auto &modelComponent = componentManager.getComponent<ModelComponent>(*entity);
    ASSERT_TRUE(modelComponent.modelAsset.is_ready());
    const auto *model = modelComponent.modelAsset.get();
    ASSERT_NE(model, nullptr);
    EXPECT_FALSE(model->meshes.empty());
    EXPECT_FALSE(model->materials.empty());
  }
}
