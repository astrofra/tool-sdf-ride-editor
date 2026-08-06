#include "sdf/generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sdf
{

namespace
{

struct GridDimensions
{
  int nx = 0;
  int ny = 0;
  int nz = 0;
};

std::size_t grid_index(const GridDimensions &dims, int x, int y, int z)
{
  return static_cast<std::size_t>((z * dims.ny + y) * dims.nx + x);
}

void append_face(
  Mesh &mesh,
  const Vec3 &v0,
  const Vec3 &v1,
  const Vec3 &v2,
  const Vec3 &v3,
  const Vec3 &normal)
{
  const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

  mesh.vertices.push_back({v0, normal, {0.0f, 0.0f}});
  mesh.vertices.push_back({v1, normal, {1.0f, 0.0f}});
  mesh.vertices.push_back({v2, normal, {1.0f, 1.0f}});
  mesh.vertices.push_back({v3, normal, {0.0f, 1.0f}});

  mesh.triangles.push_back({base + 0, base + 1, base + 2, 0});
  mesh.triangles.push_back({base + 0, base + 2, base + 3, 0});
}

bool is_inside_grid(const GridDimensions &dims, int x, int y, int z)
{
  return x >= 0 && x < dims.nx && y >= 0 && y < dims.ny && z >= 0 && z < dims.nz;
}

}  // namespace

float evaluate_box_sdf(const SdfBox &box, const Vec3 &point)
{
  const Vec3 local = point - box.transform.translation;
  const Vec3 q = abs_components(local) - box.half_size;
  const Vec3 outside = max_components(q, {0.0f, 0.0f, 0.0f});
  const float outside_distance = length(outside);
  const float inside_distance = std::min(max_component(q), 0.0f);
  return outside_distance + inside_distance;
}

float evaluate_scene_sdf(const SceneDocument &scene, const Vec3 &point)
{
  float distance = std::numeric_limits<float>::max();
  bool has_positive_shape = false;

  for (const SdfBox &box : scene.boxes)
  {
    const float box_distance = evaluate_box_sdf(box, point);

    if (box.op == CsgOp::Add)
    {
      distance = has_positive_shape ? std::min(distance, box_distance) : box_distance;
      has_positive_shape = true;
      continue;
    }

    if (has_positive_shape)
    {
      distance = std::max(distance, -box_distance);
    }
  }

  if (!has_positive_shape)
  {
    return std::numeric_limits<float>::max();
  }

  return distance;
}

SceneBuildResult build_scene_mesh(const SceneDocument &scene, const BuildSettings &settings)
{
  if (settings.cell_size <= 0.0f)
  {
    throw std::invalid_argument("cell_size must be strictly positive");
  }

  const Vec3 span = settings.bounds.max - settings.bounds.min;
  const GridDimensions dims = {
    std::max(1, static_cast<int>(std::ceil(span.x / settings.cell_size))),
    std::max(1, static_cast<int>(std::ceil(span.y / settings.cell_size))),
    std::max(1, static_cast<int>(std::ceil(span.z / settings.cell_size)))
  };

  std::vector<std::uint8_t> occupancy(static_cast<std::size_t>(dims.nx) * dims.ny * dims.nz, 0);

  auto cell_min = [&](int x, int y, int z) -> Vec3
  {
    return {
      settings.bounds.min.x + static_cast<float>(x) * settings.cell_size,
      settings.bounds.min.y + static_cast<float>(y) * settings.cell_size,
      settings.bounds.min.z + static_cast<float>(z) * settings.cell_size
    };
  };

  auto cell_center = [&](int x, int y, int z) -> Vec3
  {
    const float half_cell = settings.cell_size * 0.5f;
    const Vec3 min_corner = cell_min(x, y, z);
    return {
      min_corner.x + half_cell,
      min_corner.y + half_cell,
      min_corner.z + half_cell
    };
  };

  SceneBuildResult result;
  result.sampled_cells = occupancy.size();

  for (int z = 0; z < dims.nz; ++z)
  {
    for (int y = 0; y < dims.ny; ++y)
    {
      for (int x = 0; x < dims.nx; ++x)
      {
        if (evaluate_scene_sdf(scene, cell_center(x, y, z)) <= 0.0f)
        {
          occupancy[grid_index(dims, x, y, z)] = 1;
          ++result.occupied_cells;
        }
      }
    }
  }

  auto is_occupied = [&](int x, int y, int z) -> bool
  {
    if (!is_inside_grid(dims, x, y, z))
    {
      return false;
    }

    return occupancy[grid_index(dims, x, y, z)] != 0;
  };

  for (int z = 0; z < dims.nz; ++z)
  {
    for (int y = 0; y < dims.ny; ++y)
    {
      for (int x = 0; x < dims.nx; ++x)
      {
        if (!is_occupied(x, y, z))
        {
          continue;
        }

        const Vec3 min_corner = cell_min(x, y, z);
        const float x0 = min_corner.x;
        const float y0 = min_corner.y;
        const float z0 = min_corner.z;
        const float x1 = x0 + settings.cell_size;
        const float y1 = y0 + settings.cell_size;
        const float z1 = z0 + settings.cell_size;

        if (!is_occupied(x + 1, y, z))
        {
          append_face(
            result.mesh,
            {x1, y0, z0},
            {x1, y1, z0},
            {x1, y1, z1},
            {x1, y0, z1},
            {1.0f, 0.0f, 0.0f});
        }

        if (!is_occupied(x - 1, y, z))
        {
          append_face(
            result.mesh,
            {x0, y0, z1},
            {x0, y1, z1},
            {x0, y1, z0},
            {x0, y0, z0},
            {-1.0f, 0.0f, 0.0f});
        }

        if (!is_occupied(x, y + 1, z))
        {
          append_face(
            result.mesh,
            {x0, y1, z1},
            {x1, y1, z1},
            {x1, y1, z0},
            {x0, y1, z0},
            {0.0f, 1.0f, 0.0f});
        }

        if (!is_occupied(x, y - 1, z))
        {
          append_face(
            result.mesh,
            {x0, y0, z0},
            {x1, y0, z0},
            {x1, y0, z1},
            {x0, y0, z1},
            {0.0f, -1.0f, 0.0f});
        }

        if (!is_occupied(x, y, z + 1))
        {
          append_face(
            result.mesh,
            {x0, y0, z1},
            {x1, y0, z1},
            {x1, y1, z1},
            {x0, y1, z1},
            {0.0f, 0.0f, 1.0f});
        }

        if (!is_occupied(x, y, z - 1))
        {
          append_face(
            result.mesh,
            {x1, y0, z0},
            {x0, y0, z0},
            {x0, y1, z0},
            {x1, y1, z0},
            {0.0f, 0.0f, -1.0f});
        }
      }
    }
  }

  return result;
}

}  // namespace sdf

