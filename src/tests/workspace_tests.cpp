#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_support.hpp"

#include "../editor/workspace_file_operations.hpp"
#include "../editor/workspace_manager.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

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

    const std::filesystem::path moverPath = scriptsDirectory / "Mover.cpp";
    {
      std::ofstream output(moverPath);
      output << "public sealed class Mover {}\n";
    }

    EntityManager entityManager;
    ComponentManager componentManager;

    const auto firstEntity = EntityFactory::createCube(entityManager, componentManager);
    const auto secondEntity = EntityFactory::createCube(entityManager, componentManager);

    ScriptComponent firstComponent;
    firstComponent.attachments.push_back(ScriptAttachment{"Scripts/Mover.cpp", "Mover", true});
    componentManager.addComponent(firstEntity, firstComponent);

    ScriptComponent secondComponent;
    secondComponent.attachments.push_back(ScriptAttachment{"Scripts/Mover.cpp", "Mover", true});
    secondComponent.attachments.push_back(ScriptAttachment{"Scripts/Other.cpp", "Other", true});
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
    EXPECT_EQ(deleteResult.removedScriptPaths.front(), "Scripts/Mover.cpp");
    EXPECT_EQ(deleteResult.removedScriptAssignments, 2U);
    EXPECT_EQ(deleteResult.affectedScriptComponents, 2U);

    EXPECT_TRUE(componentManager.getComponent<ScriptComponent>(firstEntity).attachments.empty());

    const auto &remainingAttachments = componentManager.getComponent<ScriptComponent>(secondEntity).attachments;
    ASSERT_EQ(remainingAttachments.size(), 1U);
    EXPECT_EQ(remainingAttachments.front().scriptPath, "Scripts/Other.cpp");
  }

  TEST(WorkspaceFileOperationsTest, DeleteWorkspaceItemRemovesNestedScriptAssignments)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-workspace-delete-folder");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    const std::filesystem::path aiDirectory = workspaceRoot / "Scripts" / "AI";
    std::filesystem::create_directories(aiDirectory);

    {
      std::ofstream output(aiDirectory / "Chaser.cpp");
      output << "public sealed class Chaser {}\n";
    }
    {
      std::ofstream output(aiDirectory / "Lookout.cpp");
      output << "public sealed class Lookout {}\n";
    }

    EntityManager entityManager;
    ComponentManager componentManager;

    const auto entity = EntityFactory::createCube(entityManager, componentManager);
    ScriptComponent scriptComponent;
    scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/AI/Chaser.cpp", "Chaser", true});
    scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/AI/Lookout.cpp", "Lookout", true});
    scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/Patrol.cpp", "Patrol", true});
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
    EXPECT_EQ(remainingAttachments.front().scriptPath, "Scripts/Patrol.cpp");
  }
}
