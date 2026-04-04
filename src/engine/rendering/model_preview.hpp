#ifndef HADES_ENGINE_RENDERING_MODEL_PREVIEW_HPP
#define HADES_ENGINE_RENDERING_MODEL_PREVIEW_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../assets/imported_model.hpp"
#include "../components/position_component_3d.hpp"

namespace hades::preview
{
  struct Vec2
  {
    float x = 0.0f;
    float y = 0.0f;
  };

  struct Vec3
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct ProjectedTriangle
  {
    std::array<Vec2, 3> points{};
    float averageDepth = 0.0f;
    float shade = 0.65f;
    float shadeR = 0.65f;
    float shadeG = 0.65f;
    float shadeB = 0.65f;
  };

  struct LightData
  {
    struct Light
    {
      int type = 0;
      float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
      float dirX = 0.0f, dirY = -1.0f, dirZ = 0.0f;
      float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
      float intensity = 1.0f;
      float range = 10.0f;
      float innerConeAngle = 25.0f, outerConeAngle = 35.0f;
      float ambientContribution = 0.05f;
    };

    std::vector<Light> lights;
    float globalAmbient = 0.15f;
  };

  inline Vec3 make_vec3(float x, float y, float z)
  {
    return Vec3{x, y, z};
  }

  inline Vec3 add_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return make_vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
  }

  inline Vec3 subtract_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return make_vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
  }

  inline Vec3 scale_vec3(const Vec3 &value, float scalar)
  {
    return make_vec3(value.x * scalar, value.y * scalar, value.z * scalar);
  }

  inline float dot_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
  }

  inline Vec3 cross_vec3(const Vec3 &lhs, const Vec3 &rhs)
  {
    return make_vec3(
        (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.x * rhs.y) - (lhs.y * rhs.x));
  }

  inline float length_vec3(const Vec3 &value)
  {
    return std::sqrt(dot_vec3(value, value));
  }

  inline Vec3 normalize_vec3(const Vec3 &value)
  {
    const float length = length_vec3(value);
    if (length <= 1e-5f)
    {
      return make_vec3(0.0f, 0.0f, 0.0f);
    }

    return scale_vec3(value, 1.0f / length);
  }

  inline bool has_renderable_geometry(const ImportedModel &model)
  {
    for (const auto &mesh : model.meshes)
    {
      if (!mesh.vertices.empty() && !mesh.triangles.empty())
      {
        return true;
      }
    }

    return false;
  }

  inline std::uint8_t scale_color_channel(std::uint8_t channel, float shade)
  {
    const float scaled = std::clamp(static_cast<float>(channel) * shade, 0.0f, 255.0f);
    return static_cast<std::uint8_t>(std::lround(scaled));
  }

  inline void compute_lit_shade(
      const Vec3 &triangleCenter,
      const Vec3 &triangleNormal,
      const LightData &lightData,
      float &outR, float &outG, float &outB)
  {
    float ambientR = lightData.globalAmbient;
    float ambientG = lightData.globalAmbient;
    float ambientB = lightData.globalAmbient;
    float diffuseR = 0.0f;
    float diffuseG = 0.0f;
    float diffuseB = 0.0f;

    for (const auto &light : lightData.lights)
    {
      float contribution = 0.0f;
      float attenuation = 1.0f;

      if (light.type == 0) // Directional
      {
        Vec3 lightDir = normalize_vec3(make_vec3(-light.dirX, -light.dirY, -light.dirZ));
        contribution = std::max(0.0f, dot_vec3(triangleNormal, lightDir));
      }
      else if (light.type == 1) // Point
      {
        Vec3 lightPos = make_vec3(light.posX, light.posY, light.posZ);
        Vec3 toLight = subtract_vec3(lightPos, triangleCenter);
        float dist = length_vec3(toLight);
        if (dist > light.range || dist < 1e-5f)
        {
          ambientR += light.ambientContribution * light.colorR;
          ambientG += light.ambientContribution * light.colorG;
          ambientB += light.ambientContribution * light.colorB;
          continue;
        }
        Vec3 lightDir = scale_vec3(toLight, 1.0f / dist);
        contribution = std::max(0.0f, dot_vec3(triangleNormal, lightDir));
        float ratio = dist / light.range;
        attenuation = 1.0f / (1.0f + ratio * ratio);
      }
      else if (light.type == 2) // Spot
      {
        Vec3 lightPos = make_vec3(light.posX, light.posY, light.posZ);
        Vec3 toLight = subtract_vec3(lightPos, triangleCenter);
        float dist = length_vec3(toLight);
        if (dist > light.range || dist < 1e-5f)
        {
          ambientR += light.ambientContribution * light.colorR;
          ambientG += light.ambientContribution * light.colorG;
          ambientB += light.ambientContribution * light.colorB;
          continue;
        }
        Vec3 lightDir = scale_vec3(toLight, 1.0f / dist);
        contribution = std::max(0.0f, dot_vec3(triangleNormal, lightDir));
        float ratio = dist / light.range;
        attenuation = 1.0f / (1.0f + ratio * ratio);

        Vec3 spotDir = normalize_vec3(make_vec3(light.dirX, light.dirY, light.dirZ));
        Vec3 negLightDir = scale_vec3(lightDir, -1.0f);
        float cosAngle = dot_vec3(negLightDir, spotDir);
        float cosInner = std::cos(light.innerConeAngle * 3.14159265f / 180.0f);
        float cosOuter = std::cos(light.outerConeAngle * 3.14159265f / 180.0f);
        float spotFactor = std::clamp((cosAngle - cosOuter) / (cosInner - cosOuter + 1e-5f), 0.0f, 1.0f);
        attenuation *= spotFactor;
      }

      float scaled = contribution * light.intensity * attenuation;
      diffuseR += scaled * light.colorR;
      diffuseG += scaled * light.colorG;
      diffuseB += scaled * light.colorB;

      ambientR += light.ambientContribution * light.colorR;
      ambientG += light.ambientContribution * light.colorG;
      ambientB += light.ambientContribution * light.colorB;
    }

    outR = std::clamp(ambientR + diffuseR, 0.0f, 1.0f);
    outG = std::clamp(ambientG + diffuseG, 0.0f, 1.0f);
    outB = std::clamp(ambientB + diffuseB, 0.0f, 1.0f);
  }

  template <typename ToCameraSpace, typename ProjectToScreen>
  std::vector<ProjectedTriangle> project_model_triangles(
      const ImportedModel &model,
      const PositionComponent3D &position,
      ToCameraSpace toCameraSpace,
      ProjectToScreen projectToScreen,
      const LightData *lightData = nullptr)
  {
    std::vector<ProjectedTriangle> projectedTriangles;
    projectedTriangles.reserve(model.totalFaceCount);

    const Vec3 translation = make_vec3(position.x, position.y, position.z);
    for (const auto &mesh : model.meshes)
    {
      if (mesh.vertices.empty() || mesh.triangles.empty())
      {
        continue;
      }

      std::vector<Vec3> worldVertices;
      worldVertices.reserve(mesh.vertices.size());
      for (const auto &vertex : mesh.vertices)
      {
        worldVertices.push_back(add_vec3(translation, make_vec3(vertex.x, vertex.y, vertex.z)));
      }

      std::vector<Vec3> cameraVertices;
      cameraVertices.reserve(mesh.vertices.size());
      for (const auto &worldVertex : worldVertices)
      {
        cameraVertices.push_back(toCameraSpace(worldVertex));
      }

      for (const auto &triangle : mesh.triangles)
      {
        std::array<std::size_t, 3> indices = {
            static_cast<std::size_t>(triangle.a),
            static_cast<std::size_t>(triangle.b),
            static_cast<std::size_t>(triangle.c)};
        if (indices[0] >= cameraVertices.size() ||
            indices[1] >= cameraVertices.size() ||
            indices[2] >= cameraVertices.size())
        {
          continue;
        }

        std::array<Vec3, 3> cameraPoints = {
            cameraVertices[indices[0]],
            cameraVertices[indices[1]],
            cameraVertices[indices[2]]};
        Vec3 normal = cross_vec3(
            subtract_vec3(cameraPoints[1], cameraPoints[0]),
            subtract_vec3(cameraPoints[2], cameraPoints[0]));
        if (length_vec3(normal) <= 1e-5f)
        {
          continue;
        }

        const Vec3 center = scale_vec3(
            add_vec3(
                add_vec3(cameraPoints[0], cameraPoints[1]),
                cameraPoints[2]),
            1.0f / 3.0f);

        bool flipped = false;
        if (dot_vec3(normal, center) > 0.0f)
        {
          std::swap(indices[1], indices[2]);
          cameraPoints[1] = cameraVertices[indices[1]];
          cameraPoints[2] = cameraVertices[indices[2]];
          normal = scale_vec3(normal, -1.0f);
          flipped = true;
        }

        ProjectedTriangle projectedTriangle;
        float depthSum = 0.0f;
        bool validTriangle = true;
        for (std::size_t pointIndex = 0; pointIndex < cameraPoints.size(); ++pointIndex)
        {
          Vec2 screenPoint;
          if (!projectToScreen(cameraPoints[pointIndex], screenPoint))
          {
            validTriangle = false;
            break;
          }

          projectedTriangle.points[pointIndex] = screenPoint;
          depthSum += cameraPoints[pointIndex].z;
        }

        if (!validTriangle)
        {
          continue;
        }

        projectedTriangle.averageDepth = depthSum / 3.0f;

        if (lightData != nullptr && !lightData->lights.empty())
        {
          Vec3 worldNormal = cross_vec3(
              subtract_vec3(worldVertices[indices[1]], worldVertices[indices[0]]),
              subtract_vec3(worldVertices[indices[2]], worldVertices[indices[0]]));
          if (flipped)
          {
            worldNormal = scale_vec3(worldNormal, -1.0f);
          }
          worldNormal = normalize_vec3(worldNormal);

          Vec3 worldCenter = scale_vec3(
              add_vec3(
                  add_vec3(worldVertices[indices[0]], worldVertices[indices[1]]),
                  worldVertices[indices[2]]),
              1.0f / 3.0f);

          compute_lit_shade(worldCenter, worldNormal, *lightData,
                            projectedTriangle.shadeR, projectedTriangle.shadeG, projectedTriangle.shadeB);
          projectedTriangle.shade = (projectedTriangle.shadeR + projectedTriangle.shadeG + projectedTriangle.shadeB) / 3.0f;
        }
        else
        {
          const Vec3 normalizedNormal = normalize_vec3(normal);
          projectedTriangle.shade = std::clamp(0.25f + ((-normalizedNormal.z) * 0.75f), 0.18f, 1.0f);
          projectedTriangle.shadeR = projectedTriangle.shade;
          projectedTriangle.shadeG = projectedTriangle.shade;
          projectedTriangle.shadeB = projectedTriangle.shade;
        }

        projectedTriangles.push_back(projectedTriangle);
      }
    }

    std::sort(
        projectedTriangles.begin(),
        projectedTriangles.end(),
        [](const ProjectedTriangle &lhs, const ProjectedTriangle &rhs)
        {
          return lhs.averageDepth > rhs.averageDepth;
        });

    return projectedTriangles;
  }
}

#endif
