#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sdf/types.h"

namespace sdf
{

enum class CsgOp : std::uint8_t
{
  Add = 0,
  Subtract = 1
};

struct Transform
{
  Vec3 translation;
};

struct SdfBox
{
  std::string name;
  Transform transform;
  Vec3 half_size;
  std::uint32_t material_id = 0;
  CsgOp op = CsgOp::Add;
};

struct SceneDocument
{
  std::string name;
  std::vector<SdfBox> boxes;
};

struct BuildSettings
{
  Aabb bounds;
  float cell_size = 1.0f;
};

SceneDocument make_frame_006_blockout_scene();
BuildSettings make_frame_006_build_settings();

}  // namespace sdf

