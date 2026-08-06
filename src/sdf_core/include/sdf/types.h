#pragma once

#include <algorithm>
#include <cmath>

namespace sdf
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

struct Aabb
{
  Vec3 min;
  Vec3 max;
};

inline Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs)
{
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs)
{
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3 operator*(const Vec3 &lhs, float scalar)
{
  return {lhs.x * scalar, lhs.y * scalar, lhs.z * scalar};
}

inline Vec3 max_components(const Vec3 &lhs, const Vec3 &rhs)
{
  return {
    std::max(lhs.x, rhs.x),
    std::max(lhs.y, rhs.y),
    std::max(lhs.z, rhs.z)
  };
}

inline Vec3 abs_components(const Vec3 &value)
{
  return {
    std::fabs(value.x),
    std::fabs(value.y),
    std::fabs(value.z)
  };
}

inline float max_component(const Vec3 &value)
{
  return std::max(value.x, std::max(value.y, value.z));
}

inline float length(const Vec3 &value)
{
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline float dot(const Vec3 &lhs, const Vec3 &rhs)
{
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 cross(const Vec3 &lhs, const Vec3 &rhs)
{
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x
  };
}

inline Vec3 normalized(const Vec3 &value)
{
  const float value_length = length(value);
  if (value_length <= 1.0e-8f)
  {
    return {0.0f, 1.0f, 0.0f};
  }

  const float inv_length = 1.0f / value_length;
  return {value.x * inv_length, value.y * inv_length, value.z * inv_length};
}

}  // namespace sdf
