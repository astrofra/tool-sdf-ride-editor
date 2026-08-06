#include "sdf/bake_ao.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sdf
{

namespace
{

class Rng
{
public:
  explicit Rng(std::uint32_t seed)
    : state_(seed != 0 ? seed : 0x12345678u)
  {
  }

  std::uint32_t next_u32()
  {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }

  float next_float()
  {
    const std::uint32_t bits = next_u32() >> 8;
    return static_cast<float>(bits) / static_cast<float>(0x01000000u);
  }

private:
  std::uint32_t state_;
};

float cross2d(const Vec2 &lhs, const Vec2 &rhs)
{
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

Vec2 subtract(const Vec2 &lhs, const Vec2 &rhs)
{
  return {lhs.x - rhs.x, lhs.y - rhs.y};
}

float minimum3(float a, float b, float c)
{
  return std::min(a, std::min(b, c));
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

Vec3 sample_cosine_hemisphere(const Vec3 &normal, Rng *rng)
{
  constexpr float kPi = 3.14159265358979323846f;

  const float u1 = rng->next_float();
  const float u2 = rng->next_float();
  const float radius = std::sqrt(u1);
  const float angle = 2.0f * kPi * u2;
  const float x = radius * std::cos(angle);
  const float y = radius * std::sin(angle);
  const float z = std::sqrt(std::max(0.0f, 1.0f - u1));

  const Vec3 tangent_seed = std::fabs(normal.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  const Vec3 tangent = normalized(cross(tangent_seed, normal));
  const Vec3 bitangent = cross(normal, tangent);
  return normalized(tangent * x + bitangent * y + normal * z);
}

float estimate_ambient_occlusion(
  const RayScene &scene,
  const Vec3 &position,
  const Vec3 &shading_normal,
  int sample_count,
  float max_distance,
  std::uint32_t seed)
{
  if (sample_count <= 0 || max_distance <= 0.0f)
  {
    return 1.0f;
  }

  const Vec3 scene_extent = scene.bounds.max - scene.bounds.min;
  const float scene_diagonal = length(scene_extent);
  const float bias = std::max(1.0e-3f, scene_diagonal * 1.0e-4f);

  Rng rng(seed);
  int unoccluded = 0;
  for (int sample_index = 0; sample_index < sample_count; ++sample_index)
  {
    Ray ray;
    ray.origin = position + shading_normal * bias;
    ray.direction = sample_cosine_hemisphere(shading_normal, &rng);
    if (!is_occluded(scene, ray, max_distance))
    {
      ++unoccluded;
    }
  }

  return static_cast<float>(unoccluded) / static_cast<float>(sample_count);
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

unsigned char to_byte(float value)
{
  return static_cast<unsigned char>(clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool fail(std::string *error_message, const std::string &message)
{
  if (error_message != nullptr)
  {
    *error_message = message;
  }
  return false;
}

}  // namespace

bool bake_ambient_occlusion_texture(
  const Mesh &uv_mesh,
  const RayScene &ray_scene,
  const AoBakeSettings &settings,
  Rgb8Image *image,
  AoBakeResult *result,
  std::string *error_message)
{
  if (image == nullptr)
  {
    return fail(error_message, "ao bake requires a non-null image output");
  }

  if (settings.width <= 0 || settings.height <= 0)
  {
    return fail(error_message, "ao bake dimensions must be strictly positive");
  }

  if (uv_mesh.vertices.empty() || uv_mesh.triangles.empty())
  {
    return fail(error_message, "ao bake requires a non-empty uv mesh");
  }

  if (ray_scene.triangles.empty())
  {
    return fail(error_message, "ao bake requires a non-empty ray scene");
  }

  const std::size_t texel_count = static_cast<std::size_t>(settings.width) * static_cast<std::size_t>(settings.height);
  std::vector<float> ao_values(texel_count, 0.0f);
  std::vector<float> coverage_scores(texel_count, -1.0e30f);
  std::vector<std::uint8_t> valid(texel_count, 0u);

  for (std::size_t triangle_index = 0; triangle_index < uv_mesh.triangles.size(); ++triangle_index)
  {
    const MeshTriangle &triangle = uv_mesh.triangles[triangle_index];
    if (triangle.i0 >= uv_mesh.vertices.size() ||
        triangle.i1 >= uv_mesh.vertices.size() ||
        triangle.i2 >= uv_mesh.vertices.size())
    {
      return fail(error_message, "ao bake received a mesh with out-of-range triangle indices");
    }

    const MeshVertex &v0 = uv_mesh.vertices[triangle.i0];
    const MeshVertex &v1 = uv_mesh.vertices[triangle.i1];
    const MeshVertex &v2 = uv_mesh.vertices[triangle.i2];

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

    if (x0 > x1 || y0 > y1)
    {
      continue;
    }

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
        const float coverage_score = minimum3(w0, w1, w2);
        if (valid[texel_index] != 0 && coverage_score <= coverage_scores[texel_index])
        {
          continue;
        }

        const Vec3 position = v0.position * w0 + v1.position * w1 + v2.position * w2;
        const Vec3 shading_normal = normalized(v0.normal * w0 + v1.normal * w1 + v2.normal * w2);
        const std::uint32_t texel_seed = hash_u32(
          settings.seed ^
          static_cast<std::uint32_t>(x * 1973) ^
          static_cast<std::uint32_t>(y * 9277) ^
          static_cast<std::uint32_t>(triangle_index * 26699));
        const float ao = estimate_ambient_occlusion(
          ray_scene,
          position,
          shading_normal,
          settings.ao_samples,
          settings.ao_max_distance,
          texel_seed);

        ao_values[texel_index] = ao;
        coverage_scores[texel_index] = coverage_score;
        valid[texel_index] = 1u;
      }
    }
  }

  std::size_t baked_texels = 0;
  for (std::uint8_t value : valid)
  {
    baked_texels += value != 0 ? 1u : 0u;
  }

  const std::array<int, 8> offsets_x = {{-1, 0, 1, -1, 1, -1, 0, 1}};
  const std::array<int, 8> offsets_y = {{-1, -1, -1, 0, 0, 1, 1, 1}};
  for (int pass = 0; pass < std::max(settings.dilation_passes, 0); ++pass)
  {
    std::vector<float> next_ao = ao_values;
    std::vector<std::uint8_t> next_valid = valid;
    bool any_change = false;

    for (int y = 0; y < settings.height; ++y)
    {
      for (int x = 0; x < settings.width; ++x)
      {
        const std::size_t texel_index = static_cast<std::size_t>(x + y * settings.width);
        if (valid[texel_index] != 0)
        {
          continue;
        }

        float sum = 0.0f;
        int count = 0;
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

          sum += ao_values[neighbor_index];
          ++count;
        }

        if (count <= 0)
        {
          continue;
        }

        next_ao[texel_index] = sum / static_cast<float>(count);
        next_valid[texel_index] = 1u;
        any_change = true;
      }
    }

    ao_values.swap(next_ao);
    valid.swap(next_valid);
    if (!any_change)
    {
      break;
    }
  }

  std::size_t covered_texels = 0;
  for (std::uint8_t value : valid)
  {
    covered_texels += value != 0 ? 1u : 0u;
  }

  image->width = settings.width;
  image->height = settings.height;
  image->pixels.assign(texel_count * 3, 0u);

  for (std::size_t texel_index = 0; texel_index < texel_count; ++texel_index)
  {
    const unsigned char gray = valid[texel_index] != 0 ? to_byte(ao_values[texel_index]) : 0u;
    image->pixels[texel_index * 3 + 0] = gray;
    image->pixels[texel_index * 3 + 1] = gray;
    image->pixels[texel_index * 3 + 2] = gray;
  }

  if (result != nullptr)
  {
    result->baked_texels = baked_texels;
    result->covered_texels = covered_texels;
    result->dilated_texels = covered_texels >= baked_texels ? (covered_texels - baked_texels) : 0;
  }

  if (error_message != nullptr)
  {
    error_message->clear();
  }

  return true;
}

}  // namespace sdf
