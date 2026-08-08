#include "sdf/bake_surface_pack.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
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

struct CoveredSurfaceTexel
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

float compute_spatial_weight(int dx, int dy, int radius)
{
  const float sigma = std::max(0.75f, static_cast<float>(radius) * 0.75f);
  const float distance_squared = static_cast<float>(dx * dx + dy * dy);
  const float sigma_squared = sigma * sigma;
  return std::exp(-distance_squared / (2.0f * sigma_squared));
}

float compute_normal_weight(const Vec3 &lhs, const Vec3 &rhs)
{
  constexpr float kNormalSigma = 0.15f;
  const float alignment = clampf(dot(lhs, rhs), 0.0f, 1.0f);
  return std::exp(-(1.0f - alignment) / kNormalSigma);
}

std::uint32_t hash_u32(std::uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

float fractf(float value)
{
  return value - std::floor(value);
}

// This is not true blue noise, but it gives a stable low-clumping pattern
// that is much better behaved than plain hash-based white noise in UV space.
float interleaved_gradient_noise(float x, float y)
{
  return fractf(52.9829189f * fractf(0.06711056f * x + 0.00583715f * y));
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

void build_surface_tangent_frame(
  const Vec3 &surface_normal,
  const TriangleTangentBasis &triangle_basis,
  Vec3 *tangent,
  Vec3 *bitangent)
{
  Vec3 frame_tangent = triangle_basis.tangent - surface_normal * dot(surface_normal, triangle_basis.tangent);
  if (length_squared(frame_tangent) <= 1.0e-10f)
  {
    frame_tangent = choose_fallback_tangent(surface_normal);
  }
  else
  {
    frame_tangent = normalized(frame_tangent);
  }

  Vec3 frame_bitangent = cross(surface_normal, frame_tangent) * triangle_basis.handedness;
  if (length_squared(frame_bitangent) <= 1.0e-10f)
  {
    frame_bitangent = normalized(cross(surface_normal, frame_tangent));
  }
  else
  {
    frame_bitangent = normalized(frame_bitangent);
  }

  *tangent = frame_tangent;
  *bitangent = frame_bitangent;
}

float estimate_curvature_signal(
  const SceneDocument &scene,
  const Vec3 &surface_position,
  const Vec3 &surface_normal,
  const Vec3 &tangent,
  const Vec3 &bitangent,
  float surface_epsilon,
  float sample_radius,
  int projection_iterations)
{
  const float radius = std::max(sample_radius, surface_epsilon * 2.0f);
  const std::array<Vec3, 4> directions = {{tangent, -tangent, bitangent, -bitangent}};

  float variation_sum = 0.0f;
  int variation_count = 0;
  for (const Vec3 &direction : directions)
  {
    const Vec3 projected = project_point_to_scene_surface(
      scene,
      surface_position + direction * radius,
      surface_epsilon,
      projection_iterations);
    const Vec3 neighbor_normal = estimate_scene_surface_normal(scene, projected, surface_epsilon);
    const float alignment = clampf(dot(surface_normal, neighbor_normal), -1.0f, 1.0f);
    variation_sum += 1.0f - std::fabs(alignment);
    ++variation_count;
  }

  if (variation_count <= 0)
  {
    return 0.0f;
  }

  const float raw_curvature = variation_sum / static_cast<float>(variation_count);
  return clampf(std::sqrt(clampf(raw_curvature, 0.0f, 1.0f)), 0.0f, 1.0f);
}

float estimate_thickness_signal(
  const SceneDocument &scene,
  const Vec3 &surface_position,
  const Vec3 &surface_normal,
  const Vec3 &tangent,
  const Vec3 &bitangent,
  int texel_x,
  int texel_y,
  std::uint32_t seed,
  float surface_epsilon,
  float max_distance,
  float cone_angle_degrees)
{
  if (max_distance <= 0.0f)
  {
    return 0.0f;
  }

  const auto trace_thickness_along_direction =
    [&](const Vec3 &direction)
    {
      if (length_squared(direction) <= 1.0e-10f)
      {
        return 0.0f;
      }

      const Vec3 inward_direction = normalized(direction);
      float inside_offset = std::max(surface_epsilon * 2.0f, 1.0e-3f);
      Vec3 point = surface_position + inward_direction * inside_offset;
      float signed_distance = evaluate_scene_sdf(scene, point);
      bool found_inside = signed_distance <= 0.0f;

      for (int attempt = 0; !found_inside && attempt < 5; ++attempt)
      {
        inside_offset *= 2.0f;
        if (inside_offset >= max_distance)
        {
          break;
        }

        point = surface_position + inward_direction * inside_offset;
        signed_distance = evaluate_scene_sdf(scene, point);
        found_inside = signed_distance <= 0.0f;
      }

      if (!found_inside)
      {
        return 0.0f;
      }

      const float min_step = std::max(surface_epsilon, max_distance / 512.0f);
      float travel = inside_offset;
      float previous_travel = travel;

      for (int iteration = 0; iteration < 256 && travel < max_distance; ++iteration)
      {
        if (signed_distance > 0.0f)
        {
          break;
        }

        previous_travel = travel;
        const float remaining = max_distance - travel;
        if (remaining <= 0.0f)
        {
          break;
        }

        const float step = std::min(std::max(-signed_distance, min_step), remaining);
        if (step <= 1.0e-6f)
        {
          break;
        }

        travel += step;
        point = surface_position + inward_direction * travel;
        signed_distance = evaluate_scene_sdf(scene, point);
        if (signed_distance <= 0.0f)
        {
          continue;
        }

        float low = previous_travel;
        float high = travel;
        for (int refine = 0; refine < 8; ++refine)
        {
          const float mid = 0.5f * (low + high);
          const float mid_distance = evaluate_scene_sdf(scene, surface_position + inward_direction * mid);
          if (mid_distance <= 0.0f)
          {
            low = mid;
          }
          else
          {
            high = mid;
          }
        }

        return clampf(high / max_distance, 0.0f, 1.0f);
      }

      return 1.0f;
    };

  constexpr float kPi = 3.14159265358979323846f;
  const Vec3 inward = -surface_normal;
  const float clamped_cone_angle_degrees = clampf(cone_angle_degrees, 0.0f, 89.0f);
  const float cos_max_theta = std::cos(clamped_cone_angle_degrees * kPi / 180.0f);
  const float seed_offset_x = static_cast<float>(hash_u32(seed ^ 0x68bc21ebu) & 1023u) / 1024.0f;
  const float seed_offset_y = static_cast<float>(hash_u32(seed ^ 0x02e5be93u) & 1023u) / 1024.0f;
  const float x = static_cast<float>(texel_x);
  const float y = static_cast<float>(texel_y);
  const float u1 = interleaved_gradient_noise(x + 0.5f + seed_offset_x, y + 0.5f + seed_offset_y);
  const float u2 = interleaved_gradient_noise(x + 17.5f + seed_offset_y, y + 47.5f + seed_offset_x);
  const float cos_theta = 1.0f - u1 * (1.0f - cos_max_theta);
  const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
  const float phi = 2.0f * kPi * u2;
  const Vec3 direction = normalized(
    inward * cos_theta +
    tangent * (std::cos(phi) * sin_theta) +
    bitangent * (std::sin(phi) * sin_theta));

  return trace_thickness_along_direction(direction);
}

}  // namespace

bool bake_surface_pack_texture(
  const SceneDocument &scene,
  const Mesh &uv_mesh,
  const Rgb8Image &ao_image,
  const SurfacePackSettings &settings,
  Rgb8Image *image,
  SurfacePackResult *result,
  std::string *error_message,
  const ProgressCallback &progress_callback)
{
  if (image == nullptr)
  {
    return fail(error_message, "surface pack bake requires a non-null image output");
  }

  if (settings.width <= 0 || settings.height <= 0)
  {
    return fail(error_message, "surface pack bake dimensions must be strictly positive");
  }

  if (uv_mesh.vertices.empty() || uv_mesh.triangles.empty())
  {
    return fail(error_message, "surface pack bake requires a non-empty uv mesh");
  }

  if (settings.thickness_max_distance <= 0.0f)
  {
    return fail(error_message, "surface pack thickness max distance must be strictly positive");
  }

  const std::size_t texel_count = static_cast<std::size_t>(settings.width) * static_cast<std::size_t>(settings.height);
  if (ao_image.width != settings.width ||
      ao_image.height != settings.height ||
      ao_image.pixels.size() < texel_count * 3)
  {
    return fail(error_message, "surface pack bake requires an AO image that matches the bake dimensions");
  }

  const float surface_epsilon = settings.surface_epsilon > 0.0f ? settings.surface_epsilon : 0.05f;
  const float curvature_sample_radius =
    settings.curvature_sample_radius > 0.0f ? settings.curvature_sample_radius : std::max(surface_epsilon * 4.0f, 0.2f);

  std::vector<float> curvature_values(texel_count, 0.0f);
  std::vector<float> thickness_values(texel_count, 0.0f);
  std::vector<Vec3> surface_normals(texel_count);
  std::vector<CoveredSurfaceTexel> covered_texels(texel_count);
  std::vector<std::uint8_t> valid(texel_count, 0u);
  std::vector<int> chart_ids(texel_count, -1);
  std::vector<int> source_texels(texel_count, -1);
  std::vector<TriangleTangentBasis> triangle_tangent_bases(uv_mesh.triangles.size());

  detail::ProgressScope coverage_progress(
    progress_callback,
    "Rasterize surface pack coverage",
    static_cast<std::uint64_t>(uv_mesh.triangles.size()));
  std::uint64_t triangles_done = 0;

  for (std::size_t triangle_index = 0; triangle_index < uv_mesh.triangles.size(); ++triangle_index)
  {
    const MeshTriangle &triangle = uv_mesh.triangles[triangle_index];
    if (triangle.i0 >= uv_mesh.vertices.size() ||
        triangle.i1 >= uv_mesh.vertices.size() ||
        triangle.i2 >= uv_mesh.vertices.size())
    {
      return fail(error_message, "surface pack bake received a mesh with out-of-range triangle indices");
    }

    const MeshVertex &v0 = uv_mesh.vertices[triangle.i0];
    const MeshVertex &v1 = uv_mesh.vertices[triangle.i1];
    const MeshVertex &v2 = uv_mesh.vertices[triangle.i2];
    triangle_tangent_bases[triangle_index] = compute_triangle_tangent_basis(v0, v1, v2);

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
          CoveredSurfaceTexel &covered_texel = covered_texels[texel_index];
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
    "Shade surface pack texels",
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
      const CoveredSurfaceTexel &covered_texel = covered_texels[texel_index];
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
      const Vec3 surface_position = project_point_to_scene_surface(
        scene,
        low_poly_position,
        surface_epsilon,
        settings.projection_iterations);
      const Vec3 surface_normal = estimate_scene_surface_normal(scene, surface_position, surface_epsilon);

      Vec3 tangent;
      Vec3 bitangent;
      build_surface_tangent_frame(
        surface_normal,
        triangle_tangent_bases[static_cast<std::size_t>(covered_texel.triangle_index)],
        &tangent,
        &bitangent);

      curvature_values[texel_index] = estimate_curvature_signal(
        scene,
        surface_position,
        surface_normal,
        tangent,
        bitangent,
        surface_epsilon,
        curvature_sample_radius,
        settings.projection_iterations);
      surface_normals[texel_index] = surface_normal;
      chart_ids[texel_index] = triangle.uv_chart_id;
      thickness_values[texel_index] = estimate_thickness_signal(
        scene,
        surface_position,
        surface_normal,
        tangent,
        bitangent,
        x,
        y,
        settings.seed,
        surface_epsilon,
        settings.thickness_max_distance,
        settings.thickness_cone_angle_degrees);
    }

    const int done = shaded_rows.fetch_add(1) + 1;
    shade_progress.update(static_cast<std::uint64_t>(done));
  }

  shade_progress.finish();

  const int thickness_filter_pass_count = std::max(settings.thickness_filter_passes, 0);
  const int thickness_filter_radius = std::max(settings.thickness_filter_radius, 0);
  if (thickness_filter_pass_count > 0 && thickness_filter_radius > 0)
  {
    detail::ProgressScope thickness_filter_progress(
      progress_callback,
      "Filter thickness",
      static_cast<std::uint64_t>(thickness_filter_pass_count) * static_cast<std::uint64_t>(settings.height));

    for (int pass = 0; pass < thickness_filter_pass_count; ++pass)
    {
      std::vector<float> next_thickness = thickness_values;
      std::atomic<int> filter_rows_completed = 0;
      const std::uint64_t pass_base = static_cast<std::uint64_t>(pass) * static_cast<std::uint64_t>(settings.height);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
      for (int y = 0; y < settings.height; ++y)
      {
        for (int x = 0; x < settings.width; ++x)
        {
          const std::size_t texel_index = static_cast<std::size_t>(x + y * settings.width);
          if (valid[texel_index] == 0)
          {
            continue;
          }

          const int center_chart_id = chart_ids[texel_index];
          const Vec3 &center_normal = surface_normals[texel_index];
          float weighted_sum = 0.0f;
          float weight_sum = 0.0f;

          const int min_y = std::max(0, y - thickness_filter_radius);
          const int max_y = std::min(settings.height - 1, y + thickness_filter_radius);
          const int min_x = std::max(0, x - thickness_filter_radius);
          const int max_x = std::min(settings.width - 1, x + thickness_filter_radius);
          for (int sample_y = min_y; sample_y <= max_y; ++sample_y)
          {
            for (int sample_x = min_x; sample_x <= max_x; ++sample_x)
            {
              const std::size_t sample_index = static_cast<std::size_t>(sample_x + sample_y * settings.width);
              if (valid[sample_index] == 0 || chart_ids[sample_index] != center_chart_id)
              {
                continue;
              }

              const int dx = sample_x - x;
              const int dy = sample_y - y;
              const float spatial_weight = compute_spatial_weight(dx, dy, thickness_filter_radius);
              const float normal_weight = compute_normal_weight(center_normal, surface_normals[sample_index]);
              const float weight = spatial_weight * normal_weight;
              weighted_sum += thickness_values[sample_index] * weight;
              weight_sum += weight;
            }
          }

          if (weight_sum > 1.0e-6f)
          {
            next_thickness[texel_index] = weighted_sum / weight_sum;
          }
        }

        const int done = filter_rows_completed.fetch_add(1) + 1;
        thickness_filter_progress.update(pass_base + static_cast<std::uint64_t>(done));
      }

      thickness_values.swap(next_thickness);
    }

    thickness_filter_progress.finish();
  }

  const int dilation_pass_count =
    settings.dilation_passes >= 0
      ? settings.dilation_passes
      : compute_auto_dilation_pass_count(settings.width, settings.height);

  const std::array<int, 8> offsets_x = {{0, -1, 1, 0, -1, 1, -1, 1}};
  const std::array<int, 8> offsets_y = {{-1, 0, 0, 1, -1, -1, 1, 1}};
  for (int pass = 0; pass < std::max(dilation_pass_count, 0); ++pass)
  {
    std::vector<float> next_curvature = curvature_values;
    std::vector<float> next_thickness = thickness_values;
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

        next_curvature[texel_index] = curvature_values[static_cast<std::size_t>(chosen_source)];
        next_thickness[texel_index] = thickness_values[static_cast<std::size_t>(chosen_source)];
        next_valid[texel_index] = 1u;
        next_sources[texel_index] = chosen_source;
        any_change = 1;
      }
    }

    curvature_values.swap(next_curvature);
    thickness_values.swap(next_thickness);
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
    if (valid[texel_index] == 0)
    {
      continue;
    }

    const int source_texel = source_texels[texel_index] >= 0 ? source_texels[texel_index] : static_cast<int>(texel_index);
    const std::size_t ao_index = static_cast<std::size_t>(source_texel) * 3;
    image->pixels[texel_index * 3 + 0] = ao_image.pixels[ao_index + 0];
    image->pixels[texel_index * 3 + 1] = to_byte(curvature_values[texel_index]);
    image->pixels[texel_index * 3 + 2] = to_byte(thickness_values[texel_index]);
  }

  if (result != nullptr)
  {
    result->baked_texels = baked_texels;
    result->covered_texels = covered_texel_count;
    result->dilated_texels = covered_texel_count >= baked_texels ? (covered_texel_count - baked_texels) : 0;
    result->thickness_filter_passes = thickness_filter_pass_count;
    result->thickness_filter_radius = thickness_filter_pass_count > 0 ? thickness_filter_radius : 0;
    result->dilation_passes = dilation_pass_count;
  }

  if (error_message != nullptr)
  {
    error_message->clear();
  }

  return true;
}

}  // namespace sdf
