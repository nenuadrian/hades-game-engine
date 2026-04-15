#include <gtest/gtest.h>

#include <filesystem>
#include <thread>
#include <vector>

#include "test_support.hpp"

#include "../engine/runtime/policy_registry.hpp"

namespace hades
{
  namespace
  {
    hne::SpaceSpec box4()
    {
      hne::BoxSpace s;
      s.shape = {4};
      s.low = {-1.0f, -1.0f, -1.0f, -1.0f};
      s.high = {1.0f, 1.0f, 1.0f, 1.0f};
      return s;
    }

    hne::SpaceSpec box6()
    {
      hne::BoxSpace s;
      s.shape = {6};
      s.low.assign(6, -1.0f);
      s.high.assign(6, 1.0f);
      return s;
    }

    hne::SpaceSpec discrete(int32_t n)
    {
      hne::DiscreteSpace d;
      d.n = n;
      return d;
    }
  }

  TEST(PolicyRegistryTest, SpecEqualsStructurally)
  {
    EXPECT_TRUE(spec_equals(box4(), box4()));
    EXPECT_FALSE(spec_equals(box4(), box6()));
    EXPECT_TRUE(spec_equals(discrete(2), discrete(2)));
    EXPECT_FALSE(spec_equals(discrete(2), discrete(3)));
    EXPECT_FALSE(spec_equals(discrete(2), box4()));
  }

  TEST(PolicyRegistryTest, DescribeSpaceProducesReadableStrings)
  {
    const auto box = describe_space(box4());
    EXPECT_NE(box.find("Box"), std::string::npos);
    EXPECT_NE(box.find("shape=[4]"), std::string::npos);

    const auto d = describe_space(discrete(5));
    EXPECT_NE(d.find("Discrete"), std::string::npos);
    EXPECT_NE(d.find("n=5"), std::string::npos);
  }

  TEST(PolicyRegistryTest, MissingFileFailsLoudlyAndIsMemoized)
  {
    PolicyRegistry registry;
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() /
        "hades_policy_registry_missing_path_xyz.pt";
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    auto first = registry.get_validated(missing, box4(), discrete(2));
    EXPECT_EQ(first.runtime, nullptr);
    EXPECT_FALSE(first.error.empty());

    // Second call returns the same error without re-attempting to touch the
    // filesystem. We can't directly observe the cache hit, but the error
    // string must be stable.
    auto second = registry.get_validated(missing, box4(), discrete(2));
    EXPECT_EQ(second.runtime, nullptr);
    EXPECT_EQ(first.error, second.error);
  }

  TEST(PolicyRegistryTest, ConcurrentGetValidatedIsSafe)
  {
    PolicyRegistry registry;
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() /
        "hades_policy_registry_concurrent.pt";
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
      threads.emplace_back([&registry, &missing]()
                           {
        for (int k = 0; k < 32; ++k)
        {
          auto r = registry.get_validated(missing, box4(), discrete(2));
          (void)r;
        }
      });
    }
    for (auto &t : threads)
    {
      t.join();
    }
    SUCCEED();
  }

  TEST(PolicyRegistryTest, ClearDropsMemoizedFailures)
  {
    PolicyRegistry registry;
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() /
        "hades_policy_registry_clear.pt";
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    (void)registry.get_validated(missing, box4(), discrete(2));
    registry.clear();
    // Same call after clear should still fail with the same shape of error
    // (the file is still missing) — the point is that clear() doesn't crash.
    auto after = registry.get_validated(missing, box4(), discrete(2));
    EXPECT_EQ(after.runtime, nullptr);
    EXPECT_FALSE(after.error.empty());
  }
}
