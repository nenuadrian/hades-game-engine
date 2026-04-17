#ifndef HADES_ENGINE_RENDERING_PRIMITIVE_MESH_HPP
#define HADES_ENGINE_RENDERING_PRIMITIVE_MESH_HPP

#include <cstdint>
#include <vector>

#include "../components/primitive_component.hpp"

namespace hades
{
  struct MeshVertex
  {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
  };

  struct MeshCpuData
  {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
  };

  // Unit-sized primitives matching the scale applied in scene_renderer:
  // cube: axis-aligned, ±0.5 half extents
  // plane: XZ quad, ±0.5 half extents, normal +Y
  // sphere: radius 0.5
  MeshCpuData buildPrimitiveMesh(PrimitiveType type);
}

#endif
