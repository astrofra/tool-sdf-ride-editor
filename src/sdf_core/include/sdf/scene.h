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

enum class ModifierMask : std::uint8_t
{
  All = 0,
  Top = 1,
  Bottom = 2,
  Edges = 3,
  TopEdges = 4
};

enum class MeshingMode : std::uint8_t
{
  MarchingTetrahedra = 0,
  DualContouring = 1,
  AdaptiveDualContouring = 2
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

struct NoiseDisplaceMaskedModifier
{
  std::string name;
  std::string target_box_name;
  float amplitude = 0.0f;
  float frequency = 0.0f;
  std::uint32_t seed = 0;
  std::uint32_t octaves = 1;
  ModifierMask mask = ModifierMask::All;
  float mask_width = 1.0f;
};

struct BoxCutModifier
{
  std::string name;
  std::string target_box_name;
  Vec3 translation;
  Vec3 half_size;
};

struct SceneDocument
{
  std::string name;
  std::vector<SdfBox> boxes;
  std::vector<NoiseDisplaceMaskedModifier> noise_modifiers;
  std::vector<BoxCutModifier> box_cut_modifiers;
};

struct BuildSettings
{
  Aabb bounds;
  float cell_size = 1.0f;
  MeshingMode meshing_mode = MeshingMode::MarchingTetrahedra;
};

const char *meshing_mode_name(MeshingMode mode);
bool parse_meshing_mode_name(const std::string &text, MeshingMode *mode);

SceneDocument make_frame_006_blockout_scene();
BuildSettings make_frame_006_build_settings();

}  // namespace sdf
