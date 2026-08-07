#pragma once

#include <cstdint>
#include <vector>

#include "sdf/types.h"

namespace sdf
{

struct MeshVertex
{
  Vec3 position;
  Vec3 normal;
  Vec2 uv0;
};

struct MeshTriangle
{
  std::uint32_t i0 = 0;
  std::uint32_t i1 = 0;
  std::uint32_t i2 = 0;
  std::uint32_t material_id = 0;
  std::int32_t uv_chart_id = -1;
};

struct Mesh
{
  std::vector<MeshVertex> vertices;
  std::vector<MeshTriangle> triangles;
};

}  // namespace sdf
