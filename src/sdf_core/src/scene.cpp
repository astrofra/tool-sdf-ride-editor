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

}  // namespace

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
    make_box("debris_slab", {-8.0f, 1.0f, 20.0f}, {2.0f, 1.0f, 2.0f}),
    make_box("left_tower_opening", {-18.0f, 20.0f, 18.0f}, {2.0f, 4.0f, 2.0f}, CsgOp::Subtract),
    make_box("right_tower_opening", {16.0f, 20.0f, 13.0f}, {1.5f, 4.0f, 1.5f}, CsgOp::Subtract)
  };

  return scene;
}

BuildSettings make_frame_006_build_settings()
{
  BuildSettings settings;
  settings.bounds.min = {-36.0f, -4.0f, -16.0f};
  settings.bounds.max = {42.0f, 42.0f, 96.0f};
  settings.cell_size = 1.0f;
  return settings;
}

}  // namespace sdf

