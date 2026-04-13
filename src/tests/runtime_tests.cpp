#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_support.hpp"

#include "../engine/components/camera_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/runtime/main_camera_selection.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

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
        "missing-script.cpp",
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

    const std::filesystem::path scriptPath = scriptsDirectory / "Mover.cpp";
    {
      std::ofstream output(scriptPath);
      output << "#include \"engine/runtime/hades_script.hpp\"\n";
      output << "#include \"engine/runtime/hades_script_registration.hpp\"\n\n";
      output << "class Mover : public hades::HadesScript {};\n";
      output << "HADES_REGISTER_SCRIPT(Mover)\n";
    }

    EntityManager entityManager;
    ComponentManager componentManager;
    ScriptRuntime scriptRuntime;

    const auto cube = EntityFactory::createCube(entityManager, componentManager);
    ScriptComponent scriptComponent;
    scriptComponent.attachments.push_back(ScriptAttachment{
        "Scripts/Mover.cpp",
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
