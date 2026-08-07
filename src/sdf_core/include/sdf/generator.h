#pragma once

#include <cstddef>

#include "sdf/mesh.h"
#include "sdf/progress.h"
#include "sdf/scene.h"

namespace sdf
{

struct SceneBuildResult
{
  Mesh mesh;
  std::size_t sampled_cells = 0;
  std::size_t occupied_cells = 0;
};

float evaluate_box_sdf(const SdfBox &box, const Vec3 &point);
float evaluate_scene_sdf(const SceneDocument &scene, const Vec3 &point);
SceneBuildResult build_scene_mesh(
  const SceneDocument &scene,
  const BuildSettings &settings,
  const ProgressCallback &progress_callback = {});

}  // namespace sdf
