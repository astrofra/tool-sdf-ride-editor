#include "sdf/generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "progress_utils.h"

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

struct HermiteSample
{
  Vec3 position;
  Vec3 normal;
};

struct DualCellVertex
{
  bool active = false;
  std::uint32_t vertex_index = std::numeric_limits<std::uint32_t>::max();
};

constexpr std::array<std::array<int, 4>, 6> kCubeTetrahedra = {{
  {{0, 5, 1, 6}},
  {{0, 1, 2, 6}},
  {{0, 2, 3, 6}},
  {{0, 3, 7, 6}},
  {{0, 7, 4, 6}},
  {{0, 4, 5, 6}}
}};

constexpr std::array<std::array<int, 2>, 12> kCubeEdges = {{
  {{0, 1}},
  {{1, 2}},
  {{2, 3}},
  {{3, 0}},
  {{4, 5}},
  {{5, 6}},
  {{6, 7}},
  {{7, 4}},
  {{0, 4}},
  {{1, 5}},
  {{2, 6}},
  {{3, 7}}
}};

std::size_t grid_index(const GridDimensions &dims, int x, int y, int z)
{
  return static_cast<std::size_t>((z * dims.ny + y) * dims.nx + x);
}

bool is_inside(float value)
{
  return value <= 0.0f;
}

