#define _CRT_SECURE_NO_WARNINGS

#include "sdf/image_write.h"

#include <filesystem>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace sdf
{

bool write_png_rgb8(
  const Rgb8Image &image,
  const std::filesystem::path &output_path,
  std::string *error_message)
{
  if (image.width <= 0 || image.height <= 0)
  {
    if (error_message != nullptr)
    {
      *error_message = "image dimensions must be strictly positive";
    }
    return false;
  }

  if (image.pixels.size() != static_cast<std::size_t>(image.width * image.height * 3))
  {
    if (error_message != nullptr)
    {
      *error_message = "image pixel buffer size does not match width*height*3";
    }
    return false;
  }

  std::error_code create_error;
  std::filesystem::create_directories(output_path.parent_path(), create_error);
  if (create_error)
  {
    if (error_message != nullptr)
    {
      *error_message = "failed to create image output directory";
    }
    return false;
  }

  const int stride = image.width * 3;
  const int ok = stbi_write_png(
    output_path.string().c_str(),
    image.width,
    image.height,
    3,
    image.pixels.data(),
    stride);

  if (ok == 0)
  {
    if (error_message != nullptr)
    {
      *error_message = "stbi_write_png failed";
    }
    return false;
  }

  return true;
}

}  // namespace sdf
