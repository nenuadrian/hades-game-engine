#include <fstream>

#include <gtest/gtest.h>

#include "../editor/script_analysis.hpp"
#include "test_support.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  TEST(ScriptAnalysisTest, ParsesRegisteredScriptClasses)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-analysis-classes");
    const ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const std::filesystem::path scriptPath = testRoot / "Movement.cpp";
    {
      std::ofstream output(scriptPath);
      output << "#include \"engine/runtime/hades_script.hpp\"\n"
                "#include \"engine/runtime/hades_script_registration.hpp\"\n"
                "\n"
                "class MoveAlongX : public hades::HadesScript {};\n"
                "HADES_REGISTER_SCRIPT(MoveAlongX)\n"
                "\n"
                "class MoveAlongY : public hades::HadesScript {};\n"
                "HADES_REGISTER_SCRIPT(MoveAlongY)\n";
    }

    const auto classes = parse_script_classes(scriptPath);
    ASSERT_EQ(classes.size(), 2u);
    EXPECT_EQ(classes[0].qualifiedName, "MoveAlongX");
    EXPECT_EQ(classes[0].simpleName, "MoveAlongX");
    EXPECT_EQ(classes[1].qualifiedName, "MoveAlongY");
    EXPECT_EQ(classes[1].simpleName, "MoveAlongY");
  }

  TEST(ScriptAnalysisTest, FindsSelectedClassByName)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-script-analysis-find");
    const ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const std::filesystem::path scriptPath = testRoot / "EnemyLogic.cpp";
    {
      std::ofstream output(scriptPath);
      output << "#include \"engine/runtime/hades_script.hpp\"\n"
                "#include \"engine/runtime/hades_script_registration.hpp\"\n"
                "\n"
                "class Patrol : public hades::HadesScript {};\n"
                "HADES_REGISTER_SCRIPT(Patrol)\n"
                "\n"
                "class Lookout : public hades::HadesScript {};\n"
                "HADES_REGISTER_SCRIPT(Lookout)\n";
    }

    const auto classes = parse_script_classes(scriptPath);
    const auto *lookout = find_script_class(classes, "Lookout");
    ASSERT_NE(lookout, nullptr);
    EXPECT_EQ(lookout->simpleName, "Lookout");

    const auto *missing = find_script_class(classes, "Nonexistent");
    EXPECT_EQ(missing, nullptr);
  }
}