bool has_sign_change(float a, float b)
{
  return is_inside(a) != is_inside(b);
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

void append_indexed_triangle(Mesh &mesh, std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
{
  if (i0 == i1 || i1 == i2 || i0 == i2)
  {
    return;
  }

  if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
  {
    return;
  }

  const Vec3 &p0 = mesh.vertices[i0].position;
  const Vec3 &p1 = mesh.vertices[i1].position;
  const Vec3 &p2 = mesh.vertices[i2].position;
  if (length_squared(cross(p1 - p0, p2 - p0)) <= 1.0e-10f)
  {
    return;
  }

  mesh.triangles.push_back({i0, i1, i2, 0});
}

void append_oriented_quad(
  Mesh &mesh,
  const std::array<std::uint32_t, 4> &quad,
  const Vec3 &desired_normal)
{
  if (quad[0] == quad[1] || quad[1] == quad[2] || quad[2] == quad[3] || quad[3] == quad[0] ||
      quad[0] == quad[2] || quad[1] == quad[3])
  {
    return;
  }

  for (std::uint32_t index : quad)
  {
    if (index >= mesh.vertices.size())
    {
      return;
    }
  }

  const Vec3 &p0 = mesh.vertices[quad[0]].position;
  const Vec3 &p1 = mesh.vertices[quad[1]].position;
  const Vec3 &p2 = mesh.vertices[quad[2]].position;
  const Vec3 &p3 = mesh.vertices[quad[3]].position;
  const Vec3 face_normal = cross(p1 - p0, p2 - p0) + cross(p2 - p0, p3 - p0);
  if (length_squared(face_normal) <= 1.0e-10f)
  {
    return;
  }

  std::array<std::uint32_t, 4> ordered = quad;
  if (dot(face_normal, desired_normal) < 0.0f)
  {
    ordered = {{quad[0], quad[3], quad[2], quad[1]}};
  }

  append_indexed_triangle(mesh, ordered[0], ordered[1], ordered[2]);
  append_indexed_triangle(mesh, ordered[0], ordered[2], ordered[3]);
}

bool solve_linear_system_3x3(std::array<std::array<float, 4>, 3> matrix, Vec3 *solution)
{
  if (solution == nullptr)
  {
    return false;
  }

  for (int pivot = 0; pivot < 3; ++pivot)
  {
    int best_row = pivot;
    float best_abs = std::fabs(matrix[pivot][pivot]);
    for (int row = pivot + 1; row < 3; ++row)
    {
      const float candidate_abs = std::fabs(matrix[row][pivot]);
      if (candidate_abs > best_abs)
      {
        best_abs = candidate_abs;
        best_row = row;
      }
    }

    if (best_abs <= 1.0e-8f)
    {
      return false;
    }

    if (best_row != pivot)
    {
      std::swap(matrix[pivot], matrix[best_row]);
    }

    const float pivot_value = matrix[pivot][pivot];
    for (int column = pivot; column < 4; ++column)
    {
      matrix[pivot][column] /= pivot_value;
    }

    for (int row = 0; row < 3; ++row)
    {
      if (row == pivot)
      {
        continue;
      }

      const float factor = matrix[row][pivot];
      if (std::fabs(factor) <= 1.0e-8f)
      {
        continue;
      }

      for (int column = pivot; column < 4; ++column)
      {
        matrix[row][column] -= factor * matrix[pivot][column];
      }
    }
  }

  solution->x = matrix[0][3];
  solution->y = matrix[1][3];
  solution->z = matrix[2][3];
  return true;
}

Vec3 solve_dual_vertex_position(
  const std::vector<HermiteSample> &samples,
  const Vec3 &cell_min,
  const Vec3 &cell_max)
{
  if (samples.empty())
  {
    return (cell_min + cell_max) * 0.5f;
  }

  Vec3 centroid = {0.0f, 0.0f, 0.0f};
  for (const HermiteSample &sample : samples)
  {
    centroid += sample.position;
  }
  centroid *= 1.0f / static_cast<float>(samples.size());

  float ata00 = 0.0f;
  float ata01 = 0.0f;
  float ata02 = 0.0f;
  float ata11 = 0.0f;
  float ata12 = 0.0f;
  float ata22 = 0.0f;
  float atb0 = 0.0f;
  float atb1 = 0.0f;
  float atb2 = 0.0f;

  for (const HermiteSample &sample : samples)
  {
    const Vec3 normal = normalized(sample.normal);
    const float distance = dot(normal, sample.position);
    ata00 += normal.x * normal.x;
    ata01 += normal.x * normal.y;
    ata02 += normal.x * normal.z;
    ata11 += normal.y * normal.y;
    ata12 += normal.y * normal.z;
    ata22 += normal.z * normal.z;
    atb0 += normal.x * distance;
    atb1 += normal.y * distance;
    atb2 += normal.z * distance;
  }

  const float regularization = std::max(1.0e-4f, 1.0e-3f * static_cast<float>(samples.size()));
  ata00 += regularization;
  ata11 += regularization;
  ata22 += regularization;
  atb0 += regularization * centroid.x;
  atb1 += regularization * centroid.y;
  atb2 += regularization * centroid.z;

  Vec3 solution = centroid;
  if (!solve_linear_system_3x3(
        {{
          {{ata00, ata01, ata02, atb0}},
          {{ata01, ata11, ata12, atb1}},
          {{ata02, ata12, ata22, atb2}}
        }},
        &solution))
  {
    solution = centroid;
  }

  solution.x = clampf(solution.x, cell_min.x, cell_max.x);
  solution.y = clampf(solution.y, cell_min.y, cell_max.y);
  solution.z = clampf(solution.z, cell_min.z, cell_max.z);
  return solution;
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
    if (is_inside(values[index]))
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

template <typename SampleValueFn, typename GridPointFn>
void build_marching_tetrahedra_mesh(
  const SceneDocument &scene,
  const BuildSettings &settings,
  const GridDimensions &cell_dims,
  const SampleValueFn &sample_value,
  const GridPointFn &grid_point_position,
  SceneBuildResult *result,
  const ProgressCallback &progress_callback)
{
  const float half_cell = settings.cell_size * 0.5f;
  const float gradient_epsilon = std::max(settings.cell_size * 0.25f, 1.0e-3f);
  detail::ProgressScope polygonize_progress(
    progress_callback,
    "Polygonize cells",
    static_cast<std::uint64_t>(cell_dims.nz) * static_cast<std::uint64_t>(cell_dims.ny));
  std::uint64_t polygonized_rows = 0;

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
          ++result->occupied_cells;
        }

        bool has_inside = false;
        bool has_outside = false;
        for (float value : cube_values)
        {
          has_inside = has_inside || is_inside(value);
          has_outside = has_outside || !is_inside(value);
        }

        if (!(has_inside && has_outside))
        {
          continue;
        }

        for (const std::array<int, 4> &tetrahedron : kCubeTetrahedra)
        {
          polygonize_tetrahedron(
            scene,
            {{
              cube_points[tetrahedron[0]],
              cube_points[tetrahedron[1]],
              cube_points[tetrahedron[2]],
              cube_points[tetrahedron[3]]
            }},
            {{
              cube_values[tetrahedron[0]],
              cube_values[tetrahedron[1]],
              cube_values[tetrahedron[2]],
              cube_values[tetrahedron[3]]
            }},
            gradient_epsilon,
            result->mesh);
        }
      }

      ++polygonized_rows;
      polygonize_progress.update(polygonized_rows);
    }
  }

  polygonize_progress.finish();
}

