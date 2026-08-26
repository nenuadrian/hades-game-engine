#include "model_loader.hpp"

#include <cstring>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    math::Mat4 toMat4(const aiMatrix4x4 &m)
    {
      // aiMatrix4x4 is row-major (m.a1..d4 = rows); math::Mat4 is
      // column-major m[col][row].
      math::Mat4 r;
      r.m[0][0] = m.a1; r.m[1][0] = m.a2; r.m[2][0] = m.a3; r.m[3][0] = m.a4;
      r.m[0][1] = m.b1; r.m[1][1] = m.b2; r.m[2][1] = m.b3; r.m[3][1] = m.b4;
      r.m[0][2] = m.c1; r.m[1][2] = m.c2; r.m[2][2] = m.c3; r.m[3][2] = m.c4;
      r.m[0][3] = m.d1; r.m[1][3] = m.d2; r.m[2][3] = m.d3; r.m[3][3] = m.d4;
      return r;
    }

    struct BoneKey
    {
      int nodeIndex;
      math::Mat4 offset;

      bool operator==(const BoneKey &o) const
      {
        return nodeIndex == o.nodeIndex &&
               std::memcmp(&offset.m[0][0], &o.offset.m[0][0], sizeof(offset.m)) == 0;
      }
    };

    struct BoneKeyHash
    {
      std::size_t operator()(const BoneKey &k) const
      {
        std::size_t h = std::hash<int>()(k.nodeIndex);
        const float *f = &k.offset.m[0][0];
        for (int i = 0; i < 16; ++i)
        {
          std::size_t bits = 0;
          std::memcpy(&bits, &f[i], sizeof(float));
          h ^= bits + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
      }
    };

    struct ImportContext
    {
      ModelAsset *asset = nullptr;
      std::unordered_map<std::string, int> nodeIndexByName;
      std::unordered_map<unsigned int, int> firstNodeForMesh;
      std::unordered_map<BoneKey, uint32_t, BoneKeyHash> boneIndexByKey;
      bool boneLimitWarned = false;
    };

    void collectNodes(const aiNode *node, int parentIndex, ImportContext &ctx)
    {
      const int index = static_cast<int>(ctx.asset->nodes.size());

      ModelNode n;
      n.name = node->mName.C_Str();
      n.parent = parentIndex;
      n.localTransform = toMat4(node->mTransformation);
      ctx.asset->nodes.push_back(std::move(n));

      // First name wins on duplicates; bones resolve to that node.
      ctx.nodeIndexByName.emplace(node->mName.C_Str(), index);

      for (unsigned int i = 0; i < node->mNumMeshes; ++i)
      {
        ctx.firstNodeForMesh.emplace(node->mMeshes[i], index);
      }

      for (unsigned int i = 0; i < node->mNumChildren; ++i)
      {
        collectNodes(node->mChildren[i], index, ctx);
      }
    }

    uint32_t paletteIndexFor(const BoneKey &key, ImportContext &ctx)
    {
      auto it = ctx.boneIndexByKey.find(key);
      if (it != ctx.boneIndexByKey.end())
      {
        return it->second;
      }

      if (ctx.asset->bones.size() >= kMaxModelBones)
      {
        if (!ctx.boneLimitWarned)
        {
          Log::warn_tagged(
              "model_loader",
              "model exceeds the %u-bone limit; extra bones are clamped",
              kMaxModelBones);
          ctx.boneLimitWarned = true;
        }
        return 0;
      }

      const uint32_t index = static_cast<uint32_t>(ctx.asset->bones.size());
      ctx.asset->bones.push_back(ModelBone{key.nodeIndex, key.offset});
      ctx.boneIndexByKey.emplace(key, index);
      return index;
    }

    void addVertexWeight(ModelVertex &v, uint32_t boneIndex, float weight)
    {
      // Fill a free slot, or replace the smallest weight if all four are used.
      int slot = -1;
      for (int i = 0; i < 4; ++i)
      {
        if (v.boneWeights[i] <= 0.0f)
        {
          slot = i;
          break;
        }
      }
      if (slot < 0)
      {
        slot = 0;
        for (int i = 1; i < 4; ++i)
        {
          if (v.boneWeights[i] < v.boneWeights[slot])
          {
            slot = i;
          }
        }
        if (v.boneWeights[slot] >= weight)
        {
          return;
        }
      }
      v.boneIndices[slot] = boneIndex;
      v.boneWeights[slot] = weight;
    }

    Material importMaterial(const aiScene *scene, unsigned int materialIndex)
    {
      Material material;
      if (materialIndex >= scene->mNumMaterials)
      {
        return material;
      }

      const aiMaterial *src = scene->mMaterials[materialIndex];

      aiColor4D color;
      if (src->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
          src->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
      {
        material.baseColorR = color.r;
        material.baseColorG = color.g;
        material.baseColorB = color.b;
      }

      float scalar = 0.0f;
      if (src->Get(AI_MATKEY_METALLIC_FACTOR, scalar) == AI_SUCCESS)
      {
        material.metallic = scalar;
      }
      if (src->Get(AI_MATKEY_ROUGHNESS_FACTOR, scalar) == AI_SUCCESS)
      {
        material.roughness = scalar;
      }
      if (src->Get(AI_MATKEY_OPACITY, scalar) == AI_SUCCESS && scalar > 0.0f)
      {
        material.opacity = scalar;
      }

      return material;
    }

    void importMesh(const aiScene *scene, unsigned int meshIndex, ImportContext &ctx)
    {
      const aiMesh *mesh = scene->mMeshes[meshIndex];
      if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 || mesh->mNumVertices == 0)
      {
        return;
      }

      auto nodeIt = ctx.firstNodeForMesh.find(meshIndex);
      const int meshNode = nodeIt != ctx.firstNodeForMesh.end() ? nodeIt->second : 0;
      const BoneKey rigidKey{meshNode, math::Mat4::identity()};

      ModelMeshData data;
      data.material = importMaterial(scene, mesh->mMaterialIndex);
      // A skinned mesh reaches model space through its bone offset matrices,
      // which already carry the mesh's own placement; only a rigid mesh is
      // placed by the node that references it.
      data.nodeIndex = mesh->mNumBones > 0 ? -1 : meshNode;
      data.vertices.resize(mesh->mNumVertices);

      for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
      {
        auto &v = data.vertices[i];
        v = ModelVertex{};
        v.px = mesh->mVertices[i].x;
        v.py = mesh->mVertices[i].y;
        v.pz = mesh->mVertices[i].z;
        if (mesh->mNormals != nullptr)
        {
          v.nx = mesh->mNormals[i].x;
          v.ny = mesh->mNormals[i].y;
          v.nz = mesh->mNormals[i].z;
        }
        if (mesh->mTextureCoords[0] != nullptr)
        {
          v.u = mesh->mTextureCoords[0][i].x;
          v.v = mesh->mTextureCoords[0][i].y;
        }
      }

      if (mesh->mNumBones > 0)
      {
        ctx.asset->hasSkeleton = true;
        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
          const aiBone *bone = mesh->mBones[b];
          auto boneNodeIt = ctx.nodeIndexByName.find(bone->mName.C_Str());
          if (boneNodeIt == ctx.nodeIndexByName.end())
          {
            Log::warn_tagged(
                "model_loader", "bone '%s' has no matching node; skipping", bone->mName.C_Str());
            continue;
          }

          const uint32_t palette =
              paletteIndexFor(BoneKey{boneNodeIt->second, toMat4(bone->mOffsetMatrix)}, ctx);
          for (unsigned int w = 0; w < bone->mNumWeights; ++w)
          {
            const auto &weight = bone->mWeights[w];
            if (weight.mVertexId < data.vertices.size() && weight.mWeight > 0.0f)
            {
              addVertexWeight(data.vertices[weight.mVertexId], palette, weight.mWeight);
            }
          }
        }

        // Normalize weights; vertices no bone touched fall back to the rigid
        // pseudo-bone of the mesh's node so they still follow the model.
        for (auto &v : data.vertices)
        {
          const float total = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
          if (total > 1e-6f)
          {
            for (float &w : v.boneWeights)
            {
              w /= total;
            }
          }
          else
          {
            v.boneIndices[0] = paletteIndexFor(rigidKey, ctx);
            v.boneWeights[0] = 1.0f;
          }
        }
      }
      else
      {
        const uint32_t palette = paletteIndexFor(rigidKey, ctx);
        for (auto &v : data.vertices)
        {
          v.boneIndices[0] = palette;
          v.boneWeights[0] = 1.0f;
        }
      }

      data.indices.reserve(mesh->mNumFaces * 3);
      for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
      {
        const aiFace &face = mesh->mFaces[f];
        if (face.mNumIndices != 3)
        {
          continue;
        }
        data.indices.push_back(face.mIndices[0]);
        data.indices.push_back(face.mIndices[1]);
        data.indices.push_back(face.mIndices[2]);
      }

      if (!data.indices.empty())
      {
        ctx.asset->meshes.push_back(std::move(data));
      }
    }

    void importAnimations(const aiScene *scene, ImportContext &ctx)
    {
      for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
      {
        const aiAnimation *anim = scene->mAnimations[a];
        const double ticksPerSecond = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

        AnimationClip clip;
        clip.name = anim->mName.length > 0 ? anim->mName.C_Str() : ("Clip " + std::to_string(a + 1));
        clip.duration = static_cast<float>(anim->mDuration / ticksPerSecond);

        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
          const aiNodeAnim *src = anim->mChannels[c];
          auto nodeIt = ctx.nodeIndexByName.find(src->mNodeName.C_Str());
          if (nodeIt == ctx.nodeIndexByName.end())
          {
            continue;
          }

          AnimationChannel channel;
          channel.nodeIndex = nodeIt->second;

          channel.positions.reserve(src->mNumPositionKeys);
          for (unsigned int k = 0; k < src->mNumPositionKeys; ++k)
          {
            const auto &key = src->mPositionKeys[k];
            channel.positions.push_back(
                {static_cast<float>(key.mTime / ticksPerSecond),
                 {key.mValue.x, key.mValue.y, key.mValue.z}});
          }

          channel.rotations.reserve(src->mNumRotationKeys);
          for (unsigned int k = 0; k < src->mNumRotationKeys; ++k)
          {
            const auto &key = src->mRotationKeys[k];
            channel.rotations.push_back(
                {static_cast<float>(key.mTime / ticksPerSecond),
                 {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
          }

          channel.scales.reserve(src->mNumScalingKeys);
          for (unsigned int k = 0; k < src->mNumScalingKeys; ++k)
          {
            const auto &key = src->mScalingKeys[k];
            channel.scales.push_back(
                {static_cast<float>(key.mTime / ticksPerSecond),
                 {key.mValue.x, key.mValue.y, key.mValue.z}});
          }

          clip.channels.push_back(std::move(channel));
        }

        ctx.asset->clips.push_back(std::move(clip));
      }
    }
  }

  bool load_model_asset(const std::filesystem::path &file, ModelAsset &out, std::string *error)
  {
    Assimp::Importer importer;
    // Collapsing FBX pivot nodes keeps the hierarchy small and bone names
    // aligned with animation channel names.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_SortByPType |
        aiProcess_GlobalScale;

    const aiScene *scene = importer.ReadFile(file.string(), flags);
    if (scene == nullptr || scene->mRootNode == nullptr)
    {
      if (error != nullptr)
      {
        *error = importer.GetErrorString();
        if (error->empty())
        {
          *error = "assimp returned no scene for '" + file.string() + "'";
        }
      }
      return false;
    }

    out = ModelAsset{};

    ImportContext ctx;
    ctx.asset = &out;
    collectNodes(scene->mRootNode, -1, ctx);
    out.globalInverseTransform = toMat4(scene->mRootNode->mTransformation).inverse();

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
      importMesh(scene, m, ctx);
    }

    if (out.meshes.empty())
    {
      if (error != nullptr)
      {
        *error = "no triangle meshes found in '" + file.string() + "'";
      }
      return false;
    }

    importAnimations(scene, ctx);
    out.finalize();
    return true;
  }
}
