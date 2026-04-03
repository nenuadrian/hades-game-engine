#include <gtest/gtest.h>

#include "../engine/rendering/vector_text.hpp"

namespace hades
{
  TEST(VectorTextTest, LayoutWrapsAndReportsMetrics)
  {
    const VectorTextLayout layout = layout_vector_text(
        "HELLO WORLD",
        VectorTextStyle{1.0f, 4.0f, 1.25f});

    EXPECT_GT(layout.lineCount, 1);
    EXPECT_LE(layout.width, 4.0f + 0.001f);
    EXPECT_GT(layout.height, 1.0f);
    EXPECT_FALSE(layout.segments.empty());
  }

  TEST(VectorTextTest, EulerFrameProducesWorldSpaceAxes)
  {
    const VectorTextFrame3D frame = make_vector_text_frame_from_euler(
        VectorTextPoint3D{1.0f, 2.0f, 3.0f},
        90.0f,
        0.0f,
        0.0f);

    EXPECT_FLOAT_EQ(frame.origin.x, 1.0f);
    EXPECT_FLOAT_EQ(frame.origin.y, 2.0f);
    EXPECT_FLOAT_EQ(frame.origin.z, 3.0f);
    EXPECT_NEAR(frame.right.x, 0.0f, 0.001f);
    EXPECT_NEAR(frame.right.y, 0.0f, 0.001f);
    EXPECT_NEAR(frame.right.z, -1.0f, 0.001f);
    EXPECT_NEAR(frame.up.x, 0.0f, 0.001f);
    EXPECT_NEAR(frame.up.y, 1.0f, 0.001f);
    EXPECT_NEAR(frame.up.z, 0.0f, 0.001f);
  }
}
