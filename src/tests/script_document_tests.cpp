#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "test_support.hpp"

#include "../editor/script_document.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  TEST(ScriptDocumentTest, LoadScriptDocumentReadsScriptContents)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-document-load");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path scriptPath = testRoot / "Scripts" / "Mover.cs";
    std::filesystem::create_directories(scriptPath.parent_path());
    {
      std::ofstream output(scriptPath, std::ios::binary);
      output << "using Hades.Scripting;\n";
      output << "public sealed class Mover : HadesScript {}\n";
    }

    ScriptDocumentSnapshot snapshot;
    std::string errorMessage;
    ASSERT_TRUE(load_script_document(scriptPath, snapshot, &errorMessage)) << errorMessage;
    EXPECT_EQ(
        snapshot.contents,
        "using Hades.Scripting;\npublic sealed class Mover : HadesScript {}\n");
    EXPECT_TRUE(snapshot.hasLastWriteTime);
  }

  TEST(ScriptDocumentTest, SaveScriptDocumentPersistsContents)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-document-save");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path scriptPath = testRoot / "Scripts" / "Walker.cs";
    std::filesystem::create_directories(scriptPath.parent_path());

    ScriptDocumentSnapshot snapshot;
    std::string errorMessage;
    ASSERT_TRUE(save_script_document(
                    scriptPath,
                    "using Hades.Scripting;\npublic sealed class Walker : HadesScript {}\n",
                    &snapshot,
                    &errorMessage))
        << errorMessage;
    EXPECT_TRUE(snapshot.hasLastWriteTime);

    std::ifstream input(scriptPath, std::ios::binary);
    const std::string fileContents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    EXPECT_EQ(
        fileContents,
        "using Hades.Scripting;\npublic sealed class Walker : HadesScript {}\n");
  }

  TEST(ScriptDocumentTest, LoadScriptDocumentReportsMissingFiles)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-document-missing");
    ScopedDirectoryCleanup cleanup(testRoot);

    ScriptDocumentSnapshot snapshot;
    std::string errorMessage;
    EXPECT_FALSE(load_script_document(testRoot / "Missing.cs", snapshot, &errorMessage));
    EXPECT_TRUE(snapshot.contents.empty());
    EXPECT_FALSE(errorMessage.empty());
  }
}
