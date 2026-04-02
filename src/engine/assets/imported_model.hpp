#ifndef HADES_ENGINE_ASSETS_IMPORTED_MODEL_HPP
#define HADES_ENGINE_ASSETS_IMPORTED_MODEL_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace hades
{
  struct ImportedMesh
  {
    std::string name;
    std::size_t vertexCount = 0;
    std::size_t faceCount = 0;
    std::size_t materialIndex = 0;
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
