#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../editor/workspace_manager.hpp"
#include "../editor/workspace_file_operations.hpp"
#include "../engine/assets/model_importer.hpp"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    struct ScopedDirectoryCleanup
    {
      explicit ScopedDirectoryCleanup(std::filesystem::path directoryPath)
          : directory(directoryPath) {}

      ~ScopedDirectoryCleanup()
      {
        std::error_code errorCode;
        std::filesystem::remove_all(directory, errorCode);
      }

      std::filesystem::path directory;
    };

    std::filesystem::path unique_test_directory(const char *prefix)
    {
      const auto uniqueSuffix = std::to_string(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
      return std::filesystem::temp_directory_path() / (std::string(prefix) + "-" + uniqueSuffix);
    }

    std::filesystem::path backpack_model_path()
    {
      return std::filesystem::path(__FILE__).parent_path() / "backpack/12305_backpack_v2_l3.obj";
    }

    TEST(WorkspaceManagerTest, CreateWorkspaceCreatesFolderAndPersistsRecentHistory)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-create");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path parentDirectory = testRoot / "workspaces";
      const std::filesystem::path storagePath = testRoot / "prefs" / "recent_workspaces.txt";
      std::filesystem::create_directories(parentDirectory);

      WorkspaceManager manager(storagePath);
      std::string errorMessage;
      const auto createdWorkspace = manager.create_workspace(parentDirectory, "Sandbox", &errorMessage);

      ASSERT_TRUE(createdWorkspace.has_value()) << errorMessage;
      EXPECT_EQ(createdWorkspace->name, "Sandbox");
      EXPECT_TRUE(std::filesystem::exists(createdWorkspace->path));
      EXPECT_TRUE(std::filesystem::is_directory(createdWorkspace->path));
      ASSERT_TRUE(manager.current_workspace().has_value());
      EXPECT_EQ(manager.current_workspace()->path, createdWorkspace->path);
      ASSERT_EQ(manager.recent_workspaces().size(), 1U);
      EXPECT_EQ(manager.recent_workspaces().front().path, createdWorkspace->path);

      WorkspaceManager reloadedManager(storagePath);
      EXPECT_TRUE(reloadedManager.load(&errorMessage)) << errorMessage;
      ASSERT_EQ(reloadedManager.recent_workspaces().size(), 1U);
      EXPECT_EQ(reloadedManager.recent_workspaces().front().name, "Sandbox");
      EXPECT_EQ(reloadedManager.recent_workspaces().front().path, createdWorkspace->path);
    }

    TEST(WorkspaceManagerTest, CreateWorkspaceBuildsMissingParentDirectories)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-create-nested");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path parentDirectory = testRoot / "projects" / "gameplay" / "levels";
      const std::filesystem::path storagePath = testRoot / "prefs" / "recent_workspaces.txt";

      WorkspaceManager manager(storagePath);
      std::string errorMessage;
      const auto createdWorkspace = manager.create_workspace(parentDirectory, "Sandbox", &errorMessage);

      ASSERT_TRUE(createdWorkspace.has_value()) << errorMessage;
      EXPECT_TRUE(std::filesystem::exists(parentDirectory));
      EXPECT_TRUE(std::filesystem::exists(createdWorkspace->path));
      EXPECT_TRUE(std::filesystem::is_directory(createdWorkspace->path));
      EXPECT_EQ(createdWorkspace->path, std::filesystem::weakly_canonical(parentDirectory / "Sandbox"));
    }

    TEST(WorkspaceManagerTest, ReopeningWorkspaceMovesItToFrontWithoutDuplicates)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-reopen");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path storagePath = testRoot / "prefs" / "recent_workspaces.txt";
      const std::filesystem::path alphaWorkspace = testRoot / "Alpha";
      const std::filesystem::path betaWorkspace = testRoot / "Beta";
      std::filesystem::create_directories(alphaWorkspace);
      std::filesystem::create_directories(betaWorkspace);

      WorkspaceManager manager(storagePath);
      std::string errorMessage;
      ASSERT_TRUE(manager.open_workspace(alphaWorkspace, &errorMessage).has_value()) << errorMessage;
      ASSERT_TRUE(manager.open_workspace(betaWorkspace, &errorMessage).has_value()) << errorMessage;
      ASSERT_TRUE(manager.open_workspace(alphaWorkspace, &errorMessage).has_value()) << errorMessage;

      ASSERT_EQ(manager.recent_workspaces().size(), 2U);
      EXPECT_EQ(manager.recent_workspaces()[0].name, "Alpha");
      EXPECT_EQ(manager.recent_workspaces()[0].path, std::filesystem::weakly_canonical(alphaWorkspace));
      EXPECT_EQ(manager.recent_workspaces()[1].name, "Beta");
      EXPECT_EQ(manager.recent_workspaces()[1].path, std::filesystem::weakly_canonical(betaWorkspace));
      ASSERT_TRUE(manager.current_workspace().has_value());
      EXPECT_EQ(manager.current_workspace()->path, std::filesystem::weakly_canonical(alphaWorkspace));
    }

    TEST(WorkspaceManagerTest, LoadSkipsMissingRecentWorkspaceFolders)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-load");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path storagePath = testRoot / "prefs" / "recent_workspaces.txt";
      const std::filesystem::path existingWorkspace = testRoot / "Playable";
      const std::filesystem::path missingWorkspace = testRoot / "Missing";
      std::filesystem::create_directories(existingWorkspace);
      std::filesystem::create_directories(storagePath.parent_path());

      {
        std::ofstream output(storagePath);
        output << missingWorkspace.string() << '\n';
        output << existingWorkspace.string() << '\n';
      }

      WorkspaceManager manager(storagePath);
      std::string errorMessage;
      EXPECT_TRUE(manager.load(&errorMessage)) << errorMessage;
      ASSERT_EQ(manager.recent_workspaces().size(), 1U);
      EXPECT_EQ(manager.recent_workspaces().front().name, "Playable");
      EXPECT_EQ(manager.recent_workspaces().front().path, std::filesystem::weakly_canonical(existingWorkspace));
    }

    TEST(WorkspaceManagerTest, PruneMissingRecentWorkspacesRemovesDeletedFolders)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-prune");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path storagePath = testRoot / "prefs" / "recent_workspaces.txt";
      const std::filesystem::path alphaWorkspace = testRoot / "Alpha";
      const std::filesystem::path betaWorkspace = testRoot / "Beta";
      std::filesystem::create_directories(alphaWorkspace);
      std::filesystem::create_directories(betaWorkspace);

      WorkspaceManager manager(storagePath);
      std::string errorMessage;
      ASSERT_TRUE(manager.open_workspace(alphaWorkspace, &errorMessage).has_value()) << errorMessage;
      ASSERT_TRUE(manager.open_workspace(betaWorkspace, &errorMessage).has_value()) << errorMessage;
      ASSERT_EQ(manager.recent_workspaces().size(), 2U);

      std::filesystem::remove_all(betaWorkspace);

      EXPECT_TRUE(manager.prune_missing_recent_workspaces(&errorMessage)) << errorMessage;
      ASSERT_EQ(manager.recent_workspaces().size(), 1U);
      EXPECT_EQ(manager.recent_workspaces().front().path, std::filesystem::weakly_canonical(alphaWorkspace));

      WorkspaceManager reloadedManager(storagePath);
      EXPECT_TRUE(reloadedManager.load(&errorMessage)) << errorMessage;
      ASSERT_EQ(reloadedManager.recent_workspaces().size(), 1U);
      EXPECT_EQ(reloadedManager.recent_workspaces().front().path, std::filesystem::weakly_canonical(alphaWorkspace));
    }

    TEST(WorkspaceFileOperationsTest, CopyFileToDirectoryCopiesExternalFileIntoWorkspace)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-copy");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path workspaceRoot = testRoot / "Workspace";
      const std::filesystem::path sourceRoot = testRoot / "External";
      std::filesystem::create_directories(workspaceRoot);
      std::filesystem::create_directories(sourceRoot);

      const std::filesystem::path sourceFile = sourceRoot / "Settings.json";
      {
        std::ofstream output(sourceFile);
        output << "{ \"volume\": 0.8 }\n";
      }

      std::filesystem::path copiedPath;
      std::string errorMessage;
      ASSERT_TRUE(copy_file_to_directory(sourceFile, workspaceRoot, &copiedPath, &errorMessage)) << errorMessage;

      EXPECT_TRUE(std::filesystem::exists(sourceFile));
      EXPECT_TRUE(std::filesystem::exists(copiedPath));
      EXPECT_EQ(copiedPath, std::filesystem::weakly_canonical(workspaceRoot / "Settings.json"));

      std::ifstream copiedInput(copiedPath);
      std::string copiedContents;
      std::getline(copiedInput, copiedContents);
      EXPECT_EQ(copiedContents, "{ \"volume\": 0.8 }");
    }

    TEST(WorkspaceFileOperationsTest, DeleteWorkspaceItemRemovesDeletedScriptAssignments)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-delete-script");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path workspaceRoot = testRoot / "Workspace";
      const std::filesystem::path scriptsDirectory = workspaceRoot / "Scripts";
      std::filesystem::create_directories(scriptsDirectory);

      const std::filesystem::path moverPath = scriptsDirectory / "Mover.cs";
      {
        std::ofstream output(moverPath);
        output << "public sealed class Mover {}\n";
      }

      EntityManager entityManager;
      ComponentManager componentManager;

      const auto firstEntity = EntityFactory::createCube(entityManager, componentManager);
      const auto secondEntity = EntityFactory::createCube(entityManager, componentManager);

      ScriptComponent firstComponent;
      firstComponent.attachments.push_back(ScriptAttachment{"Scripts/Mover.cs", "Mover", true});
      componentManager.addComponent(firstEntity, firstComponent);

      ScriptComponent secondComponent;
      secondComponent.attachments.push_back(ScriptAttachment{"Scripts/Mover.cs", "Mover", true});
      secondComponent.attachments.push_back(ScriptAttachment{"Scripts/Other.cs", "Other", true});
      componentManager.addComponent(secondEntity, secondComponent);

      WorkspaceDeleteResult deleteResult;
      std::string errorMessage;
      ASSERT_TRUE(delete_workspace_item(
                      workspaceRoot,
                      moverPath,
                      entityManager,
                      componentManager,
                      &deleteResult,
                      &errorMessage))
          << errorMessage;

      EXPECT_FALSE(std::filesystem::exists(moverPath));
      ASSERT_EQ(deleteResult.removedScriptPaths.size(), 1U);
      EXPECT_EQ(deleteResult.removedScriptPaths.front(), "Scripts/Mover.cs");
      EXPECT_EQ(deleteResult.removedScriptAssignments, 2U);
      EXPECT_EQ(deleteResult.affectedScriptComponents, 2U);

      EXPECT_TRUE(componentManager.getComponent<ScriptComponent>(firstEntity).attachments.empty());

      const auto &remainingAttachments = componentManager.getComponent<ScriptComponent>(secondEntity).attachments;
      ASSERT_EQ(remainingAttachments.size(), 1U);
      EXPECT_EQ(remainingAttachments.front().scriptPath, "Scripts/Other.cs");
    }

    TEST(WorkspaceFileOperationsTest, DeleteWorkspaceItemRemovesNestedScriptAssignments)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-workspace-delete-folder");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path workspaceRoot = testRoot / "Workspace";
      const std::filesystem::path aiDirectory = workspaceRoot / "Scripts" / "AI";
      std::filesystem::create_directories(aiDirectory);

      {
        std::ofstream output(aiDirectory / "Chaser.cs");
        output << "public sealed class Chaser {}\n";
      }
      {
        std::ofstream output(aiDirectory / "Lookout.cs");
        output << "public sealed class Lookout {}\n";
      }

      EntityManager entityManager;
      ComponentManager componentManager;

      const auto entity = EntityFactory::createCube(entityManager, componentManager);
      ScriptComponent scriptComponent;
      scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/AI/Chaser.cs", "Chaser", true});
      scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/AI/Lookout.cs", "Lookout", true});
      scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/Patrol.cs", "Patrol", true});
      componentManager.addComponent(entity, scriptComponent);

      WorkspaceDeleteResult deleteResult;
      std::string errorMessage;
      ASSERT_TRUE(delete_workspace_item(
                      workspaceRoot,
                      aiDirectory,
                      entityManager,
                      componentManager,
                      &deleteResult,
                      &errorMessage))
          << errorMessage;

      EXPECT_FALSE(std::filesystem::exists(aiDirectory));
      EXPECT_EQ(deleteResult.removedScriptPaths.size(), 2U);
      EXPECT_EQ(deleteResult.removedScriptAssignments, 2U);
      EXPECT_EQ(deleteResult.affectedScriptComponents, 1U);

      const auto &remainingAttachments = componentManager.getComponent<ScriptComponent>(entity).attachments;
      ASSERT_EQ(remainingAttachments.size(), 1U);
      EXPECT_EQ(remainingAttachments.front().scriptPath, "Scripts/Patrol.cs");
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
      EXPECT_FALSE(model->meshes.front().vertices.empty());
      EXPECT_FALSE(model->meshes.front().triangles.empty());
      EXPECT_EQ(model->meshes.front().vertexCount, model->meshes.front().vertices.size());
      EXPECT_EQ(model->meshes.front().faceCount, model->meshes.front().triangles.size());
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

    TEST(EntityManagerTest, DestroyEntityRemovesItFromActiveList)
    {
      EntityManager entityManager;

      const auto first = entityManager.createEntity();
      const auto second = entityManager.createEntity();
      const auto third = entityManager.createEntity();

      entityManager.destroyEntity(second);

      const auto remainingEntities = entityManager.getAllEntities();
      EXPECT_EQ(remainingEntities.size(), 2U);
      EXPECT_NE(std::find(remainingEntities.begin(), remainingEntities.end(), first), remainingEntities.end());
      EXPECT_EQ(std::find(remainingEntities.begin(), remainingEntities.end(), second), remainingEntities.end());
      EXPECT_NE(std::find(remainingEntities.begin(), remainingEntities.end(), third), remainingEntities.end());
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

    TEST(MainCameraSelectionTest, FiltersMainCameraSelectionByWorld)
    {
      EntityManager entityManager;
      ComponentManager componentManager;

      const auto firstWorld = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
      const auto secondWorld = EntityFactory::createWorld(entityManager, componentManager, "World2", false);
      const auto firstCamera = EntityFactory::createCamera(entityManager, componentManager, firstWorld);
      const auto secondCamera = EntityFactory::createCamera(entityManager, componentManager, secondWorld);

      componentManager.getComponent<CameraComponent>(firstCamera).isMainCamera = true;
      componentManager.getComponent<CameraComponent>(secondCamera).isMainCamera = true;

      const auto worldOneSelection = select_main_camera(entityManager, componentManager, firstWorld);
      ASSERT_EQ(worldOneSelection.status, MainCameraSelectionStatus::Ready);
      ASSERT_TRUE(worldOneSelection.entity.has_value());
      EXPECT_EQ(worldOneSelection.entity.value(), firstCamera);

      const auto worldTwoSelection = select_main_camera(entityManager, componentManager, secondWorld);
      ASSERT_EQ(worldTwoSelection.status, MainCameraSelectionStatus::Ready);
      ASSERT_TRUE(worldTwoSelection.entity.has_value());
      EXPECT_EQ(worldTwoSelection.entity.value(), secondCamera);

      const auto unfilteredSelection = select_main_camera(entityManager, componentManager);
      EXPECT_EQ(unfilteredSelection.status, MainCameraSelectionStatus::MultipleMainCamerasSelected);
    }

    TEST(ScriptRuntimeTest, StartWithoutAttachedScriptsDoesNothing)
    {
      EntityManager entityManager;
      ComponentManager componentManager;
      ScriptRuntime scriptRuntime;

      EntityFactory::createCube(entityManager, componentManager);

      std::string errorMessage;
      EXPECT_TRUE(scriptRuntime.start(componentManager, entityManager, &errorMessage));
      EXPECT_FALSE(scriptRuntime.is_running());
      EXPECT_FALSE(scriptRuntime.faulted());
      EXPECT_TRUE(errorMessage.empty());
    }

    TEST(ScriptRuntimeTest, StartRejectsMissingScriptFile)
    {
      EntityManager entityManager;
      ComponentManager componentManager;
      ScriptRuntime scriptRuntime;

      const auto cube = EntityFactory::createCube(entityManager, componentManager);
      ScriptComponent scriptComponent;
      scriptComponent.attachments.push_back(ScriptAttachment{
          "missing-script.cs",
          "MissingScript",
          true});
      componentManager.addComponent(cube, scriptComponent);

      std::string errorMessage;
      EXPECT_FALSE(scriptRuntime.start(componentManager, entityManager, &errorMessage));
      EXPECT_FALSE(scriptRuntime.is_running());
      EXPECT_TRUE(scriptRuntime.faulted());
      EXPECT_NE(errorMessage.find("Script file does not exist"), std::string::npos);
    }

    TEST(ScriptRuntimeTest, StartResolvesRelativeScriptFileFromWorkspaceRoot)
    {
      const std::filesystem::path testRoot = unique_test_directory("hades-script-workspace");
      ScopedDirectoryCleanup cleanup(testRoot);

      const std::filesystem::path workspaceRoot = testRoot / "Workspace";
      const std::filesystem::path scriptsDirectory = workspaceRoot / "Scripts";
      std::filesystem::create_directories(scriptsDirectory);

      const std::filesystem::path scriptPath = scriptsDirectory / "Mover.cs";
      {
        std::ofstream output(scriptPath);
        output << "using Hades.Scripting;\n";
        output << "public sealed class Mover : HadesScript {}\n";
      }

      EntityManager entityManager;
      ComponentManager componentManager;
      ScriptRuntime scriptRuntime;

      const auto cube = EntityFactory::createCube(entityManager, componentManager);
      ScriptComponent scriptComponent;
      scriptComponent.attachments.push_back(ScriptAttachment{
          "Scripts/Mover.cs",
          "Mover",
          true});
      componentManager.addComponent(cube, scriptComponent);

      std::string errorMessage;
      const bool started = scriptRuntime.start(componentManager, entityManager, workspaceRoot, &errorMessage);
      if (!started)
      {
        EXPECT_EQ(errorMessage.find("Script file does not exist"), std::string::npos);
      }
    }
  }
}
