#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_support.hpp"

#include "../editor/editor_settings.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  TEST(EditorSettingsTest, SaveAndLoadWorkspaceSettingsRoundTrip)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-editor-settings-roundtrip");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    std::filesystem::create_directories(workspaceRoot);

    WorkspaceEditorSettings savedSettings;
    savedSettings.showDebugInfo = true;
    savedSettings.sceneCameraTargetX = 4.5f;
    savedSettings.sceneCameraTargetY = -2.0f;
    savedSettings.sceneCameraTargetZ = 9.25f;
    savedSettings.sceneCameraDistance = 18.0f;
    savedSettings.sceneCameraYawDegrees = 135.0f;
    savedSettings.sceneCameraPitchDegrees = -25.0f;
    savedSettings.sceneGameView = true;
    savedSettings.pluginVisibility["workspace"] = true;
    savedSettings.pluginVisibility["entities"] = false;

    std::string errorMessage;
    ASSERT_TRUE(save_workspace_settings(workspaceRoot, savedSettings, &errorMessage)) << errorMessage;
    EXPECT_TRUE(std::filesystem::exists(workspace_settings_path(workspaceRoot)));

    WorkspaceEditorSettings loadedSettings;
    ASSERT_TRUE(load_workspace_settings(workspaceRoot, loadedSettings, &errorMessage)) << errorMessage;
    EXPECT_TRUE(errorMessage.empty()) << errorMessage;
    EXPECT_TRUE(loadedSettings.showDebugInfo);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraTargetX, 4.5f);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraTargetY, -2.0f);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraTargetZ, 9.25f);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraDistance, 18.0f);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraYawDegrees, 135.0f);
    EXPECT_FLOAT_EQ(loadedSettings.sceneCameraPitchDegrees, -25.0f);
    EXPECT_TRUE(loadedSettings.sceneGameView);
    ASSERT_EQ(loadedSettings.pluginVisibility.size(), 2U);
    EXPECT_TRUE(loadedSettings.pluginVisibility["workspace"]);
    EXPECT_FALSE(loadedSettings.pluginVisibility["entities"]);
  }

  TEST(EditorSettingsTest, LoadWorkspaceSettingsPreservesDefaultsWhenDocumentIsMissingFields)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-editor-settings-defaults");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    const std::filesystem::path settingsPath = workspace_settings_path(workspaceRoot);
    std::filesystem::create_directories(settingsPath.parent_path());

    {
      std::ofstream output(settingsPath);
      output << R"json({
  "version": 1,
  "editor": {
    "showDebugInfo": true
  },
  "plugins": {
    "workspace": false
  }
}
)json";
    }

    WorkspaceEditorSettings settings;
    settings.showDebugInfo = false;
    settings.sceneCameraTargetX = 10.0f;
    settings.sceneCameraTargetY = 20.0f;
    settings.sceneCameraTargetZ = 30.0f;
    settings.sceneCameraDistance = 40.0f;
    settings.sceneCameraYawDegrees = 50.0f;
    settings.sceneCameraPitchDegrees = 60.0f;
    settings.pluginVisibility["entities"] = true;

    std::string errorMessage;
    ASSERT_TRUE(load_workspace_settings(workspaceRoot, settings, &errorMessage)) << errorMessage;
    EXPECT_TRUE(settings.showDebugInfo);
    EXPECT_FLOAT_EQ(settings.sceneCameraTargetX, 10.0f);
    EXPECT_FLOAT_EQ(settings.sceneCameraTargetY, 20.0f);
    EXPECT_FLOAT_EQ(settings.sceneCameraTargetZ, 30.0f);
    EXPECT_FLOAT_EQ(settings.sceneCameraDistance, 40.0f);
    EXPECT_FLOAT_EQ(settings.sceneCameraYawDegrees, 50.0f);
    EXPECT_FLOAT_EQ(settings.sceneCameraPitchDegrees, 60.0f);
    EXPECT_FALSE(settings.pluginVisibility["workspace"]);
    EXPECT_TRUE(settings.pluginVisibility["entities"]);
  }
}
