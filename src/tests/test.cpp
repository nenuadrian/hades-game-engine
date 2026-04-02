#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "../engine/assets/model_importer.hpp"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/runtime/main_camera_selection.hpp"

namespace hades
{
  namespace
  {
    std::filesystem::path backpack_model_path()
    {
      return std::filesystem::path(__FILE__).parent_path() / "backpack/12305_backpack_v2_l3.obj";
    }

    TEST(ModelImporterTest, ImportObjCollectsMeshAndMaterialMetadata)
    {
      std::string errorMessage;
      const auto model = ModelImporter::importFromFile(backpack_model_path(), &errorMessage);

      ASSERT_TRUE(model.has_value()) << errorMessage;
      EXPECT_EQ(model->formatHint, "obj");
      EXPECT_FALSE(model->meshes.empty());
      EXPECT_FALSE(model->materials.empty());
      EXPECT_GT(model->totalVertexCount, 0U);
      EXPECT_GT(model->totalFaceCount, 0U);
      EXPECT_FALSE(model->sourcePath.empty());
    }

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
          backpack_model_path(),
          std::nullopt,
          &errorMessage);

      ASSERT_TRUE(entity.has_value()) << errorMessage;
      EXPECT_TRUE(componentManager.hasComponent<NameComponent>(*entity));
      EXPECT_EQ(componentManager.getComponent<NameComponent>(*entity).value, "12305_backpack_v2_l3");
      EXPECT_TRUE(componentManager.hasComponent<ModelComponent>(*entity));
      EXPECT_FALSE(componentManager.getComponent<TransformHierarchyComponent>(*entity).hasParent());

      const auto &model = componentManager.getComponent<ModelComponent>(*entity).model;
      EXPECT_FALSE(model.meshes.empty());
      EXPECT_FALSE(model.materials.empty());
    }

    TEST(MainCameraSelectionTest, RejectsScenesWithoutAnyCamera)
    {
      EntityManager entityManager;
      ComponentManager componentManager;

      EntityFactory::createCube(entityManager, componentManager);

      const auto selection = select_main_camera(entityManager, componentManager);

      EXPECT_EQ(selection.status, MainCameraSelectionStatus::NoCameraPresent);
      EXPECT_FALSE(selection.entity.has_value());
    }

    TEST(MainCameraSelectionTest, RejectsScenesWithoutAMainCamera)
    {
      EntityManager entityManager;
      ComponentManager componentManager;

      const auto camera = EntityFactory::createCamera(entityManager, componentManager);
      componentManager.getComponent<CameraComponent>(camera).isMainCamera = false;

      const auto selection = select_main_camera(entityManager, componentManager);

      EXPECT_EQ(selection.status, MainCameraSelectionStatus::NoMainCameraSelected);
      EXPECT_FALSE(selection.entity.has_value());
    }

    TEST(MainCameraSelectionTest, RejectsScenesWithMultipleMainCameras)
    {
      EntityManager entityManager;
      ComponentManager componentManager;

      const auto firstCamera = EntityFactory::createCamera(entityManager, componentManager);
      const auto secondCamera = EntityFactory::createCamera(entityManager, componentManager);
      componentManager.getComponent<CameraComponent>(firstCamera).isMainCamera = true;
      componentManager.getComponent<CameraComponent>(secondCamera).isMainCamera = true;

      const auto selection = select_main_camera(entityManager, componentManager);

      EXPECT_EQ(selection.status, MainCameraSelectionStatus::MultipleMainCamerasSelected);
      EXPECT_FALSE(selection.entity.has_value());
    }

    TEST(MainCameraSelectionTest, ReturnsTheSingleMainCamera)
    {
      EntityManager entityManager;
      ComponentManager componentManager;

      const auto firstCamera = EntityFactory::createCamera(entityManager, componentManager);
      const auto secondCamera = EntityFactory::createCamera(entityManager, componentManager);
      componentManager.getComponent<CameraComponent>(firstCamera).isMainCamera = false;
      componentManager.getComponent<CameraComponent>(secondCamera).isMainCamera = true;

      const auto selection = select_main_camera(entityManager, componentManager);

      ASSERT_EQ(selection.status, MainCameraSelectionStatus::Ready);
      ASSERT_TRUE(selection.entity.has_value());
      EXPECT_EQ(selection.entity.value(), secondCamera);
    }
  }
}