template <typename SampleValueFn, typename GridPointFn>
void build_dual_contouring_mesh(
  const SceneDocument &scene,
  const BuildSettings &settings,
  const GridDimensions &cell_dims,
  const SampleValueFn &sample_value,
  const GridPointFn &grid_point_position,
  SceneBuildResult *result,
  const ProgressCallback &progress_callback)
{
  const float half_cell = settings.cell_size * 0.5f;
  const float gradient_epsilon = std::max(settings.cell_size * 0.25f, 1.0e-3f);
  std::vector<DualCellVertex> cell_vertices(
    static_cast<std::size_t>(cell_dims.nx) * static_cast<std::size_t>(cell_dims.ny) * static_cast<std::size_t>(cell_dims.nz));

  detail::ProgressScope cell_vertex_progress(
    progress_callback,
    "Build dual cell vertices",
    static_cast<std::uint64_t>(cell_dims.nz) * static_cast<std::uint64_t>(cell_dims.ny));
  std::uint64_t processed_rows = 0;

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
        const Vec3 cell_max = cube_points[6];
        const Vec3 center = {cell_min.x + half_cell, cell_min.y + half_cell, cell_min.z + half_cell};
        if (evaluate_scene_sdf(scene, center) <= 0.0f)
        {
          ++result->occupied_cells;
        }

        bool has_inside = false;
        bool has_outside = false;
        std::vector<HermiteSample> samples;
        samples.reserve(kCubeEdges.size());
        for (float value : cube_values)
        {
          has_inside = has_inside || is_inside(value);
          has_outside = has_outside || !is_inside(value);
        }

        if (!(has_inside && has_outside))
        {
          continue;
        }

        Vec3 normal_sum = {0.0f, 0.0f, 0.0f};
        for (const std::array<int, 2> &edge : kCubeEdges)
        {
          const int a = edge[0];
          const int b = edge[1];
          if (!has_sign_change(cube_values[a], cube_values[b]))
          {
            continue;
          }

          const SurfaceVertex intersection = interpolate_surface_vertex(
            scene,
            cube_points[a],
            cube_values[a],
            cube_points[b],
            cube_values[b],
            gradient_epsilon);
          samples.push_back({intersection.position, intersection.normal});
          normal_sum += intersection.normal;
        }

        if (samples.empty())
        {
          continue;
        }

        const Vec3 position = solve_dual_vertex_position(samples, cell_min, cell_max);
        const Vec3 normal = length_squared(normal_sum) > 1.0e-8f
          ? normalized(normal_sum)
          : estimate_surface_normal(scene, position, gradient_epsilon);
        const std::uint32_t vertex_index = static_cast<std::uint32_t>(result->mesh.vertices.size());
        result->mesh.vertices.push_back({position, normal, compute_default_uv(position, normal)});

        DualCellVertex &cell_vertex = cell_vertices[grid_index(cell_dims, x, y, z)];
        cell_vertex.active = true;
        cell_vertex.vertex_index = vertex_index;
      }

      ++processed_rows;
      cell_vertex_progress.update(processed_rows);
    }
  }

  cell_vertex_progress.finish();

  auto active_cell_vertex_index = [&](int x, int y, int z) -> std::uint32_t
  {
    if (x < 0 || y < 0 || z < 0 || x >= cell_dims.nx || y >= cell_dims.ny || z >= cell_dims.nz)
    {
      return std::numeric_limits<std::uint32_t>::max();
    }

    const DualCellVertex &cell_vertex = cell_vertices[grid_index(cell_dims, x, y, z)];
    return cell_vertex.active ? cell_vertex.vertex_index : std::numeric_limits<std::uint32_t>::max();
  };

  const std::uint64_t connect_total =
    static_cast<std::uint64_t>(std::max(cell_dims.nz - 1, 0)) * static_cast<std::uint64_t>(std::max(cell_dims.ny - 1, 0)) +
    static_cast<std::uint64_t>(std::max(cell_dims.nz - 1, 0)) * static_cast<std::uint64_t>(std::max(cell_dims.nx - 1, 0)) +
    static_cast<std::uint64_t>(std::max(cell_dims.ny - 1, 0)) * static_cast<std::uint64_t>(std::max(cell_dims.nx - 1, 0));
  detail::ProgressScope connect_progress(
    progress_callback,
    "Connect dual faces",
    std::max<std::uint64_t>(connect_total, 1u));
  std::uint64_t connected_rows = 0;

  for (int z = 1; z < cell_dims.nz; ++z)
  {
    for (int y = 1; y < cell_dims.ny; ++y)
    {
      for (int x = 0; x < cell_dims.nx; ++x)
      {
        const float value_a = sample_value(x, y, z);
        const float value_b = sample_value(x + 1, y, z);
        if (!has_sign_change(value_a, value_b))
        {
          continue;
        }

        const std::array<std::uint32_t, 4> quad = {{
          active_cell_vertex_index(x, y - 1, z - 1),
          active_cell_vertex_index(x, y, z - 1),
          active_cell_vertex_index(x, y, z),
          active_cell_vertex_index(x, y - 1, z)
        }};
        if (std::find(quad.begin(), quad.end(), std::numeric_limits<std::uint32_t>::max()) != quad.end())
        {
          continue;
        }

        const SurfaceVertex edge_vertex = interpolate_surface_vertex(
          scene,
          grid_point_position(x, y, z),
          value_a,
          grid_point_position(x + 1, y, z),
          value_b,
          gradient_epsilon);
        append_oriented_quad(result->mesh, quad, edge_vertex.normal);
      }

      ++connected_rows;
      connect_progress.update(connected_rows);
    }
  }

  for (int z = 1; z < cell_dims.nz; ++z)
  {
    for (int x = 1; x < cell_dims.nx; ++x)
    {
      for (int y = 0; y < cell_dims.ny; ++y)
      {
        const float value_a = sample_value(x, y, z);
        const float value_b = sample_value(x, y + 1, z);
        if (!has_sign_change(value_a, value_b))
        {
          continue;
        }

        const std::array<std::uint32_t, 4> quad = {{
          active_cell_vertex_index(x - 1, y, z - 1),
          active_cell_vertex_index(x, y, z - 1),
          active_cell_vertex_index(x, y, z),
          active_cell_vertex_index(x - 1, y, z)
        }};
        if (std::find(quad.begin(), quad.end(), std::numeric_limits<std::uint32_t>::max()) != quad.end())
        {
          continue;
        }

        const SurfaceVertex edge_vertex = interpolate_surface_vertex(
          scene,
          grid_point_position(x, y, z),
          value_a,
          grid_point_position(x, y + 1, z),
          value_b,
          gradient_epsilon);
        append_oriented_quad(result->mesh, quad, edge_vertex.normal);
      }

      ++connected_rows;
      connect_progress.update(connected_rows);
    }
  }

  for (int y = 1; y < cell_dims.ny; ++y)
  {
    for (int x = 1; x < cell_dims.nx; ++x)
    {
      for (int z = 0; z < cell_dims.nz; ++z)
      {
        const float value_a = sample_value(x, y, z);
        const float value_b = sample_value(x, y, z + 1);
        if (!has_sign_change(value_a, value_b))
        {
          continue;
        }

        const std::array<std::uint32_t, 4> quad = {{
          active_cell_vertex_index(x - 1, y - 1, z),
          active_cell_vertex_index(x, y - 1, z),
          active_cell_vertex_index(x, y, z),
          active_cell_vertex_index(x - 1, y, z)
        }};
        if (std::find(quad.begin(), quad.end(), std::numeric_limits<std::uint32_t>::max()) != quad.end())
        {
          continue;
        }

        const SurfaceVertex edge_vertex = interpolate_surface_vertex(
          scene,
          grid_point_position(x, y, z),
          value_a,
          grid_point_position(x, y, z + 1),
          value_b,
          gradient_epsilon);
        append_oriented_quad(result->mesh, quad, edge_vertex.normal);
      }

      ++connected_rows;
      connect_progress.update(connected_rows);
    }
  }

  connect_progress.finish();
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

