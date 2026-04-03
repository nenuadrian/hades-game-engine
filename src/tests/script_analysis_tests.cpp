#include <fstream>

#include <gtest/gtest.h>

#include "../editor/script_analysis.hpp"
#include "test_support.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  TEST(ScriptAnalysisTest, ParsesScriptClassesAndDefaultsToQualifiedNames)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-analysis-classes");
    const ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const std::filesystem::path scriptPath = testRoot / "Mover.cs";
    {
      std::ofstream output(scriptPath);
      output << "namespace Gameplay.Motion;\n"
                "\n"
                "using Hades.Scripting;\n"
                "\n"
                "public sealed class MoveAlongX : HadesScript\n"
                "{\n"
                "    public float Speed;\n"
                "}\n"
                "\n"
                "public sealed class MoveAlongY : Hades.Scripting.HadesScript\n"
                "{\n"
                "    public float Amount;\n"
                "}\n";
    }

    const auto classes = parse_script_classes(scriptPath);
    ASSERT_EQ(classes.size(), 2u);
    EXPECT_EQ(classes[0].qualifiedName, "Gameplay.Motion.MoveAlongX");
    EXPECT_EQ(classes[0].simpleName, "MoveAlongX");
    EXPECT_EQ(classes[1].qualifiedName, "Gameplay.Motion.MoveAlongY");
    EXPECT_EQ(classes[1].simpleName, "MoveAlongY");
  }

  TEST(ScriptAnalysisTest, FindsSelectedClassAndReturnsFieldsForThatClass)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-analysis-fields");
    const ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const std::filesystem::path scriptPath = testRoot / "EnemyLogic.cs";
    {
      std::ofstream output(scriptPath);
      output << "using Hades.Scripting;\n"
                "\n"
                "public sealed class Patrol : HadesScript\n"
                "{\n"
                "    public float Speed;\n"
                "}\n"
                "\n"
                "public sealed class Lookout : HadesScript\n"
                "{\n"
                "    public int Range;\n"
                "    public string Label;\n"
                "}\n";
    }

    const auto classes = parse_script_classes(scriptPath);
    const auto *lookout = find_script_class(classes, "Lookout");
    ASSERT_NE(lookout, nullptr);
    ASSERT_EQ(lookout->publicFields.size(), 2u);
    EXPECT_EQ(lookout->publicFields[0].first, "int");
    EXPECT_EQ(lookout->publicFields[0].second, "Range");
    EXPECT_EQ(lookout->publicFields[1].first, "string");
    EXPECT_EQ(lookout->publicFields[1].second, "Label");
  }
}
