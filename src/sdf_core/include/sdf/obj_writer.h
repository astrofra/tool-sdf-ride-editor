#pragma once

#include <filesystem>
#include <string>

#include "sdf/mesh.h"

namespace sdf
{

struct ObjWriteOptions
{
  std::string object_name = "blockout";
  std::string material_name = "ao_preview";
  std::filesystem::path diffuse_texture_path;
};

bool write_obj(
  const Mesh &mesh,
  const std::filesystem::path &output_path,
  const ObjWriteOptions &options = {},
  std::string *error_message = nullptr);

}  // namespace sdf
