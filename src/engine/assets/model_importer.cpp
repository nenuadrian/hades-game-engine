#include "model_importer.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>
#include <string>
#include <utility>

namespace hades
{
  namespace
  {
    std::string mesh_name_or_default(const aiMesh &mesh, std::size_t index)
    {
      if (mesh.mName.length > 0)
      {
        return mesh.mName.C_Str();
      }

      return "Mesh " + std::to_string(index);
    }

    std::string material_name_or_default(const aiMaterial &material, std::size_t index)
    {
      aiString materialName;
      if (material.Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS && materialName.length > 0)
      {
        return materialName.C_Str();
      }

      return "Material " + std::to_string(index);
    }

    std::string extension_without_dot(const std::filesystem::path &path)
    {
      std::string extension = path.extension().string();
      if (!extension.empty() && extension.front() == '.')
      {
        extension.erase(0, 1);
      }
      return extension;
    }
  }

  std::optional<ImportedModel> ModelImporter::importFromFile(
      const std::filesystem::path &sourcePath,
      std::string *errorMessage)
  {
    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }

    if (sourcePath.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Model path is empty.";
      }
      return std::nullopt;
    }

    if (!std::filesystem::exists(sourcePath))
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Model file does not exist: " + sourcePath.string();
      }
      return std::nullopt;
    }

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        sourcePath.string(),
        aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality);

    if (scene == nullptr || scene->mRootNode == nullptr)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = importer.GetErrorString();
      }
      return std::nullopt;
    }

    ImportedModel model;
    model.sourcePath = std::filesystem::absolute(sourcePath).lexically_normal().string();
    model.formatHint = extension_without_dot(sourcePath);

    model.meshes.reserve(scene->mNumMeshes);
    for (std::size_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
      const aiMesh *mesh = scene->mMeshes[meshIndex];
      if (mesh == nullptr)
      {
        continue;
      }

      ImportedMesh importedMesh;
      importedMesh.name = mesh_name_or_default(*mesh, meshIndex);
      importedMesh.vertexCount = mesh->mNumVertices;
      importedMesh.faceCount = mesh->mNumFaces;
      importedMesh.materialIndex = mesh->mMaterialIndex;

      model.totalVertexCount += importedMesh.vertexCount;
      model.totalFaceCount += importedMesh.faceCount;
      model.meshes.push_back(std::move(importedMesh));
    }

    model.materials.reserve(scene->mNumMaterials);
    for (std::size_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
    {
      const aiMaterial *material = scene->mMaterials[materialIndex];
      if (material == nullptr)
      {
        continue;
      }

      ImportedMaterial importedMaterial;
      importedMaterial.name = material_name_or_default(*material, materialIndex);
      model.materials.push_back(std::move(importedMaterial));
    }

    return model;
  }
}
