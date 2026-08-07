#include "sdf/scene.h"

namespace sdf
{

namespace
{

SdfBox make_box(const char *name, Vec3 translation, Vec3 half_size, CsgOp op = CsgOp::Add)
{
  SdfBox box;
  box.name = name;
  box.transform.translation = translation;
  box.half_size = half_size;
  box.op = op;
  return box;
}

NoiseDisplaceMaskedModifier make_noise_modifier(
  const char *name,
  const char *target_box_name,
  float amplitude,
  float frequency,
  std::uint32_t seed,
  std::uint32_t octaves,
  ModifierMask mask,
  float mask_width)
{
  NoiseDisplaceMaskedModifier modifier;
  modifier.name = name;
  modifier.target_box_name = target_box_name;
  modifier.amplitude = amplitude;
  modifier.frequency = frequency;
  modifier.seed = seed;
  modifier.octaves = octaves;
  modifier.mask = mask;
  modifier.mask_width = mask_width;
  return modifier;
}

BoxCutModifier make_box_cut_modifier(const char *name, const char *target_box_name, Vec3 translation, Vec3 half_size)
{
  BoxCutModifier modifier;
  modifier.name = name;
  modifier.target_box_name = target_box_name;
  modifier.translation = translation;
  modifier.half_size = half_size;
  return modifier;
}

}  // namespace

const char *meshing_mode_name(MeshingMode mode)
{
  switch (mode)
  {
  case MeshingMode::MarchingTetrahedra:
    return "marching_tetrahedra";
  case MeshingMode::DualContouring:
    return "dual_contouring";
  default:
    return "unknown";
  }
}

bool parse_meshing_mode_name(const std::string &text, MeshingMode *mode)
{
  if (mode == nullptr)
  {
    return false;
  }

  if (text == "marching_tetrahedra")
  {
    *mode = MeshingMode::MarchingTetrahedra;
    return true;
  }

  if (text == "dual_contouring")
  {
    *mode = MeshingMode::DualContouring;
    return true;
  }

  return false;
}

SceneDocument make_frame_006_blockout_scene()
{
  SceneDocument scene;
  scene.name = "frame_006_blockout";

  scene.boxes = {
    make_box("ground_slab", {0.0f, -2.0f, 40.0f}, {32.0f, 2.0f, 52.0f}),
    make_box("left_tower", {-18.0f, 18.0f, 18.0f}, {7.0f, 18.0f, 7.0f}),
    make_box("left_catwalk_mass", {-13.0f, 30.0f, 18.0f}, {4.0f, 2.0f, 4.0f}),
    make_box("right_tower", {21.0f, 22.0f, 15.0f}, {10.0f, 22.0f, 12.0f}),
    make_box("right_wall", {28.0f, 8.0f, 38.0f}, {14.0f, 8.0f, 30.0f}),
    make_box("central_rear_mass", {0.0f, 14.0f, 56.0f}, {6.0f, 14.0f, 10.0f}),
    make_box("left_background_mass", {-8.0f, 10.0f, 40.0f}, {4.0f, 10.0f, 8.0f}),
    make_box("center_plinth", {0.0f, 3.0f, 26.0f}, {3.0f, 3.0f, 3.0f}),
    make_box("right_plinth", {10.0f, 2.0f, 24.0f}, {2.0f, 2.0f, 2.0f}),
    make_box("left_marker_head", {-20.0f, 7.0f, 8.0f}, {2.5f, 3.0f, 2.5f}),
    make_box("left_marker_stem", {-20.0f, 2.0f, 8.0f}, {1.0f, 2.0f, 1.0f}),
    make_box("debris_slab", {-8.0f, 1.0f, 20.0f}, {2.0f, 1.0f, 2.0f})
  };

  scene.noise_modifiers = {
    make_noise_modifier("left_tower_top_edges", "left_tower", 0.75f, 0.18f, 17, 3, ModifierMask::TopEdges, 4.0f),
    make_noise_modifier("right_tower_edges", "right_tower", 0.70f, 0.16f, 29, 3, ModifierMask::Edges, 3.5f),
    make_noise_modifier("rear_mass_top", "central_rear_mass", 0.55f, 0.11f, 43, 2, ModifierMask::Top, 4.5f),
    make_noise_modifier("left_background_bottom", "left_background_mass", 0.45f, 0.20f, 61, 2, ModifierMask::Bottom, 3.0f),
    make_noise_modifier("catwalk_all", "left_catwalk_mass", 0.25f, 0.28f, 73, 2, ModifierMask::All, 1.5f)
  };

  scene.box_cut_modifiers = {
    make_box_cut_modifier("left_tower_opening", "left_tower", {0.0f, 2.0f, 0.0f}, {2.0f, 4.0f, 2.0f}),
    make_box_cut_modifier("right_tower_opening", "right_tower", {-5.0f, -2.0f, -2.0f}, {1.5f, 4.0f, 1.5f})
  };

  return scene;
}

BuildSettings make_frame_006_build_settings()
{
  BuildSettings settings;
  settings.bounds.min = {-40.0f, -6.0f, -20.0f};
  settings.bounds.max = {46.0f, 48.0f, 100.0f};
  settings.cell_size = 1.0f;
  settings.meshing_mode = MeshingMode::MarchingTetrahedra;
  return settings;
}

}  // namespace sdf
