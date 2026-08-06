#include "sdf/generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

struct SurfaceVertex
{
  Vec3 position;
  Vec3 normal;
};

constexpr std::array<std::array<int, 4>, 6> kCubeTetrahedra = {{
  {{0, 5, 1, 6}},
  {{0, 1, 2, 6}},
  {{0, 2, 3, 6}},
  {{0, 3, 7, 6}},
  {{0, 7, 4, 6}},
  {{0, 4, 5, 6}}
}};

std::size_t grid_index(const GridDimensions &dims, int x, int y, int z)
{
  return static_cast<std::size_t>((z * dims.ny + y) * dims.nx + x);
}

float clamp01(float value)
{
  return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep01(float value)
{
  const float t = clamp01(value);
  return t * t * (3.0f - 2.0f * t);
}

std::uint32_t mix_hash(std::uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

float hash_noise(int x, int y, int z, std::uint32_t seed)
{
  std::uint32_t h = seed;
  h ^= mix_hash(static_cast<std::uint32_t>(x) + 0x9e3779b9U);
  h ^= mix_hash(static_cast<std::uint32_t>(y) + 0x85ebca6bU);
  h ^= mix_hash(static_cast<std::uint32_t>(z) + 0xc2b2ae35U);
  h = mix_hash(h);
  return static_cast<float>(h) / static_cast<float>(std::numeric_limits<std::uint32_t>::max()) * 2.0f - 1.0f;
}

float lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

float value_noise_3d(const Vec3 &point, std::uint32_t seed)
{
  const int x0 = static_cast<int>(std::floor(point.x));
  const int y0 = static_cast<int>(std::floor(point.y));
  const int z0 = static_cast<int>(std::floor(point.z));
  const int x1 = x0 + 1;
  const int y1 = y0 + 1;
  const int z1 = z0 + 1;

  const float tx = smoothstep01(point.x - static_cast<float>(x0));
  const float ty = smoothstep01(point.y - static_cast<float>(y0));
  const float tz = smoothstep01(point.z - static_cast<float>(z0));

  const float c000 = hash_noise(x0, y0, z0, seed);
  const float c100 = hash_noise(x1, y0, z0, seed);
  const float c010 = hash_noise(x0, y1, z0, seed);
  const float c110 = hash_noise(x1, y1, z0, seed);
  const float c001 = hash_noise(x0, y0, z1, seed);
  const float c101 = hash_noise(x1, y0, z1, seed);
  const float c011 = hash_noise(x0, y1, z1, seed);
  const float c111 = hash_noise(x1, y1, z1, seed);

  const float x00 = lerp(c000, c100, tx);
  const float x10 = lerp(c010, c110, tx);
  const float x01 = lerp(c001, c101, tx);
  const float x11 = lerp(c011, c111, tx);

  const float y0v = lerp(x00, x10, ty);
  const float y1v = lerp(x01, x11, ty);
  return lerp(y0v, y1v, tz);
}

float fractal_noise_3d(const Vec3 &point, std::uint32_t seed, std::uint32_t octaves)
{
  float amplitude = 1.0f;
  float total = 0.0f;
  float normalization = 0.0f;
  Vec3 octave_point = point;

  for (std::uint32_t octave = 0; octave < octaves; ++octave)
  {
    total += value_noise_3d(octave_point, seed + octave * 1013U) * amplitude;
    normalization += amplitude;
    amplitude *= 0.5f;
    octave_point = octave_point * 2.0f;
  }

  return normalization > 0.0f ? total / normalization : 0.0f;
}

float edge_band_factor(float coordinate, float half_extent, float width)
{
  const float distance_to_edge = std::fabs(coordinate) - std::max(0.0f, half_extent - width);
  return smoothstep01(distance_to_edge / width);
}

float top_band_factor(float coordinate, float half_extent, float width)
{
  const float distance_to_top_band = coordinate - std::max(-half_extent, half_extent - width);
  return smoothstep01(distance_to_top_band / width);
}

float bottom_band_factor(float coordinate, float half_extent, float width)
{
  const float distance_to_bottom_band = (-coordinate) - std::max(-half_extent, half_extent - width);
  return smoothstep01(distance_to_bottom_band / width);
}

float evaluate_mask_weight(const NoiseDisplaceMaskedModifier &modifier, const SdfBox &box, const Vec3 &local_point)
{
  const float width = std::max(modifier.mask_width, 1.0e-4f);
  const float top = top_band_factor(local_point.y, box.half_size.y, width);
  const float bottom = bottom_band_factor(local_point.y, box.half_size.y, width);
  const float edge_x = edge_band_factor(local_point.x, box.half_size.x, width);
  const float edge_y = edge_band_factor(local_point.y, box.half_size.y, width);
  const float edge_z = edge_band_factor(local_point.z, box.half_size.z, width);
  const float edges = std::max(edge_x, std::max(edge_y, edge_z));
  const float top_edges = top * std::max(edge_x, edge_z);

  switch (modifier.mask)
  {
  case ModifierMask::All:
    return 1.0f;
  case ModifierMask::Top:
    return top;
  case ModifierMask::Bottom:
    return bottom;
  case ModifierMask::Edges:
    return edges;
  case ModifierMask::TopEdges:
    return top_edges;
  default:
    return 0.0f;
  }
}

float evaluate_local_box_sdf(const Vec3 &local_point, const Vec3 &half_size)
{
  const Vec3 q = abs_components(local_point) - half_size;
  const Vec3 outside = max_components(q, {0.0f, 0.0f, 0.0f});
  const float outside_distance = length(outside);
  const float inside_distance = std::min(max_component(q), 0.0f);
  return outside_distance + inside_distance;
}

float evaluate_modified_box_sdf(const SceneDocument &scene, const SdfBox &box, const Vec3 &point)
{
  const Vec3 local_point = point - box.transform.translation;
  float distance = evaluate_local_box_sdf(local_point, box.half_size);

  for (const BoxCutModifier &modifier : scene.box_cut_modifiers)
  {
    if (modifier.target_box_name != box.name)
    {
      continue;
    }

    const float cut_distance = evaluate_local_box_sdf(local_point - modifier.translation, modifier.half_size);
    distance = std::max(distance, -cut_distance);
  }

  for (const NoiseDisplaceMaskedModifier &modifier : scene.noise_modifiers)
  {
    if (modifier.target_box_name != box.name || modifier.amplitude <= 0.0f)
    {
      continue;
    }

    const float mask_weight = evaluate_mask_weight(modifier, box, local_point);
    if (mask_weight <= 0.0f)
    {
      continue;
    }

    const Vec3 noise_point = local_point * modifier.frequency;
    const float noise_value = fractal_noise_3d(noise_point, modifier.seed, modifier.octaves);
    distance -= noise_value * modifier.amplitude * mask_weight;
  }

  return distance;
}

Vec2 compute_default_uv(const Vec3 &position, const Vec3 &normal)
{
  const Vec3 abs_normal = abs_components(normal);
  const float scale = 0.1f;

  if (abs_normal.y >= abs_normal.x && abs_normal.y >= abs_normal.z)
  {
    return {position.x * scale, position.z * scale};
  }

  if (abs_normal.x >= abs_normal.y && abs_normal.x >= abs_normal.z)
  {
    return {position.z * scale, position.y * scale};
  }

  return {position.x * scale, position.y * scale};
}

Vec3 estimate_surface_normal(const SceneDocument &scene, const Vec3 &point, float epsilon)
{
  const Vec3 gradient = {
    evaluate_scene_sdf(scene, {point.x + epsilon, point.y, point.z}) -
      evaluate_scene_sdf(scene, {point.x - epsilon, point.y, point.z}),
    evaluate_scene_sdf(scene, {point.x, point.y + epsilon, point.z}) -
      evaluate_scene_sdf(scene, {point.x, point.y - epsilon, point.z}),
    evaluate_scene_sdf(scene, {point.x, point.y, point.z + epsilon}) -
      evaluate_scene_sdf(scene, {point.x, point.y, point.z - epsilon})
  };

  return normalized(gradient);
}

SurfaceVertex interpolate_surface_vertex(
  const SceneDocument &scene,
  const Vec3 &point_a,
  float value_a,
  const Vec3 &point_b,
  float value_b,
  float gradient_epsilon)
{
  const float denominator = value_a - value_b;
  const float raw_t = std::fabs(denominator) > 1.0e-8f ? value_a / denominator : 0.5f;
  const float t = std::clamp(raw_t, 0.0f, 1.0f);
  const Vec3 position = point_a + (point_b - point_a) * t;

  SurfaceVertex vertex;
  vertex.position = position;
  vertex.normal = estimate_surface_normal(scene, position, gradient_epsilon);
  return vertex;
}

void append_triangle(Mesh &mesh, SurfaceVertex v0, SurfaceVertex v1, SurfaceVertex v2)
{
  const Vec3 face_normal = cross(v1.position - v0.position, v2.position - v0.position);
  const Vec3 average_normal = normalized(v0.normal + v1.normal + v2.normal);

  if (dot(face_normal, average_normal) < 0.0f)
  {
    std::swap(v1, v2);
  }

  const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

  mesh.vertices.push_back({v0.position, v0.normal, compute_default_uv(v0.position, v0.normal)});
  mesh.vertices.push_back({v1.position, v1.normal, compute_default_uv(v1.position, v1.normal)});
  mesh.vertices.push_back({v2.position, v2.normal, compute_default_uv(v2.position, v2.normal)});

  mesh.triangles.push_back({base + 0, base + 1, base + 2, 0});
}

void polygonize_tetrahedron(
  const SceneDocument &scene,
  const std::array<Vec3, 4> &points,
  const std::array<float, 4> &values,
  float gradient_epsilon,
  Mesh &mesh)
{
  std::array<int, 4> inside_indices = {};
  std::array<int, 4> outside_indices = {};
  int inside_count = 0;
  int outside_count = 0;

  for (int index = 0; index < 4; ++index)
  {
    if (values[index] <= 0.0f)
    {
      inside_indices[inside_count++] = index;
    }
    else
    {
      outside_indices[outside_count++] = index;
    }
  }

  if (inside_count == 0 || inside_count == 4)
  {
    return;
  }

  if (inside_count == 1)
  {
    const int inside = inside_indices[0];

    append_triangle(
      mesh,
      interpolate_surface_vertex(scene, points[inside], values[inside], points[outside_indices[0]], values[outside_indices[0]], gradient_epsilon),
      interpolate_surface_vertex(scene, points[inside], values[inside], points[outside_indices[1]], values[outside_indices[1]], gradient_epsilon),
      interpolate_surface_vertex(scene, points[inside], values[inside], points[outside_indices[2]], values[outside_indices[2]], gradient_epsilon));
    return;
  }

  if (inside_count == 3)
  {
    const int outside = outside_indices[0];

    append_triangle(
      mesh,
      interpolate_surface_vertex(scene, points[outside], values[outside], points[inside_indices[0]], values[inside_indices[0]], gradient_epsilon),
      interpolate_surface_vertex(scene, points[outside], values[outside], points[inside_indices[1]], values[inside_indices[1]], gradient_epsilon),
      interpolate_surface_vertex(scene, points[outside], values[outside], points[inside_indices[2]], values[inside_indices[2]], gradient_epsilon));
    return;
  }

  const SurfaceVertex v0 = interpolate_surface_vertex(
    scene,
    points[inside_indices[0]],
    values[inside_indices[0]],
    points[outside_indices[0]],
    values[outside_indices[0]],
    gradient_epsilon);
  const SurfaceVertex v1 = interpolate_surface_vertex(
    scene,
    points[inside_indices[0]],
    values[inside_indices[0]],
    points[outside_indices[1]],
    values[outside_indices[1]],
    gradient_epsilon);
  const SurfaceVertex v2 = interpolate_surface_vertex(
    scene,
    points[inside_indices[1]],
    values[inside_indices[1]],
    points[outside_indices[0]],
    values[outside_indices[0]],
    gradient_epsilon);
  const SurfaceVertex v3 = interpolate_surface_vertex(
    scene,
    points[inside_indices[1]],
    values[inside_indices[1]],
    points[outside_indices[1]],
    values[outside_indices[1]],
    gradient_epsilon);

  append_triangle(mesh, v0, v1, v3);
  append_triangle(mesh, v0, v3, v2);
}

}  // namespace

float evaluate_box_sdf(const SdfBox &box, const Vec3 &point)
{
  const Vec3 local = point - box.transform.translation;
  return evaluate_local_box_sdf(local, box.half_size);
}

float evaluate_scene_sdf(const SceneDocument &scene, const Vec3 &point)
{
  float distance = std::numeric_limits<float>::max();
  bool has_positive_shape = false;

  for (const SdfBox &box : scene.boxes)
  {
    const float box_distance = evaluate_modified_box_sdf(scene, box, point);

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
  const GridDimensions cell_dims = {
    std::max(1, static_cast<int>(std::ceil(span.x / settings.cell_size))),
    std::max(1, static_cast<int>(std::ceil(span.y / settings.cell_size))),
    std::max(1, static_cast<int>(std::ceil(span.z / settings.cell_size)))
  };
  const GridDimensions point_dims = {cell_dims.nx + 1, cell_dims.ny + 1, cell_dims.nz + 1};

  auto grid_point_position = [&](int x, int y, int z) -> Vec3
  {
    return {
      settings.bounds.min.x + static_cast<float>(x) * settings.cell_size,
      settings.bounds.min.y + static_cast<float>(y) * settings.cell_size,
      settings.bounds.min.z + static_cast<float>(z) * settings.cell_size
    };
  };

  std::vector<float> sdf_samples(static_cast<std::size_t>(point_dims.nx) * point_dims.ny * point_dims.nz, 0.0f);

  for (int z = 0; z < point_dims.nz; ++z)
  {
    for (int y = 0; y < point_dims.ny; ++y)
    {
      for (int x = 0; x < point_dims.nx; ++x)
      {
        sdf_samples[grid_index(point_dims, x, y, z)] = evaluate_scene_sdf(scene, grid_point_position(x, y, z));
      }
    }
  }

  auto sample_value = [&](int x, int y, int z) -> float
  {
    return sdf_samples[grid_index(point_dims, x, y, z)];
  };

  SceneBuildResult result;
  result.sampled_cells = static_cast<std::size_t>(cell_dims.nx) * cell_dims.ny * cell_dims.nz;

  const float half_cell = settings.cell_size * 0.5f;
  const float gradient_epsilon = std::max(settings.cell_size * 0.25f, 1.0e-3f);

  for (int z = 0; z < cell_dims.nz; ++z)
  {
    for (int y = 0; y < cell_dims.ny; ++y)
    {
      for (int x = 0; x < cell_dims.nx; ++x)
      {
        const std::array<Vec3, 8> cube_points = {{
          grid_point_position(x + 0, y + 0, z + 0),
          grid_point_position(x + 1, y + 0, z + 0),
          grid_point_position(x + 1, y + 1, z + 0),
          grid_point_position(x + 0, y + 1, z + 0),
          grid_point_position(x + 0, y + 0, z + 1),
          grid_point_position(x + 1, y + 0, z + 1),
          grid_point_position(x + 1, y + 1, z + 1),
          grid_point_position(x + 0, y + 1, z + 1)
        }};
        const std::array<float, 8> cube_values = {{
          sample_value(x + 0, y + 0, z + 0),
          sample_value(x + 1, y + 0, z + 0),
          sample_value(x + 1, y + 1, z + 0),
          sample_value(x + 0, y + 1, z + 0),
          sample_value(x + 0, y + 0, z + 1),
          sample_value(x + 1, y + 0, z + 1),
          sample_value(x + 1, y + 1, z + 1),
          sample_value(x + 0, y + 1, z + 1)
        }};

        const Vec3 cell_min = cube_points[0];
        const Vec3 center = {cell_min.x + half_cell, cell_min.y + half_cell, cell_min.z + half_cell};
        if (evaluate_scene_sdf(scene, center) <= 0.0f)
        {
          ++result.occupied_cells;
        }

        bool has_inside = false;
        bool has_outside = false;
        for (float value : cube_values)
        {
          has_inside = has_inside || value <= 0.0f;
          has_outside = has_outside || value > 0.0f;
        }

        if (!(has_inside && has_outside))
        {
          continue;
        }

        // Splitting each cube into six tetrahedra keeps the implementation small
        // while replacing the blocky voxel shell with interpolated isosurface triangles.
        for (const std::array<int, 4> &tetrahedron : kCubeTetrahedra)
        {
          polygonize_tetrahedron(
            scene,
            {
              cube_points[tetrahedron[0]],
              cube_points[tetrahedron[1]],
              cube_points[tetrahedron[2]],
              cube_points[tetrahedron[3]]
            },
            {
              cube_values[tetrahedron[0]],
              cube_values[tetrahedron[1]],
              cube_values[tetrahedron[2]],
              cube_values[tetrahedron[3]]
            },
            gradient_epsilon,
            result.mesh);
        }
      }
    }
  }

  return result;
}

}  // namespace sdf
