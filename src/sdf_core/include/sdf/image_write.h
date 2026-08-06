#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sdf
{

struct Rgb8Image
{
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

bool write_png_rgb8(
  const Rgb8Image &image,
  const std::filesystem::path &output_path,
  std::string *error_message = nullptr);

}  // namespace sdf

