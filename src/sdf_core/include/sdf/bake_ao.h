#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "sdf/image_write.h"
#include "sdf/mesh.h"
#include "sdf/raytrace.h"

namespace sdf
{

struct AoBakeSettings
{
  int width = 0;
  int height = 0;
  int ao_samples = 8;
  int dilation_passes = -1;
  float ao_max_distance = 12.0f;
  bool flip_v = true;
  std::uint32_t seed = 1337;
};

struct AoBakeResult
{
  std::size_t baked_texels = 0;
  std::size_t dilated_texels = 0;
  std::size_t covered_texels = 0;
  int dilation_passes = 0;
};

bool bake_ambient_occlusion_texture(
  const Mesh &uv_mesh,
  const RayScene &ray_scene,
  const AoBakeSettings &settings,
  Rgb8Image *image,
  AoBakeResult *result = nullptr,
  std::string *error_message = nullptr);

}  // namespace sdf
