#pragma once

#include <filesystem>
#include <string>

#include "sdf/mesh.h"

namespace sdf
{

bool write_obj(const Mesh &mesh, const std::filesystem::path &output_path, std::string *error_message = nullptr);

}  // namespace sdf

