#ifndef HADES_ENGINE_RENDERING_VECTOR_TEXT_HPP
#define HADES_ENGINE_RENDERING_VECTOR_TEXT_HPP

#include <string_view>
#include <vector>

namespace hades
{
  struct VectorTextPoint2D
  {
    float x = 0.0f;
    float y = 0.0f;
  };

  struct VectorTextPoint3D
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct VectorTextSegment2D
  {
    VectorTextPoint2D start;
    VectorTextPoint2D end;
  };

  struct VectorTextSegment3D
  {
    VectorTextPoint3D start;
    VectorTextPoint3D end;
  };

  struct VectorTextStyle
  {
    float characterHeight = 1.0f;
    float wrapWidth = 0.0f;
    float lineSpacing = 1.25f;
  };

  struct VectorTextLayout
  {
    std::vector<VectorTextSegment2D> segments;
    float width = 0.0f;
    float height = 0.0f;
    int lineCount = 0;
  };

  struct VectorTextFrame3D
  {
    VectorTextPoint3D origin;
    VectorTextPoint3D right;
    VectorTextPoint3D up;
    float anchorX = 0.5f;
    float anchorY = 0.5f;
  };

  struct VectorTextGeometry3D
  {
    std::vector<VectorTextSegment3D> segments;
    float width = 0.0f;
    float height = 0.0f;
    int lineCount = 0;
  };

  VectorTextFrame3D make_vector_text_frame_from_euler(
      const VectorTextPoint3D &origin,
      float yawDegrees,
      float pitchDegrees,
      float rollDegrees,
      float anchorX = 0.5f,
      float anchorY = 0.5f);
  VectorTextLayout layout_vector_text(std::string_view text, const VectorTextStyle &style);
  VectorTextGeometry3D build_vector_text_geometry(
      std::string_view text,
      const VectorTextStyle &style,
      const VectorTextFrame3D &frame);
}

#endif
