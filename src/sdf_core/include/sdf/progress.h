#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace sdf
{

struct ProgressUpdate
{
  std::string_view stage;
  std::uint64_t completed = 0;
  std::uint64_t total = 0;
  int percent = 0;
};

using ProgressCallback = std::function<void(const ProgressUpdate &)>;

}  // namespace sdf
