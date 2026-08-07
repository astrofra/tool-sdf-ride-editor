#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "sdf/progress.h"

namespace sdf::detail
{

class ProgressScope
{
public:
  ProgressScope(const ProgressCallback &callback, std::string_view stage, std::uint64_t total)
    : callback_(callback), stage_(stage), total_(std::max<std::uint64_t>(total, 1u))
  {
    if (callback_)
    {
      dispatch_locked(0);
    }
  }

  void update(std::uint64_t completed)
  {
    if (!callback_)
    {
      return;
    }

    std::scoped_lock lock(mutex_);
    dispatch_locked(std::min(completed, total_));
  }

  void finish()
  {
    update(total_);
  }

private:
  void dispatch_locked(std::uint64_t completed)
  {
    const int percent = completed >= total_
      ? 100
      : static_cast<int>((completed * 100u) / total_);
    if (percent <= last_percent_)
    {
      return;
    }

    last_percent_ = percent;
    callback_(ProgressUpdate{stage_, completed, total_, percent});
  }

  ProgressCallback callback_;
  std::mutex mutex_;
  std::string_view stage_;
  std::uint64_t total_ = 1;
  int last_percent_ = -1;
};

}  // namespace sdf::detail