SceneBuildResult build_scene_mesh(
  const SceneDocument &scene,
  const BuildSettings &settings,
  const ProgressCallback &progress_callback)
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
  detail::ProgressScope sample_progress(
    progress_callback,
    "Sample SDF field",
    static_cast<std::uint64_t>(point_dims.nz) * static_cast<std::uint64_t>(point_dims.ny));
  std::uint64_t sampled_rows = 0;

  for (int z = 0; z < point_dims.nz; ++z)
  {
    for (int y = 0; y < point_dims.ny; ++y)
    {
      for (int x = 0; x < point_dims.nx; ++x)
      {
        sdf_samples[grid_index(point_dims, x, y, z)] = evaluate_scene_sdf(scene, grid_point_position(x, y, z));
      }

      ++sampled_rows;
      sample_progress.update(sampled_rows);
    }
  }

  sample_progress.finish();

  auto sample_value = [&](int x, int y, int z) -> float
  {
    return sdf_samples[grid_index(point_dims, x, y, z)];
  };

  SceneBuildResult result;
  result.sampled_cells = static_cast<std::size_t>(cell_dims.nx) * cell_dims.ny * cell_dims.nz;

  switch (settings.meshing_mode)
  {
  case MeshingMode::MarchingTetrahedra:
    build_marching_tetrahedra_mesh(
      scene,
      settings,
      cell_dims,
      sample_value,
      grid_point_position,
      &result,
      progress_callback);
    break;

  case MeshingMode::DualContouring:
    build_dual_contouring_mesh(
      scene,
      settings,
      cell_dims,
      sample_value,
      grid_point_position,
      &result,
      progress_callback);
    break;

  default:
    throw std::invalid_argument("unsupported meshing_mode");
  }

  return result;
}

}  // namespace sdf
