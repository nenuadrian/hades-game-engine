#ifndef HADES_ENGINE_RENDERING_MATH3D_HPP
#define HADES_ENGINE_RENDERING_MATH3D_HPP

#include <array>
#include <cmath>
#include <cstring>

namespace hades::math
{
  // -------------------------------------------------------------------------
  // Vec3
  // -------------------------------------------------------------------------
  struct Vec3
  {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3 &rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vec3 operator-(const Vec3 &rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    float dot(const Vec3 &rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }

    Vec3 cross(const Vec3 &rhs) const
    {
      return {y * rhs.z - z * rhs.y,
              z * rhs.x - x * rhs.z,
              x * rhs.y - y * rhs.x};
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }

    Vec3 normalized() const
    {
      float len = length();
      if (len < 1e-6f)
        return {0.0f, 0.0f, 0.0f};
      float inv = 1.0f / len;
      return {x * inv, y * inv, z * inv};
    }
  };

  inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }

  // -------------------------------------------------------------------------
  // Vec4
  // -------------------------------------------------------------------------
  struct Vec4
  {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3 &v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    Vec3 xyz() const { return {x, y, z}; }
  };

  // -------------------------------------------------------------------------
  // Mat4 — column-major 4x4 matrix
  // -------------------------------------------------------------------------
  struct Mat4
  {
    // Stored column-major: m[col][row]
    float m[4][4]{};

    Mat4() { std::memset(m, 0, sizeof(m)); }

    static Mat4 identity()
    {
      Mat4 r;
      r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
      return r;
    }

    static Mat4 translate(const Vec3 &t)
    {
      Mat4 r = identity();
      r.m[3][0] = t.x;
      r.m[3][1] = t.y;
      r.m[3][2] = t.z;
      return r;
    }

    static Mat4 scaleMatrix(const Vec3 &s)
    {
      Mat4 r;
      r.m[0][0] = s.x;
      r.m[1][1] = s.y;
      r.m[2][2] = s.z;
      r.m[3][3] = 1.0f;
      return r;
    }

    /// Perspective projection matrix (right-handed, depth [0, 1]).
    static Mat4 perspective(float fovYDegrees, float aspect, float nearClip, float farClip)
    {
      float fovRad = fovYDegrees * (3.14159265358979f / 180.0f);
      float tanHalfFov = std::tan(fovRad * 0.5f);
      Mat4 r;
      r.m[0][0] = 1.0f / (aspect * tanHalfFov);
      r.m[1][1] = 1.0f / tanHalfFov;
      r.m[2][2] = farClip / (farClip - nearClip);
      r.m[2][3] = 1.0f;
      r.m[3][2] = -(nearClip * farClip) / (farClip - nearClip);
      return r;
    }

    /// View matrix looking from eye towards target with given up vector.
    static Mat4 lookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &worldUp)
    {
      Vec3 forward = (target - eye).normalized();
      Vec3 right = worldUp.cross(forward).normalized();
      Vec3 up = forward.cross(right);

      Mat4 r = identity();
      r.m[0][0] = right.x;
      r.m[1][0] = right.y;
      r.m[2][0] = right.z;
      r.m[0][1] = up.x;
      r.m[1][1] = up.y;
      r.m[2][1] = up.z;
      r.m[0][2] = forward.x;
      r.m[1][2] = forward.y;
      r.m[2][2] = forward.z;
      r.m[3][0] = -right.dot(eye);
      r.m[3][1] = -up.dot(eye);
      r.m[3][2] = -forward.dot(eye);
      return r;
    }

    Mat4 operator*(const Mat4 &rhs) const
    {
      Mat4 r;
      for (int c = 0; c < 4; ++c)
      {
        for (int row = 0; row < 4; ++row)
        {
          r.m[c][row] = m[0][row] * rhs.m[c][0] +
                        m[1][row] * rhs.m[c][1] +
                        m[2][row] * rhs.m[c][2] +
                        m[3][row] * rhs.m[c][3];
        }
      }
      return r;
    }

    Vec4 operator*(const Vec4 &v) const
    {
      return {
          m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
          m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
          m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
          m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
    }

    /// Transform a point (w=1) and return xyz.
    Vec3 transformPoint(const Vec3 &p) const
    {
      Vec4 r = *this * Vec4(p, 1.0f);
      return r.xyz();
    }

    /// Transform a direction (w=0) and return xyz.
    Vec3 transformDirection(const Vec3 &d) const
    {
      Vec4 r = *this * Vec4(d, 0.0f);
      return r.xyz();
    }
  };

  // -------------------------------------------------------------------------
  // Quat — quaternion rotation
  // -------------------------------------------------------------------------
  struct Quat
  {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    Quat() = default;
    Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    /// Rotate a vector by this quaternion: q * v * q^-1 (simplified).
    Vec3 rotate(const Vec3 &v) const
    {
      float tx = 2.0f * (y * v.z - z * v.y);
      float ty = 2.0f * (z * v.x - x * v.z);
      float tz = 2.0f * (x * v.y - y * v.x);
      return {v.x + w * tx + (y * tz - z * ty),
              v.y + w * ty + (z * tx - x * tz),
              v.z + w * tz + (x * ty - y * tx)};
    }

