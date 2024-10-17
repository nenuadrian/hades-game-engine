#pragma once

namespace hades
{
  class PositionComponent3D
  {
  public:
    float x, y, z;

    PositionComponent3D(float x = 0.0f, float y = 0.0f, float z = 0.0f)
        : x(x), y(y), z(z) {}
  };
}
