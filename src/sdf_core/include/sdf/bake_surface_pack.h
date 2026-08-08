#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "sdf/image_write.h"
#include "sdf/mesh.h"
#include "sdf/progress.h"
#include "sdf/scene.h"

namespace sdf
{

struct SurfacePackSettings
{
  int width = 0;
  int height = 0;
  int dilation_passes = -1;
  int projection_iterations = 4;
  int thickness_filter_passes = 2;
  int thickness_filter_radius = 3;
  float surface_epsilon = 0.0f;
  float curvature_sample_radius = 0.0f;
  float thickness_max_distance = 5.0f;
  float thickness_cone_angle_degrees = 30.0f;
  bool flip_v = true;
  std::uint32_t seed = 1337;
};

struct SurfacePackResult
{
  std::size_t baked_texels = 0;
  std::size_t dilated_texels = 0;
  std::size_t covered_texels = 0;
  int thickness_filter_passes = 0;
  int thickness_filter_radius = 0;
  int dilation_passes = 0;
};

bool bake_surface_pack_texture(
  const SceneDocument &scene,
  const Mesh &uv_mesh,
  const Rgb8Image &ao_image,
  const SurfacePackSettings &settings,
  Rgb8Image *image,
  SurfacePackResult *result = nullptr,
  std::string *error_message = nullptr,
  const ProgressCallback &progress_callback = {});

}  // namespace sdf
