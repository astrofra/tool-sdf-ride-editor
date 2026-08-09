#pragma once

#include <cstddef>
#include <string>

#include "sdf/scene_io.h"
#include "sdf/uv_unwrap.h"

namespace sdf::lua_api
{

struct BuildRequest
{
  bool export_obj = true;
  std::string output_obj_path;
  bool unwrap_uvs = false;
  UvUnwrapSettings uv_unwrap_settings;
};

struct BuildResult
{
  bool obj_written = false;
  std::string output_obj_path;
  std::size_t sampled_cells = 0;
  std::size_t occupied_cells = 0;
  std::size_t generated_vertex_count = 0;
  std::size_t generated_triangle_count = 0;
  std::size_t export_vertex_count = 0;
  std::size_t export_triangle_count = 0;
  bool uv_unwrap_performed = false;
  UvUnwrapResult uv_unwrap_result;
};

bool load_scene_file(
  const std::string &input_path,
  SceneFile &out_scene_file,
  std::string &error_message);

bool save_scene_file(
  const SceneFile &scene_file,
  const std::string &output_path,
  std::string &error_message);

bool build_scene_file(
  const SceneFile &scene_file,
  const BuildRequest &request,
  BuildResult &out_build_result,
  std::string &error_message);

}  // namespace sdf::lua_api
