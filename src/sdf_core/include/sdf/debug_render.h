#pragma once

#include <cstdint>
#include <string>

#include "sdf/image_write.h"
#include "sdf/raytrace.h"

namespace sdf
{

enum class DebugRenderMode : std::uint8_t
{
  Depth = 0,
  Normal = 1,
  Ao = 2
};

enum class DebugCameraPreset : std::uint8_t
{
  Front = 0,
  LeftThreeQuarter = 1,
  RightThreeQuarter = 2
};

struct DebugRenderSettings
{
  int width = 1024;
  int height = 576;
  DebugRenderMode mode = DebugRenderMode::Depth;
  DebugCameraPreset camera_preset = DebugCameraPreset::Front;
  int ao_samples = 16;
  float ao_max_distance = 12.0f;
  float ortho_margin = 1.10f;
  std::uint32_t seed = 1337;
};

bool render_debug_image(
  const RayScene &scene,
  const DebugRenderSettings &settings,
  Rgb8Image *image,
  std::string *error_message = nullptr);

}  // namespace sdf

