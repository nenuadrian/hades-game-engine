#ifndef HADES_ENGINE_ASSETS_IMPORTED_MODEL_HPP
#define HADES_ENGINE_ASSETS_IMPORTED_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hades
{
  struct ImportedVertex
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct ImportedTriangle
  {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
  };

  struct ImportedMesh
  {
    std::string name;
    std::size_t vertexCount = 0;
    std::size_t faceCount = 0;
    std::size_t materialIndex = 0;
    std::vector<ImportedVertex> vertices;
    std::vector<ImportedTriangle> triangles;
  };

  struct ImportedMaterial
  {
    std::string name;
  };

  struct ImportedModel
  {
    std::string sourcePath;
    std::string formatHint;
    std::vector<ImportedMesh> meshes;
    std::vector<ImportedMaterial> materials;
    std::size_t totalVertexCount = 0;
    std::size_t totalFaceCount = 0;
    bool hasBounds = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
  };
}

#endif
