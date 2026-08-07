#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "sdf/mesh.h"
#include "sdf/progress.h"

namespace sdf
{

struct UvUnwrapSettings
{
  std::uint32_t resolution = 1024;
  std::uint32_t padding = 8;
  std::uint32_t max_chart_size = 0;
  std::uint32_t chart_max_iterations = 4;
  float texels_per_unit = 0.0f;
  float epsilon = 1.0e-5f;
  float chart_max_cost = 8.0f;
  float normal_deviation_weight = 1.0f;
  float roundness_weight = 0.001f;
  float straightness_weight = 1.0f;
  float normal_seam_weight = 0.5f;
  float texture_seam_weight = 0.0f;
  bool brute_force = false;
  bool bilinear = true;
  bool block_align = true;
  bool rotate_charts = true;
  bool rotate_charts_to_axis = true;
};

struct UvUnwrapResult
{
  std::uint32_t atlas_width = 0;
  std::uint32_t atlas_height = 0;
  std::uint32_t atlas_count = 0;
  std::uint32_t chart_count = 0;
  std::size_t vertex_count = 0;
  std::size_t triangle_count = 0;
  float texels_per_unit = 0.0f;
  float utilization = 0.0f;
};

bool unwrap_mesh_uvs(
  const Mesh &input_mesh,
  const UvUnwrapSettings &settings,
  Mesh *output_mesh,
  UvUnwrapResult *result = nullptr,
  std::string *error_message = nullptr,
  const ProgressCallback &progress_callback = {});

}  // namespace sdf
