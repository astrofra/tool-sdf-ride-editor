#include "sdf/bake_normal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "progress_utils.h"
#include "sdf/generator.h"

namespace sdf
{

namespace
{

struct TriangleTangentBasis
{
  Vec3 tangent = {1.0f, 0.0f, 0.0f};
  float handedness = 1.0f;
};

struct CoveredNormalTexel
{
  int triangle_index = -1;
  float coverage_score = -1.0e30f;
  float w0 = 0.0f;
  float w1 = 0.0f;
  float w2 = 0.0f;
};

float cross2d(const Vec2 &lhs, const Vec2 &rhs)
{
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

Vec2 subtract(const Vec2 &lhs, const Vec2 &rhs)
{
  return {lhs.x - rhs.x, lhs.y - rhs.y};
}

bool compute_barycentric(
  const Vec2 &point,
  const Vec2 &a,
  const Vec2 &b,
  const Vec2 &c,
  float *w0,
  float *w1,
  float *w2)
{
  const float area = cross2d(subtract(b, a), subtract(c, a));
  if (std::fabs(area) <= 1.0e-8f)
  {
    return false;
  }

  *w0 = cross2d(subtract(b, point), subtract(c, point)) / area;
  *w1 = cross2d(subtract(c, point), subtract(a, point)) / area;
  *w2 = 1.0f - *w0 - *w1;
  return true;
}

Vec2 to_image_uv(const Vec2 &uv, bool flip_v)
{
  return {uv.x, flip_v ? 1.0f - uv.y : uv.y};
}

bool fail(std::string *error_message, const std::string &message)
{
  if (error_message != nullptr)
  {
    *error_message = message;
  }
  return false;
}

int compute_auto_dilation_pass_count(int width, int height)
{
  const int max_dimension = std::max(width, height);
  return std::clamp(max_dimension / 32, 16, 64);
}

unsigned char to_byte(float value)
{
  return static_cast<unsigned char>(clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

Vec3 choose_fallback_tangent(const Vec3 &normal)
{
  const Vec3 seed = std::fabs(normal.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  return normalized(cross(seed, normal));
}

TriangleTangentBasis compute_triangle_tangent_basis(
  const MeshVertex &v0,
  const MeshVertex &v1,
  const MeshVertex &v2)
{
  const Vec3 edge1 = v1.position - v0.position;
  const Vec3 edge2 = v2.position - v0.position;
  const Vec3 geometric_normal = normalized(cross(edge1, edge2));

  TriangleTangentBasis basis;
  basis.tangent = choose_fallback_tangent(geometric_normal);
  basis.handedness = 1.0f;

  const Vec2 duv1 = subtract(v1.uv0, v0.uv0);
  const Vec2 duv2 = subtract(v2.uv0, v0.uv0);
  const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
  if (std::fabs(determinant) <= 1.0e-8f)
  {
    return basis;
  }

  const float inverse = 1.0f / determinant;
  const Vec3 tangent_raw = (edge1 * duv2.y - edge2 * duv1.y) * inverse;
  const Vec3 bitangent_raw = (edge2 * duv1.x - edge1 * duv2.x) * inverse;
  Vec3 tangent = tangent_raw - geometric_normal * dot(geometric_normal, tangent_raw);
  if (length_squared(tangent) <= 1.0e-10f)
  {
    tangent = basis.tangent;
  }
  else
  {
    tangent = normalized(tangent);
  }

  const Vec3 reference_bitangent = cross(geometric_normal, tangent);
  basis.tangent = tangent;
  basis.handedness = dot(reference_bitangent, bitangent_raw) < 0.0f ? -1.0f : 1.0f;
  return basis;
}

Vec3 project_point_to_scene_surface(
  const SceneDocument &scene,
  Vec3 point,
  float epsilon,
  int iteration_count)
{
  const int steps = std::max(iteration_count, 1);
  const float tolerance = std::max(epsilon * 0.5f, 1.0e-4f);

  for (int iteration = 0; iteration < steps; ++iteration)
  {
    const float signed_distance = evaluate_scene_sdf(scene, point);
    if (std::fabs(signed_distance) <= tolerance)
    {
      break;
    }

    const Vec3 normal = estimate_scene_surface_normal(scene, point, epsilon);
    point -= normal * signed_distance;
  }

  return point;
}

Vec3 encode_tangent_space_normal(
  const Vec3 &world_normal,
  const Vec3 &shading_normal,
  const TriangleTangentBasis &triangle_basis)
{
  Vec3 tangent = triangle_basis.tangent - shading_normal * dot(shading_normal, triangle_basis.tangent);
  if (length_squared(tangent) <= 1.0e-10f)
  {
    tangent = choose_fallback_tangent(shading_normal);
  }
  else
  {
    tangent = normalized(tangent);
  }

  Vec3 bitangent = cross(shading_normal, tangent) * triangle_basis.handedness;
  if (length_squared(bitangent) <= 1.0e-10f)
  {
    bitangent = normalized(cross(shading_normal, tangent));
  }
  else
  {
    bitangent = normalized(bitangent);
  }

  Vec3 tangent_space = {
    dot(world_normal, tangent),
    dot(world_normal, bitangent),
    dot(world_normal, shading_normal)
  };
  if (length_squared(tangent_space) <= 1.0e-10f)
  {
    return {0.0f, 0.0f, 1.0f};
  }

  return normalized(tangent_space);
}

}  // namespace

bool bake_sdf_normal_texture(
  const SceneDocument &scene,
  const Mesh &uv_mesh,
  const NormalBakeSettings &settings,
  Rgb8Image *image,
  NormalBakeResult *result,
  std::string *error_message,
  const ProgressCallback &progress_callback)
{
  if (image == nullptr)
  {
    return fail(error_message, "normal bake requires a non-null image output");
  }

  if (settings.width <= 0 || settings.height <= 0)
  {
    return fail(error_message, "normal bake dimensions must be strictly positive");
  }

  if (uv_mesh.vertices.empty() || uv_mesh.triangles.empty())
  {
    return fail(error_message, "normal bake requires a non-empty uv mesh");
  }

  const float surface_epsilon = settings.surface_epsilon > 0.0f ? settings.surface_epsilon : 0.05f;
  const std::size_t texel_count = static_cast<std::size_t>(settings.width) * static_cast<std::size_t>(settings.height);
  std::vector<Vec3> normal_values(texel_count, {0.0f, 0.0f, 1.0f});
  std::vector<CoveredNormalTexel> covered_texels(texel_count);
  std::vector<std::uint8_t> valid(texel_count, 0u);
  std::vector<int> source_texels(texel_count, -1);
  std::vector<TriangleTangentBasis> triangle_tangent_bases(uv_mesh.triangles.size());

  detail::ProgressScope coverage_progress(
    progress_callback,
    "Rasterize normal coverage",
    static_cast<std::uint64_t>(uv_mesh.triangles.size()));
  std::uint64_t triangles_done = 0;

  for (std::size_t triangle_index = 0; triangle_index < uv_mesh.triangles.size(); ++triangle_index)
  {
    const MeshTriangle &triangle = uv_mesh.triangles[triangle_index];
    if (triangle.i0 >= uv_mesh.vertices.size() ||
        triangle.i1 >= uv_mesh.vertices.size() ||
        triangle.i2 >= uv_mesh.vertices.size())
    {
      return fail(error_message, "normal bake received a mesh with out-of-range triangle indices");
    }

    const MeshVertex &v0 = uv_mesh.vertices[triangle.i0];
    const MeshVertex &v1 = uv_mesh.vertices[triangle.i1];
    const MeshVertex &v2 = uv_mesh.vertices[triangle.i2];
    const TriangleTangentBasis tangent_basis = compute_triangle_tangent_basis(v0, v1, v2);
    triangle_tangent_bases[triangle_index] = tangent_basis;

    const Vec2 t0 = to_image_uv(v0.uv0, settings.flip_v);
    const Vec2 t1 = to_image_uv(v1.uv0, settings.flip_v);
    const Vec2 t2 = to_image_uv(v2.uv0, settings.flip_v);

    const float min_u = std::min(t0.x, std::min(t1.x, t2.x));
    const float max_u = std::max(t0.x, std::max(t1.x, t2.x));
    const float min_v = std::min(t0.y, std::min(t1.y, t2.y));
    const float max_v = std::max(t0.y, std::max(t1.y, t2.y));

    const int x0 = std::max(0, static_cast<int>(std::ceil(min_u * settings.width - 0.5f)));
    const int x1 = std::min(settings.width - 1, static_cast<int>(std::floor(max_u * settings.width - 0.5f)));
    const int y0 = std::max(0, static_cast<int>(std::ceil(min_v * settings.height - 0.5f)));
    const int y1 = std::min(settings.height - 1, static_cast<int>(std::floor(max_v * settings.height - 0.5f)));
    if (x0 <= x1 && y0 <= y1)
    {
      for (int y = y0; y <= y1; ++y)
      {
        const float py = (static_cast<float>(y) + 0.5f) / static_cast<float>(settings.height);
        for (int x = x0; x <= x1; ++x)
        {
          const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(settings.width);

          float w0 = 0.0f;
          float w1 = 0.0f;
          float w2 = 0.0f;
          if (!compute_barycentric({px, py}, t0, t1, t2, &w0, &w1, &w2))
          {
            continue;
          }

          constexpr float kCoverageEpsilon = 1.0e-5f;
          if (w0 < -kCoverageEpsilon || w1 < -kCoverageEpsilon || w2 < -kCoverageEpsilon)
          {
            continue;
          }

          const std::size_t texel_index = static_cast<std::size_t>(x + y * settings.width);
          const float coverage_score = std::min(w0, std::min(w1, w2));
          CoveredNormalTexel &covered_texel = covered_texels[texel_index];
          if (covered_texel.triangle_index >= 0 && coverage_score <= covered_texel.coverage_score)
          {
            continue;
          }

          covered_texel.triangle_index = static_cast<int>(triangle_index);
          covered_texel.coverage_score = coverage_score;
          covered_texel.w0 = w0;
          covered_texel.w1 = w1;
          covered_texel.w2 = w2;
        }
      }
    }

    ++triangles_done;
    coverage_progress.update(triangles_done);
  }

  coverage_progress.finish();

  std::size_t baked_texels = 0;
  for (std::size_t texel_index = 0; texel_index < texel_count; ++texel_index)
  {
    if (covered_texels[texel_index].triangle_index < 0)
    {
      continue;
    }

    valid[texel_index] = 1u;
    source_texels[texel_index] = static_cast<int>(texel_index);
    ++baked_texels;
  }

  detail::ProgressScope shade_progress(
    progress_callback,
    "Shade normal texels",
    static_cast<std::uint64_t>(settings.height));
  std::atomic<int> shaded_rows = 0;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
  for (int y = 0; y < settings.height; ++y)
  {
    for (int x = 0; x < settings.width; ++x)
    {
      const std::size_t texel_index = static_cast<std::size_t>(x + y * settings.width);
      const CoveredNormalTexel &covered_texel = covered_texels[texel_index];
      if (covered_texel.triangle_index < 0)
      {
        continue;
      }

      const MeshTriangle &triangle = uv_mesh.triangles[static_cast<std::size_t>(covered_texel.triangle_index)];
      const MeshVertex &v0 = uv_mesh.vertices[triangle.i0];
      const MeshVertex &v1 = uv_mesh.vertices[triangle.i1];
      const MeshVertex &v2 = uv_mesh.vertices[triangle.i2];
      const float w0 = covered_texel.w0;
      const float w1 = covered_texel.w1;
      const float w2 = covered_texel.w2;
      const Vec3 low_poly_position = v0.position * w0 + v1.position * w1 + v2.position * w2;
      const Vec3 shading_normal = normalized(v0.normal * w0 + v1.normal * w1 + v2.normal * w2);
      const Vec3 surface_position = project_point_to_scene_surface(
        scene,
        low_poly_position,
        surface_epsilon,
        settings.projection_iterations);
      const Vec3 sdf_normal = estimate_scene_surface_normal(scene, surface_position, surface_epsilon);
      normal_values[texel_index] = encode_tangent_space_normal(
        sdf_normal,
        shading_normal,
        triangle_tangent_bases[static_cast<std::size_t>(covered_texel.triangle_index)]);
    }

    const int done = shaded_rows.fetch_add(1) + 1;
    shade_progress.update(static_cast<std::uint64_t>(done));
  }

  shade_progress.finish();

  const int dilation_pass_count =
    settings.dilation_passes >= 0
      ? settings.dilation_passes
      : compute_auto_dilation_pass_count(settings.width, settings.height);

  const std::array<int, 8> offsets_x = {{0, -1, 1, 0, -1, 1, -1, 1}};
  const std::array<int, 8> offsets_y = {{-1, 0, 0, 1, -1, -1, 1, 1}};
  for (int pass = 0; pass < std::max(dilation_pass_count, 0); ++pass)
  {
    std::vector<Vec3> next_normals = normal_values;
    std::vector<std::uint8_t> next_valid = valid;
    std::vector<int> next_sources = source_texels;
    int any_change = 0;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(|:any_change)
#endif
    for (int y = 0; y < settings.height; ++y)
    {
      for (int x = 0; x < settings.width; ++x)
      {
        const std::size_t texel_index = static_cast<std::size_t>(x + y * settings.width);
        if (valid[texel_index] != 0)
        {
          continue;
        }

        int chosen_source = -1;
        for (std::size_t offset_index = 0; offset_index < offsets_x.size(); ++offset_index)
        {
          const int nx = x + offsets_x[offset_index];
          const int ny = y + offsets_y[offset_index];
          if (nx < 0 || nx >= settings.width || ny < 0 || ny >= settings.height)
          {
            continue;
          }

          const std::size_t neighbor_index = static_cast<std::size_t>(nx + ny * settings.width);
          if (valid[neighbor_index] == 0)
          {
            continue;
          }

          chosen_source = source_texels[neighbor_index] >= 0
            ? source_texels[neighbor_index]
            : static_cast<int>(neighbor_index);
          break;
        }

        if (chosen_source < 0)
        {
          continue;
        }

        next_normals[texel_index] = normal_values[static_cast<std::size_t>(chosen_source)];
        next_valid[texel_index] = 1u;
        next_sources[texel_index] = chosen_source;
        any_change = 1;
      }
    }

    normal_values.swap(next_normals);
    valid.swap(next_valid);
    source_texels.swap(next_sources);
    if (any_change == 0)
    {
      break;
    }
  }

  std::size_t covered_texel_count = 0;
  for (std::uint8_t value : valid)
  {
    covered_texel_count += value != 0 ? 1u : 0u;
  }

  image->width = settings.width;
  image->height = settings.height;
  image->pixels.assign(texel_count * 3, 0u);

  for (std::size_t texel_index = 0; texel_index < texel_count; ++texel_index)
  {
    const Vec3 tangent_space = valid[texel_index] != 0 ? normal_values[texel_index] : Vec3{0.0f, 0.0f, 1.0f};
    image->pixels[texel_index * 3 + 0] = to_byte(tangent_space.x * 0.5f + 0.5f);
    image->pixels[texel_index * 3 + 1] = to_byte(tangent_space.y * 0.5f + 0.5f);
    image->pixels[texel_index * 3 + 2] = to_byte(tangent_space.z * 0.5f + 0.5f);
  }

  if (result != nullptr)
  {
    result->baked_texels = baked_texels;
    result->covered_texels = covered_texel_count;
    result->dilated_texels = covered_texel_count >= baked_texels ? (covered_texel_count - baked_texels) : 0;
    result->dilation_passes = dilation_pass_count;
  }

  if (error_message != nullptr)
  {
    error_message->clear();
  }

  return true;
}

}  // namespace sdf
