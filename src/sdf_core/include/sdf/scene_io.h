#pragma once

#include <filesystem>
#include <string>

#include "sdf/scene.h"

namespace sdf
{

struct SceneFile
{
  SceneDocument scene;
  BuildSettings build_settings;
};

bool load_scene_file(
  const std::filesystem::path &input_path,
  SceneFile *out_scene_file,
  std::string *error_message = nullptr);

}  // namespace sdf