    Mat4 toMat4() const
    {
      float xx = x * x, yy = y * y, zz = z * z;
      float xy = x * y, xz = x * z, yz = y * z;
      float wx = w * x, wy = w * y, wz = w * z;

      Mat4 r = Mat4::identity();
      r.m[0][0] = 1.0f - 2.0f * (yy + zz);
      r.m[0][1] = 2.0f * (xy + wz);
      r.m[0][2] = 2.0f * (xz - wy);
      r.m[1][0] = 2.0f * (xy - wz);
      r.m[1][1] = 1.0f - 2.0f * (xx + zz);
      r.m[1][2] = 2.0f * (yz + wx);
      r.m[2][0] = 2.0f * (xz + wy);
      r.m[2][1] = 2.0f * (yz - wx);
      r.m[2][2] = 1.0f - 2.0f * (xx + yy);
      return r;
    }
  };

  // -------------------------------------------------------------------------
  // Frustum — 6-plane frustum for culling
  // -------------------------------------------------------------------------
  struct Plane
  {
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f; // ax + by + cz + d = 0

    float distanceTo(const Vec3 &p) const
    {
      return a * p.x + b * p.y + c * p.z + d;
    }

    void normalize()
    {
      float len = std::sqrt(a * a + b * b + c * c);
      if (len < 1e-6f)
        return;
      float inv = 1.0f / len;
      a *= inv;
      b *= inv;
      c *= inv;
      d *= inv;
    }
  };

  struct Frustum
  {
    Plane planes[6]; // Left, Right, Bottom, Top, Near, Far

    /// Extract frustum planes from a combined view-projection matrix.
    static Frustum fromViewProjection(const Mat4 &vp)
    {
      Frustum f;
      // Left:   row3 + row0
      f.planes[0] = {vp.m[0][3] + vp.m[0][0], vp.m[1][3] + vp.m[1][0],
                      vp.m[2][3] + vp.m[2][0], vp.m[3][3] + vp.m[3][0]};
      // Right:  row3 - row0
      f.planes[1] = {vp.m[0][3] - vp.m[0][0], vp.m[1][3] - vp.m[1][0],
                      vp.m[2][3] - vp.m[2][0], vp.m[3][3] - vp.m[3][0]};
      // Bottom: row3 + row1
      f.planes[2] = {vp.m[0][3] + vp.m[0][1], vp.m[1][3] + vp.m[1][1],
                      vp.m[2][3] + vp.m[2][1], vp.m[3][3] + vp.m[3][1]};
      // Top:    row3 - row1
      f.planes[3] = {vp.m[0][3] - vp.m[0][1], vp.m[1][3] - vp.m[1][1],
                      vp.m[2][3] - vp.m[2][1], vp.m[3][3] - vp.m[3][1]};
      // Near:   row3 + row2
      f.planes[4] = {vp.m[0][3] + vp.m[0][2], vp.m[1][3] + vp.m[1][2],
                      vp.m[2][3] + vp.m[2][2], vp.m[3][3] + vp.m[3][2]};
      // Far:    row3 - row2
      f.planes[5] = {vp.m[0][3] - vp.m[0][2], vp.m[1][3] - vp.m[1][2],
                      vp.m[2][3] - vp.m[2][2], vp.m[3][3] - vp.m[3][2]};

      for (auto &plane : f.planes)
      {
        plane.normalize();
      }
      return f;
    }

    /// Test if a sphere is at least partially inside the frustum.
    bool containsSphere(const Vec3 &center, float radius) const
    {
      for (const auto &plane : planes)
      {
        if (plane.distanceTo(center) < -radius)
        {
          return false;
        }
      }
      return true;
    }

    /// Test if an AABB is at least partially inside the frustum.
    bool containsAABB(const Vec3 &minCorner, const Vec3 &maxCorner) const
    {
      for (const auto &plane : planes)
      {
        // Find the p-vertex (furthest along plane normal)
        Vec3 pVertex = {
            (plane.a >= 0.0f) ? maxCorner.x : minCorner.x,
            (plane.b >= 0.0f) ? maxCorner.y : minCorner.y,
            (plane.c >= 0.0f) ? maxCorner.z : minCorner.z};

        if (plane.distanceTo(pVertex) < 0.0f)
        {
          return false;
        }
      }
      return true;
    }
  };

  // -------------------------------------------------------------------------
  // Utility: build a model matrix from position, rotation, scale
  // -------------------------------------------------------------------------
  inline Mat4 buildModelMatrix(const Vec3 &position, const Quat &rotation, const Vec3 &scale)
  {
    return Mat4::translate(position) * rotation.toMat4() * Mat4::scaleMatrix(scale);
  }

} // namespace hades::math

#endif
