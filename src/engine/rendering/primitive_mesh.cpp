#include "primitive_mesh.hpp"

#include <cmath>

namespace hades
{
  namespace
  {
    constexpr float kHalf = 0.5f;
    constexpr float kPi = 3.14159265358979323846f;

    MeshCpuData buildCube()
    {
      // 6 faces × 4 verts — each face has its own normals so lighting is flat-per-face.
      MeshCpuData m;
      m.vertices.reserve(24);
      m.indices.reserve(36);

      struct Face { float nx, ny, nz; float ux, uy, uz; float vx, vy, vz; };
      const Face faces[6] = {
          { 0,  0,  1,  1,  0,  0,  0,  1,  0}, // +Z
          { 0,  0, -1, -1,  0,  0,  0,  1,  0}, // -Z
          { 1,  0,  0,  0,  0, -1,  0,  1,  0}, // +X
          {-1,  0,  0,  0,  0,  1,  0,  1,  0}, // -X
          { 0,  1,  0,  1,  0,  0,  0,  0, -1}, // +Y
          { 0, -1,  0,  1,  0,  0,  0,  0,  1}, // -Y
      };

      for (int f = 0; f < 6; ++f)
      {
        const Face &face = faces[f];
        float cx = face.nx * kHalf;
        float cy = face.ny * kHalf;
        float cz = face.nz * kHalf;
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const float sx[4] = {-1, 1, 1, -1};
        const float sy[4] = {-1, -1, 1, 1};
        uint32_t base = static_cast<uint32_t>(m.vertices.size());
        for (int i = 0; i < 4; ++i)
        {
          MeshVertex v{};
          v.px = cx + (face.ux * sx[i] + face.vx * sy[i]) * kHalf;
          v.py = cy + (face.uy * sx[i] + face.vy * sy[i]) * kHalf;
          v.pz = cz + (face.uz * sx[i] + face.vz * sy[i]) * kHalf;
          v.nx = face.nx;
          v.ny = face.ny;
          v.nz = face.nz;
          v.u = uv[i][0];
          v.v = uv[i][1];
          m.vertices.push_back(v);
        }
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
      }
      return m;
    }

    MeshCpuData buildPlane()
    {
      MeshCpuData m;
      m.vertices = {
          {-kHalf, 0.0f, -kHalf, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
          { kHalf, 0.0f, -kHalf, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
          { kHalf, 0.0f,  kHalf, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
          {-kHalf, 0.0f,  kHalf, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
      };
      m.indices = {0, 2, 1, 0, 3, 2};
      return m;
    }

    MeshCpuData buildSphere()
    {
      constexpr int kRings = 24;
      constexpr int kSectors = 32;

      MeshCpuData m;
      m.vertices.reserve((kRings + 1) * (kSectors + 1));
      m.indices.reserve(kRings * kSectors * 6);

      for (int r = 0; r <= kRings; ++r)
      {
        float phi = kPi * (float(r) / float(kRings));
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        for (int s = 0; s <= kSectors; ++s)
        {
          float theta = 2.0f * kPi * (float(s) / float(kSectors));
          float sinTheta = std::sin(theta);
          float cosTheta = std::cos(theta);

          float x = sinPhi * cosTheta;
          float y = cosPhi;
          float z = sinPhi * sinTheta;

          MeshVertex v{};
          v.px = x * kHalf;
          v.py = y * kHalf;
          v.pz = z * kHalf;
          v.nx = x;
          v.ny = y;
          v.nz = z;
          v.u = float(s) / float(kSectors);
          v.v = float(r) / float(kRings);
          m.vertices.push_back(v);
        }
      }

      for (int r = 0; r < kRings; ++r)
      {
        for (int s = 0; s < kSectors; ++s)
        {
          uint32_t a = r * (kSectors + 1) + s;
          uint32_t b = a + 1;
          uint32_t c = a + (kSectors + 1);
          uint32_t d = c + 1;
          m.indices.push_back(a);
          m.indices.push_back(c);
          m.indices.push_back(b);
          m.indices.push_back(b);
          m.indices.push_back(c);
          m.indices.push_back(d);
        }
      }
      return m;
    }
  }

  MeshCpuData buildPrimitiveMesh(PrimitiveType type)
  {
    switch (type)
    {
      case PrimitiveType::Cube:   return buildCube();
      case PrimitiveType::Plane:  return buildPlane();
      case PrimitiveType::Sphere: return buildSphere();
    }
    return buildCube();
  }
}
