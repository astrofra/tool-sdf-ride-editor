#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "sdf/image_write.h"
#include "sdf/mesh.h"
#include "sdf/progress.h"
#include "sdf/raytrace.h"

namespace sdf
{

struct AoBakeSettings
{
  int width = 0;
  int height = 0;
  int min_ao_samples = 4;
  int max_ao_samples = 128;
  int denoise_passes = 2;
  int denoise_radius = 4;
  int dilation_passes = -1;
  float ao_error_threshold = 0.03f;
  float ao_max_distance = 25.0f;
  bool flip_v = true;
  std::uint32_t seed = 1337;
};

struct AoBakeResult
{
  std::size_t baked_texels = 0;
  std::size_t dilated_texels = 0;
  std::size_t covered_texels = 0;
  std::size_t ao_ray_count = 0;
  float average_ao_samples_per_baked_texel = 0.0f;
  int denoise_passes = 0;
  int denoise_radius = 0;
  int dilation_passes = 0;
};

bool bake_ambient_occlusion_texture(
  const Mesh &uv_mesh,
  const RayScene &ray_scene,
  const AoBakeSettings &settings,
  Rgb8Image *image,
  AoBakeResult *result = nullptr,
  std::string *error_message = nullptr,
  const ProgressCallback &progress_callback = {});

}  // namespace sdf
