#pragma once

#include <cstddef>
#include <string>

#include "sdf/image_write.h"
#include "sdf/mesh.h"
#include "sdf/progress.h"
#include "sdf/scene.h"

namespace sdf
{

struct NormalBakeSettings
{
  int width = 0;
  int height = 0;
  int dilation_passes = -1;
  int projection_iterations = 4;
  float surface_epsilon = 0.0f;
  bool flip_v = true;
};

struct NormalBakeResult
{
  std::size_t baked_texels = 0;
  std::size_t dilated_texels = 0;
  std::size_t covered_texels = 0;
  int dilation_passes = 0;
};

bool bake_sdf_normal_texture(
  const SceneDocument &scene,
  const Mesh &uv_mesh,
  const NormalBakeSettings &settings,
  Rgb8Image *image,
  NormalBakeResult *result = nullptr,
  std::string *error_message = nullptr,
  const ProgressCallback &progress_callback = {});

}  // namespace sdf
