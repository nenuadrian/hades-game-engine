#include "vector_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string>

namespace hades
{
  namespace
  {
    constexpr float PI = 3.14159265358979323846f;

    struct GlyphStroke
    {
      float x1;
      float y1;
      float x2;
      float y2;
    };

    struct GlyphDefinition
    {
      const GlyphStroke *strokes = nullptr;
      std::size_t strokeCount = 0;
      float advance = 1.0f;
    };

#define GLYPH_STROKE(x1, y1, x2, y2) \
  { x1##f, y1##f, x2##f, y2##f }

    constexpr GlyphStroke GLYPH_A[] = {
        GLYPH_STROKE(0.00, 1.00, 0.50, 0.00),
        GLYPH_STROKE(0.50, 0.00, 1.00, 1.00),
        GLYPH_STROKE(0.22, 0.56, 0.78, 0.56),
    };
    constexpr GlyphStroke GLYPH_B[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.70, 0.00),
        GLYPH_STROKE(0.70, 0.00, 0.92, 0.20),
        GLYPH_STROKE(0.92, 0.20, 0.92, 0.38),
        GLYPH_STROKE(0.00, 0.50, 0.72, 0.50),
        GLYPH_STROKE(0.72, 0.50, 0.94, 0.70),
        GLYPH_STROKE(0.94, 0.70, 0.94, 0.82),
        GLYPH_STROKE(0.00, 1.00, 0.70, 1.00),
    };
    constexpr GlyphStroke GLYPH_C[] = {
        GLYPH_STROKE(0.92, 0.08, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.18, 0.00),
        GLYPH_STROKE(0.18, 0.00, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.72, 1.00),
        GLYPH_STROKE(0.72, 1.00, 0.92, 0.92),
    };
    constexpr GlyphStroke GLYPH_D[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.62, 0.00),
        GLYPH_STROKE(0.62, 0.00, 0.94, 0.22),
        GLYPH_STROKE(0.94, 0.22, 0.94, 0.78),
        GLYPH_STROKE(0.94, 0.78, 0.62, 1.00),
        GLYPH_STROKE(0.00, 1.00, 0.62, 1.00),
    };
    constexpr GlyphStroke GLYPH_E[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.92, 0.00),
        GLYPH_STROKE(0.00, 0.50, 0.72, 0.50),
        GLYPH_STROKE(0.00, 1.00, 0.92, 1.00),
    };
    constexpr GlyphStroke GLYPH_F[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.92, 0.00),
        GLYPH_STROKE(0.00, 0.50, 0.72, 0.50),
    };
    constexpr GlyphStroke GLYPH_G[] = {
        GLYPH_STROKE(0.92, 0.08, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.18, 0.00),
        GLYPH_STROKE(0.18, 0.00, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.72, 1.00),
        GLYPH_STROKE(0.72, 1.00, 0.92, 0.92),
        GLYPH_STROKE(0.92, 0.60, 0.92, 0.92),
        GLYPH_STROKE(0.92, 0.60, 0.56, 0.60),
    };
    constexpr GlyphStroke GLYPH_H[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(1.00, 0.00, 1.00, 1.00),
        GLYPH_STROKE(0.00, 0.50, 1.00, 0.50),
    };
    constexpr GlyphStroke GLYPH_I[] = {
        GLYPH_STROKE(0.00, 0.00, 0.90, 0.00),
        GLYPH_STROKE(0.45, 0.00, 0.45, 1.00),
        GLYPH_STROKE(0.00, 1.00, 0.90, 1.00),
    };
    constexpr GlyphStroke GLYPH_J[] = {
        GLYPH_STROKE(0.10, 0.00, 0.92, 0.00),
        GLYPH_STROKE(0.74, 0.00, 0.74, 0.84),
        GLYPH_STROKE(0.74, 0.84, 0.52, 1.00),
        GLYPH_STROKE(0.52, 1.00, 0.20, 1.00),
        GLYPH_STROKE(0.20, 1.00, 0.00, 0.82),
    };
    constexpr GlyphStroke GLYPH_K[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(1.00, 0.00, 0.00, 0.56),
        GLYPH_STROKE(0.26, 0.44, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_L[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 1.00, 0.92, 1.00),
    };
    constexpr GlyphStroke GLYPH_M[] = {
        GLYPH_STROKE(0.00, 1.00, 0.00, 0.00),
        GLYPH_STROKE(0.00, 0.00, 0.50, 0.46),
        GLYPH_STROKE(0.50, 0.46, 1.00, 0.00),
        GLYPH_STROKE(1.00, 0.00, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_N[] = {
        GLYPH_STROKE(0.00, 1.00, 0.00, 0.00),
        GLYPH_STROKE(0.00, 0.00, 1.00, 1.00),
        GLYPH_STROKE(1.00, 1.00, 1.00, 0.00),
    };
    constexpr GlyphStroke GLYPH_O[] = {
        GLYPH_STROKE(0.18, 0.00, 0.82, 0.00),
        GLYPH_STROKE(0.82, 0.00, 1.00, 0.18),
        GLYPH_STROKE(1.00, 0.18, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.18, 0.00),
    };
    constexpr GlyphStroke GLYPH_P[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.94, 0.20),
        GLYPH_STROKE(0.94, 0.20, 0.94, 0.40),
        GLYPH_STROKE(0.94, 0.40, 0.72, 0.56),
        GLYPH_STROKE(0.72, 0.56, 0.00, 0.56),
    };
    constexpr GlyphStroke GLYPH_Q[] = {
        GLYPH_STROKE(0.18, 0.00, 0.82, 0.00),
        GLYPH_STROKE(0.82, 0.00, 1.00, 0.18),
        GLYPH_STROKE(1.00, 0.18, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.18, 0.00),
        GLYPH_STROKE(0.58, 0.64, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_R[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 0.00, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.94, 0.20),
        GLYPH_STROKE(0.94, 0.20, 0.94, 0.40),
        GLYPH_STROKE(0.94, 0.40, 0.72, 0.56),
        GLYPH_STROKE(0.72, 0.56, 0.00, 0.56),
        GLYPH_STROKE(0.46, 0.56, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_S[] = {
        GLYPH_STROKE(0.92, 0.08, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.18, 0.00),
        GLYPH_STROKE(0.18, 0.00, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.00, 0.38),
        GLYPH_STROKE(0.00, 0.38, 0.18, 0.50),
        GLYPH_STROKE(0.18, 0.50, 0.72, 0.50),
        GLYPH_STROKE(0.72, 0.50, 0.92, 0.62),
        GLYPH_STROKE(0.92, 0.62, 0.92, 0.82),
        GLYPH_STROKE(0.92, 0.82, 0.72, 1.00),
        GLYPH_STROKE(0.72, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.92),
    };
    constexpr GlyphStroke GLYPH_T[] = {
        GLYPH_STROKE(0.00, 0.00, 1.00, 0.00),
        GLYPH_STROKE(0.50, 0.00, 0.50, 1.00),
    };
    constexpr GlyphStroke GLYPH_U[] = {
        GLYPH_STROKE(0.00, 0.00, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 1.00, 0.00),
    };
    constexpr GlyphStroke GLYPH_V[] = {
        GLYPH_STROKE(0.00, 0.00, 0.50, 1.00),
        GLYPH_STROKE(0.50, 1.00, 1.00, 0.00),
    };
    constexpr GlyphStroke GLYPH_W[] = {
        GLYPH_STROKE(0.00, 0.00, 0.22, 1.00),
        GLYPH_STROKE(0.22, 1.00, 0.50, 0.42),
        GLYPH_STROKE(0.50, 0.42, 0.78, 1.00),
        GLYPH_STROKE(0.78, 1.00, 1.00, 0.00),
    };
    constexpr GlyphStroke GLYPH_X[] = {
        GLYPH_STROKE(0.00, 0.00, 1.00, 1.00),
        GLYPH_STROKE(1.00, 0.00, 0.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_Y[] = {
        GLYPH_STROKE(0.00, 0.00, 0.50, 0.52),
        GLYPH_STROKE(1.00, 0.00, 0.50, 0.52),
        GLYPH_STROKE(0.50, 0.52, 0.50, 1.00),
    };
    constexpr GlyphStroke GLYPH_Z[] = {
        GLYPH_STROKE(0.00, 0.00, 1.00, 0.00),
        GLYPH_STROKE(1.00, 0.00, 0.00, 1.00),
        GLYPH_STROKE(0.00, 1.00, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_0[] = {
        GLYPH_STROKE(0.18, 0.00, 0.82, 0.00),
        GLYPH_STROKE(0.82, 0.00, 1.00, 0.18),
        GLYPH_STROKE(1.00, 0.18, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.18, 0.00),
        GLYPH_STROKE(0.18, 0.18, 0.82, 0.82),
    };
    constexpr GlyphStroke GLYPH_1[] = {
        GLYPH_STROKE(0.44, 0.16, 0.62, 0.00),
        GLYPH_STROKE(0.62, 0.00, 0.62, 1.00),
        GLYPH_STROKE(0.28, 1.00, 0.92, 1.00),
    };
    constexpr GlyphStroke GLYPH_2[] = {
        GLYPH_STROKE(0.16, 0.18, 0.34, 0.00),
        GLYPH_STROKE(0.34, 0.00, 0.78, 0.00),
        GLYPH_STROKE(0.78, 0.00, 0.94, 0.18),
        GLYPH_STROKE(0.94, 0.18, 0.94, 0.38),
        GLYPH_STROKE(0.94, 0.38, 0.00, 1.00),
        GLYPH_STROKE(0.00, 1.00, 0.94, 1.00),
    };
    constexpr GlyphStroke GLYPH_3[] = {
        GLYPH_STROKE(0.10, 0.00, 0.86, 0.00),
        GLYPH_STROKE(0.86, 0.00, 0.86, 1.00),
        GLYPH_STROKE(0.18, 0.50, 0.72, 0.50),
        GLYPH_STROKE(0.10, 1.00, 0.86, 1.00),
    };
    constexpr GlyphStroke GLYPH_4[] = {
        GLYPH_STROKE(0.80, 0.00, 0.80, 1.00),
        GLYPH_STROKE(0.00, 0.56, 1.00, 0.56),
        GLYPH_STROKE(0.00, 0.56, 0.64, 0.00),
    };
    constexpr GlyphStroke GLYPH_5[] = {
        GLYPH_STROKE(0.92, 0.00, 0.16, 0.00),
        GLYPH_STROKE(0.16, 0.00, 0.00, 0.44),
        GLYPH_STROKE(0.00, 0.44, 0.72, 0.44),
        GLYPH_STROKE(0.72, 0.44, 0.92, 0.62),
        GLYPH_STROKE(0.92, 0.62, 0.92, 0.82),
        GLYPH_STROKE(0.92, 0.82, 0.72, 1.00),
        GLYPH_STROKE(0.72, 1.00, 0.16, 1.00),
        GLYPH_STROKE(0.16, 1.00, 0.00, 0.92),
    };
    constexpr GlyphStroke GLYPH_6[] = {
        GLYPH_STROKE(0.92, 0.08, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.18, 0.00),
        GLYPH_STROKE(0.18, 0.00, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.72, 1.00),
        GLYPH_STROKE(0.72, 1.00, 0.92, 0.82),
        GLYPH_STROKE(0.92, 0.82, 0.92, 0.62),
        GLYPH_STROKE(0.92, 0.62, 0.72, 0.44),
        GLYPH_STROKE(0.72, 0.44, 0.00, 0.44),
    };
    constexpr GlyphStroke GLYPH_7[] = {
        GLYPH_STROKE(0.00, 0.00, 1.00, 0.00),
        GLYPH_STROKE(1.00, 0.00, 0.28, 1.00),
    };
    constexpr GlyphStroke GLYPH_8[] = {
        GLYPH_STROKE(0.18, 0.00, 0.82, 0.00),
        GLYPH_STROKE(0.82, 0.00, 1.00, 0.18),
        GLYPH_STROKE(1.00, 0.18, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.82),
        GLYPH_STROKE(0.00, 0.82, 0.00, 0.18),
        GLYPH_STROKE(0.00, 0.18, 0.18, 0.00),
        GLYPH_STROKE(0.00, 0.50, 1.00, 0.50),
    };
    constexpr GlyphStroke GLYPH_9[] = {
        GLYPH_STROKE(0.18, 0.00, 0.82, 0.00),
        GLYPH_STROKE(0.82, 0.00, 1.00, 0.18),
        GLYPH_STROKE(1.00, 0.18, 1.00, 0.82),
        GLYPH_STROKE(1.00, 0.82, 0.82, 1.00),
        GLYPH_STROKE(0.82, 1.00, 0.18, 1.00),
        GLYPH_STROKE(0.18, 1.00, 0.00, 0.92),
        GLYPH_STROKE(0.00, 0.56, 0.18, 0.44),
        GLYPH_STROKE(0.18, 0.44, 1.00, 0.44),
    };
    constexpr GlyphStroke GLYPH_DASH[] = {
        GLYPH_STROKE(0.10, 0.50, 0.90, 0.50),
    };
    constexpr GlyphStroke GLYPH_UNDERSCORE[] = {
        GLYPH_STROKE(0.00, 1.00, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_PLUS[] = {
        GLYPH_STROKE(0.08, 0.50, 0.92, 0.50),
        GLYPH_STROKE(0.50, 0.08, 0.50, 0.92),
    };
    constexpr GlyphStroke GLYPH_EQUALS[] = {
        GLYPH_STROKE(0.12, 0.36, 0.88, 0.36),
        GLYPH_STROKE(0.12, 0.68, 0.88, 0.68),
    };
    constexpr GlyphStroke GLYPH_SLASH[] = {
        GLYPH_STROKE(0.00, 1.00, 1.00, 0.00),
    };
    constexpr GlyphStroke GLYPH_BACKSLASH[] = {
        GLYPH_STROKE(0.00, 0.00, 1.00, 1.00),
    };
    constexpr GlyphStroke GLYPH_PIPE[] = {
        GLYPH_STROKE(0.50, 0.00, 0.50, 1.00),
    };
    constexpr GlyphStroke GLYPH_DOT[] = {
        GLYPH_STROKE(0.46, 0.88, 0.54, 0.96),
        GLYPH_STROKE(0.54, 0.88, 0.46, 0.96),
    };
    constexpr GlyphStroke GLYPH_COMMA[] = {
        GLYPH_STROKE(0.50, 0.84, 0.42, 1.00),
    };
    constexpr GlyphStroke GLYPH_COLON[] = {
        GLYPH_STROKE(0.46, 0.24, 0.54, 0.32),
        GLYPH_STROKE(0.54, 0.24, 0.46, 0.32),
        GLYPH_STROKE(0.46, 0.72, 0.54, 0.80),
        GLYPH_STROKE(0.54, 0.72, 0.46, 0.80),
    };
    constexpr GlyphStroke GLYPH_SEMICOLON[] = {
        GLYPH_STROKE(0.46, 0.24, 0.54, 0.32),
        GLYPH_STROKE(0.54, 0.24, 0.46, 0.32),
        GLYPH_STROKE(0.52, 0.70, 0.42, 1.00),
    };
    constexpr GlyphStroke GLYPH_APOSTROPHE[] = {
        GLYPH_STROKE(0.52, 0.00, 0.40, 0.26),
    };
    constexpr GlyphStroke GLYPH_QUOTE[] = {
        GLYPH_STROKE(0.32, 0.00, 0.24, 0.26),
        GLYPH_STROKE(0.70, 0.00, 0.62, 0.26),
    };
    constexpr GlyphStroke GLYPH_EXCLAMATION[] = {
        GLYPH_STROKE(0.50, 0.00, 0.50, 0.72),
        GLYPH_STROKE(0.46, 0.88, 0.54, 0.96),
        GLYPH_STROKE(0.54, 0.88, 0.46, 0.96),
    };
    constexpr GlyphStroke GLYPH_QUESTION[] = {
        GLYPH_STROKE(0.14, 0.18, 0.34, 0.00),
        GLYPH_STROKE(0.34, 0.00, 0.72, 0.00),
        GLYPH_STROKE(0.72, 0.00, 0.90, 0.18),
        GLYPH_STROKE(0.90, 0.18, 0.90, 0.36),
        GLYPH_STROKE(0.90, 0.36, 0.50, 0.56),
        GLYPH_STROKE(0.50, 0.56, 0.50, 0.72),
        GLYPH_STROKE(0.46, 0.88, 0.54, 0.96),
        GLYPH_STROKE(0.54, 0.88, 0.46, 0.96),
    };
    constexpr GlyphStroke GLYPH_LEFT_PAREN[] = {
        GLYPH_STROKE(0.70, 0.00, 0.38, 0.24),
        GLYPH_STROKE(0.38, 0.24, 0.38, 0.76),
        GLYPH_STROKE(0.38, 0.76, 0.70, 1.00),
    };
    constexpr GlyphStroke GLYPH_RIGHT_PAREN[] = {
        GLYPH_STROKE(0.30, 0.00, 0.62, 0.24),
        GLYPH_STROKE(0.62, 0.24, 0.62, 0.76),
        GLYPH_STROKE(0.62, 0.76, 0.30, 1.00),
    };
    constexpr GlyphStroke GLYPH_LEFT_BRACKET[] = {
        GLYPH_STROKE(0.70, 0.00, 0.34, 0.00),
        GLYPH_STROKE(0.34, 0.00, 0.34, 1.00),
        GLYPH_STROKE(0.34, 1.00, 0.70, 1.00),
    };
    constexpr GlyphStroke GLYPH_RIGHT_BRACKET[] = {
        GLYPH_STROKE(0.30, 0.00, 0.66, 0.00),
        GLYPH_STROKE(0.66, 0.00, 0.66, 1.00),
        GLYPH_STROKE(0.66, 1.00, 0.30, 1.00),
    };
    constexpr GlyphStroke GLYPH_HASH[] = {
        GLYPH_STROKE(0.30, 0.00, 0.18, 1.00),
        GLYPH_STROKE(0.74, 0.00, 0.62, 1.00),
        GLYPH_STROKE(0.00, 0.34, 1.00, 0.34),
        GLYPH_STROKE(0.00, 0.68, 1.00, 0.68),
    };

    GlyphDefinition make_glyph(const GlyphStroke *strokes, std::size_t strokeCount, float advance)
    {
      return GlyphDefinition{strokes, strokeCount, advance};
    }

    GlyphDefinition glyph_for_character(char rawCharacter)
    {
      const unsigned char unsignedCharacter = static_cast<unsigned char>(rawCharacter);
      const char character = static_cast<char>(std::toupper(unsignedCharacter));
      switch (character)
      {
      case 'A':
        return make_glyph(GLYPH_A, std::size(GLYPH_A), 1.08f);
      case 'B':
        return make_glyph(GLYPH_B, std::size(GLYPH_B), 1.06f);
      case 'C':
        return make_glyph(GLYPH_C, std::size(GLYPH_C), 1.08f);
      case 'D':
        return make_glyph(GLYPH_D, std::size(GLYPH_D), 1.10f);
      case 'E':
        return make_glyph(GLYPH_E, std::size(GLYPH_E), 1.00f);
      case 'F':
        return make_glyph(GLYPH_F, std::size(GLYPH_F), 0.98f);
      case 'G':
        return make_glyph(GLYPH_G, std::size(GLYPH_G), 1.12f);
      case 'H':
        return make_glyph(GLYPH_H, std::size(GLYPH_H), 1.10f);
      case 'I':
        return make_glyph(GLYPH_I, std::size(GLYPH_I), 0.82f);
      case 'J':
        return make_glyph(GLYPH_J, std::size(GLYPH_J), 1.00f);
      case 'K':
        return make_glyph(GLYPH_K, std::size(GLYPH_K), 1.08f);
      case 'L':
        return make_glyph(GLYPH_L, std::size(GLYPH_L), 0.94f);
      case 'M':
        return make_glyph(GLYPH_M, std::size(GLYPH_M), 1.18f);
      case 'N':
        return make_glyph(GLYPH_N, std::size(GLYPH_N), 1.12f);
      case 'O':
        return make_glyph(GLYPH_O, std::size(GLYPH_O), 1.12f);
      case 'P':
        return make_glyph(GLYPH_P, std::size(GLYPH_P), 1.04f);
      case 'Q':
        return make_glyph(GLYPH_Q, std::size(GLYPH_Q), 1.14f);
      case 'R':
        return make_glyph(GLYPH_R, std::size(GLYPH_R), 1.08f);
      case 'S':
        return make_glyph(GLYPH_S, std::size(GLYPH_S), 1.02f);
      case 'T':
        return make_glyph(GLYPH_T, std::size(GLYPH_T), 1.02f);
      case 'U':
        return make_glyph(GLYPH_U, std::size(GLYPH_U), 1.10f);
      case 'V':
        return make_glyph(GLYPH_V, std::size(GLYPH_V), 1.08f);
      case 'W':
        return make_glyph(GLYPH_W, std::size(GLYPH_W), 1.24f);
      case 'X':
        return make_glyph(GLYPH_X, std::size(GLYPH_X), 1.06f);
      case 'Y':
        return make_glyph(GLYPH_Y, std::size(GLYPH_Y), 1.02f);
      case 'Z':
        return make_glyph(GLYPH_Z, std::size(GLYPH_Z), 1.02f);
      case '0':
        return make_glyph(GLYPH_0, std::size(GLYPH_0), 1.12f);
      case '1':
        return make_glyph(GLYPH_1, std::size(GLYPH_1), 0.86f);
      case '2':
        return make_glyph(GLYPH_2, std::size(GLYPH_2), 1.04f);
      case '3':
        return make_glyph(GLYPH_3, std::size(GLYPH_3), 1.00f);
      case '4':
        return make_glyph(GLYPH_4, std::size(GLYPH_4), 1.04f);
      case '5':
        return make_glyph(GLYPH_5, std::size(GLYPH_5), 1.04f);
      case '6':
        return make_glyph(GLYPH_6, std::size(GLYPH_6), 1.06f);
      case '7':
        return make_glyph(GLYPH_7, std::size(GLYPH_7), 1.00f);
      case '8':
        return make_glyph(GLYPH_8, std::size(GLYPH_8), 1.08f);
      case '9':
        return make_glyph(GLYPH_9, std::size(GLYPH_9), 1.06f);
      case '-':
        return make_glyph(GLYPH_DASH, std::size(GLYPH_DASH), 0.82f);
      case '_':
        return make_glyph(GLYPH_UNDERSCORE, std::size(GLYPH_UNDERSCORE), 0.98f);
      case '+':
        return make_glyph(GLYPH_PLUS, std::size(GLYPH_PLUS), 0.98f);
      case '=':
        return make_glyph(GLYPH_EQUALS, std::size(GLYPH_EQUALS), 0.98f);
      case '/':
        return make_glyph(GLYPH_SLASH, std::size(GLYPH_SLASH), 0.98f);
      case '\\':
        return make_glyph(GLYPH_BACKSLASH, std::size(GLYPH_BACKSLASH), 0.98f);
      case '|':
        return make_glyph(GLYPH_PIPE, std::size(GLYPH_PIPE), 0.56f);
      case '.':
        return make_glyph(GLYPH_DOT, std::size(GLYPH_DOT), 0.42f);
      case ',':
        return make_glyph(GLYPH_COMMA, std::size(GLYPH_COMMA), 0.42f);
      case ':':
        return make_glyph(GLYPH_COLON, std::size(GLYPH_COLON), 0.42f);
      case ';':
        return make_glyph(GLYPH_SEMICOLON, std::size(GLYPH_SEMICOLON), 0.42f);
      case '\'':
        return make_glyph(GLYPH_APOSTROPHE, std::size(GLYPH_APOSTROPHE), 0.34f);
      case '"':
        return make_glyph(GLYPH_QUOTE, std::size(GLYPH_QUOTE), 0.56f);
      case '!':
        return make_glyph(GLYPH_EXCLAMATION, std::size(GLYPH_EXCLAMATION), 0.46f);
      case '?':
        return make_glyph(GLYPH_QUESTION, std::size(GLYPH_QUESTION), 0.98f);
      case '(':
        return make_glyph(GLYPH_LEFT_PAREN, std::size(GLYPH_LEFT_PAREN), 0.56f);
      case ')':
        return make_glyph(GLYPH_RIGHT_PAREN, std::size(GLYPH_RIGHT_PAREN), 0.56f);
      case '[':
        return make_glyph(GLYPH_LEFT_BRACKET, std::size(GLYPH_LEFT_BRACKET), 0.56f);
      case ']':
        return make_glyph(GLYPH_RIGHT_BRACKET, std::size(GLYPH_RIGHT_BRACKET), 0.56f);
      case '#':
        return make_glyph(GLYPH_HASH, std::size(GLYPH_HASH), 1.04f);
      case ' ':
        return make_glyph(nullptr, 0U, 0.56f);
      default:
        return make_glyph(GLYPH_QUESTION, std::size(GLYPH_QUESTION), 0.98f);
      }
    }

    float scaled_advance(char character, float characterHeight)
    {
      return glyph_for_character(character).advance * characterHeight;
    }

    float degrees_to_radians(float degrees)
    {
      return degrees * (PI / 180.0f);
    }

    VectorTextPoint3D make_point3d(float x, float y, float z)
    {
      return VectorTextPoint3D{x, y, z};
    }

    VectorTextPoint3D add_point3d(const VectorTextPoint3D &lhs, const VectorTextPoint3D &rhs)
    {
      return make_point3d(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
    }

    VectorTextPoint3D subtract_point3d(const VectorTextPoint3D &lhs, const VectorTextPoint3D &rhs)
    {
      return make_point3d(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
    }

    VectorTextPoint3D scale_point3d(const VectorTextPoint3D &point, float scale)
    {
      return make_point3d(point.x * scale, point.y * scale, point.z * scale);
    }

    float dot_point3d(const VectorTextPoint3D &lhs, const VectorTextPoint3D &rhs)
    {
      return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
    }

    VectorTextPoint3D cross_point3d(const VectorTextPoint3D &lhs, const VectorTextPoint3D &rhs)
    {
      return make_point3d(
          (lhs.y * rhs.z) - (lhs.z * rhs.y),
          (lhs.z * rhs.x) - (lhs.x * rhs.z),
          (lhs.x * rhs.y) - (lhs.y * rhs.x));
    }

    float length_point3d(const VectorTextPoint3D &point)
    {
      return std::sqrt(dot_point3d(point, point));
    }

    VectorTextPoint3D normalize_point3d(const VectorTextPoint3D &point)
    {
      const float length = length_point3d(point);
      if (length <= 1e-5f)
      {
        return make_point3d(0.0f, 0.0f, 0.0f);
      }

      return scale_point3d(point, 1.0f / length);
    }

    VectorTextPoint3D rotate_around_axis(
        const VectorTextPoint3D &point,
        const VectorTextPoint3D &axis,
        float angleRadians)
    {
      const VectorTextPoint3D normalizedAxis = normalize_point3d(axis);
      if (length_point3d(normalizedAxis) <= 1e-5f)
      {
        return point;
      }

      const float cosine = std::cos(angleRadians);
      const float sine = std::sin(angleRadians);
      return add_point3d(
          add_point3d(
              scale_point3d(point, cosine),
              scale_point3d(cross_point3d(normalizedAxis, point), sine)),
          scale_point3d(normalizedAxis, dot_point3d(normalizedAxis, point) * (1.0f - cosine)));
    }

    float measure_range_width(std::string_view text, std::size_t begin, std::size_t end, float characterHeight)
    {
      float width = 0.0f;
      for (std::size_t index = begin; index < end; ++index)
      {
        if (text[index] == '\r')
        {
          continue;
        }

        width += scaled_advance(text[index], characterHeight);
      }
      return width;
    }

    void append_glyph_strokes(
        std::vector<VectorTextSegment2D> &segments,
        char character,
        float xOffset,
        float yOffset,
        float characterHeight)
    {
      const GlyphDefinition glyph = glyph_for_character(character);
      for (std::size_t index = 0; index < glyph.strokeCount; ++index)
      {
        const auto &stroke = glyph.strokes[index];
        segments.push_back(VectorTextSegment2D{
            VectorTextPoint2D{xOffset + (stroke.x1 * characterHeight), yOffset + (stroke.y1 * characterHeight)},
            VectorTextPoint2D{xOffset + (stroke.x2 * characterHeight), yOffset + (stroke.y2 * characterHeight)},
        });
      }
    }

    VectorTextPoint3D add_scaled(
        const VectorTextPoint3D &origin,
        const VectorTextPoint3D &direction,
        float scale)
    {
      return VectorTextPoint3D{
          origin.x + (direction.x * scale),
          origin.y + (direction.y * scale),
          origin.z + (direction.z * scale),
      };
    }
  }

  VectorTextFrame3D make_vector_text_frame_from_euler(
      const VectorTextPoint3D &origin,
      float yawDegrees,
      float pitchDegrees,
      float rollDegrees,
      float anchorX,
      float anchorY)
  {
    const float yawRadians = degrees_to_radians(yawDegrees);
    const float pitchRadians = degrees_to_radians(pitchDegrees);
    const float rollRadians = degrees_to_radians(rollDegrees);
    const float cosPitch = std::cos(pitchRadians);

    VectorTextPoint3D forward = normalize_point3d(make_point3d(
        std::sin(yawRadians) * cosPitch,
        std::sin(pitchRadians),
        std::cos(yawRadians) * cosPitch));
    if (length_point3d(forward) <= 1e-5f)
    {
      forward = make_point3d(0.0f, 0.0f, 1.0f);
    }

    const VectorTextPoint3D worldUp = make_point3d(0.0f, 1.0f, 0.0f);
    VectorTextPoint3D right = normalize_point3d(cross_point3d(worldUp, forward));
    if (length_point3d(right) <= 1e-5f)
    {
      right = make_point3d(1.0f, 0.0f, 0.0f);
    }

    VectorTextPoint3D up = normalize_point3d(cross_point3d(forward, right));
    if (length_point3d(up) <= 1e-5f)
    {
      up = make_point3d(0.0f, 1.0f, 0.0f);
    }

    if (std::abs(rollRadians) > 1e-5f)
    {
      right = normalize_point3d(rotate_around_axis(right, forward, rollRadians));
      up = normalize_point3d(rotate_around_axis(up, forward, rollRadians));
    }

    return VectorTextFrame3D{
        origin,
        right,
        up,
        anchorX,
        anchorY,
    };
  }

  VectorTextLayout layout_vector_text(std::string_view text, const VectorTextStyle &style)
  {
    const float characterHeight = std::max(0.05f, style.characterHeight);
    const float wrapWidth = std::max(0.0f, style.wrapWidth);
    const float lineAdvance = characterHeight * std::max(0.8f, style.lineSpacing);

    VectorTextLayout layout;
    layout.lineCount = 1;

    auto begin_new_line = [&layout, &lineAdvance](float &x, float &y)
    {
      layout.width = std::max(layout.width, x);
      x = 0.0f;
      y += lineAdvance;
      ++layout.lineCount;
    };

    float x = 0.0f;
    float y = 0.0f;
    std::size_t index = 0;
    while (index < text.size())
    {
      if (text[index] == '\r')
      {
        ++index;
        continue;
      }

      if (text[index] == '\n')
      {
        begin_new_line(x, y);
        ++index;
        continue;
      }

      if (text[index] == ' ' || text[index] == '\t')
      {
        std::size_t tokenEnd = index;
        float whitespaceWidth = 0.0f;
        while (tokenEnd < text.size() && (text[tokenEnd] == ' ' || text[tokenEnd] == '\t'))
        {
          whitespaceWidth += scaled_advance(text[tokenEnd] == '\t' ? ' ' : text[tokenEnd], characterHeight) *
                             (text[tokenEnd] == '\t' ? 4.0f : 1.0f);
          ++tokenEnd;
        }

        if (x > 0.0f)
        {
          if (wrapWidth > 0.0f && (x + whitespaceWidth) > wrapWidth)
          {
            begin_new_line(x, y);
          }
          else
          {
            x += whitespaceWidth;
          }
        }

        index = tokenEnd;
        continue;
      }

      std::size_t tokenEnd = index;
      while (tokenEnd < text.size() &&
             text[tokenEnd] != '\r' &&
             text[tokenEnd] != '\n' &&
             text[tokenEnd] != ' ' &&
             text[tokenEnd] != '\t')
      {
        ++tokenEnd;
      }

      const float wordWidth = measure_range_width(text, index, tokenEnd, characterHeight);
      if (wrapWidth > 0.0f && x > 0.0f && (x + wordWidth) > wrapWidth)
      {
        begin_new_line(x, y);
      }

      if (wrapWidth > 0.0f && wordWidth > wrapWidth)
      {
        for (std::size_t glyphIndex = index; glyphIndex < tokenEnd; ++glyphIndex)
        {
          const float advance = scaled_advance(text[glyphIndex], characterHeight);
          if (x > 0.0f && (x + advance) > wrapWidth)
          {
            begin_new_line(x, y);
          }

          append_glyph_strokes(layout.segments, text[glyphIndex], x, y, characterHeight);
          x += advance;
        }
      }
      else
      {
        for (std::size_t glyphIndex = index; glyphIndex < tokenEnd; ++glyphIndex)
        {
          append_glyph_strokes(layout.segments, text[glyphIndex], x, y, characterHeight);
          x += scaled_advance(text[glyphIndex], characterHeight);
        }
      }

      index = tokenEnd;
    }

    layout.width = std::max(layout.width, x);
    layout.height = characterHeight + (static_cast<float>(layout.lineCount - 1) * lineAdvance);
    return layout;
  }

  VectorTextGeometry3D build_vector_text_geometry(
      std::string_view text,
      const VectorTextStyle &style,
      const VectorTextFrame3D &frame)
  {
    const VectorTextLayout layout = layout_vector_text(text, style);
    const float xOffset = -layout.width * std::clamp(frame.anchorX, 0.0f, 1.0f);
    const float yOffset = -layout.height * std::clamp(frame.anchorY, 0.0f, 1.0f);

    VectorTextGeometry3D geometry;
    geometry.width = layout.width;
    geometry.height = layout.height;
    geometry.lineCount = layout.lineCount;
    geometry.segments.reserve(layout.segments.size());

    for (const auto &segment : layout.segments)
    {
      VectorTextPoint3D start = frame.origin;
      start = add_scaled(start, frame.right, xOffset + segment.start.x);
      start = add_scaled(start, frame.up, -(yOffset + segment.start.y));

      VectorTextPoint3D end = frame.origin;
      end = add_scaled(end, frame.right, xOffset + segment.end.x);
      end = add_scaled(end, frame.up, -(yOffset + segment.end.y));

      geometry.segments.push_back(VectorTextSegment3D{start, end});
    }

    return geometry;
  }
}
