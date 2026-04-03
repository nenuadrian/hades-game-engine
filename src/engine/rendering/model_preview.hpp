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

  template <typename ToCameraSpace, typename ProjectToScreen>
  std::vector<ProjectedTriangle> project_model_triangles(
      const ImportedModel &model,
      const PositionComponent3D &position,
      ToCameraSpace toCameraSpace,
      ProjectToScreen projectToScreen)
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

      std::vector<Vec3> cameraVertices;
      cameraVertices.reserve(mesh.vertices.size());
      for (const auto &vertex : mesh.vertices)
      {
        cameraVertices.push_back(toCameraSpace(add_vec3(translation, make_vec3(vertex.x, vertex.y, vertex.z))));
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

        if (dot_vec3(normal, center) > 0.0f)
        {
          std::swap(indices[1], indices[2]);
          cameraPoints[1] = cameraVertices[indices[1]];
          cameraPoints[2] = cameraVertices[indices[2]];
          normal = scale_vec3(normal, -1.0f);
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

        const Vec3 normalizedNormal = normalize_vec3(normal);
        projectedTriangle.averageDepth = depthSum / 3.0f;
        projectedTriangle.shade = std::clamp(0.25f + ((-normalizedNormal.z) * 0.75f), 0.18f, 1.0f);
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
