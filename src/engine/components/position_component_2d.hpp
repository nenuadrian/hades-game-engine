#ifndef POSITION_COMPONENT_H
#define POSITION_COMPONENT_H

namespace hades
{
  class PositionComponent2D
  {
  public:
    float x, y;

    PositionComponent2D(float x = 0.0f, float y = 0.0f)
        : x(x), y(y) {}
  };
}

#endif
