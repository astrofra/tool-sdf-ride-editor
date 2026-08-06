#include "sdf/debug_render.h"

#include <array>
#include <cmath>

namespace sdf
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

struct CameraBasis
{
  Vec3 forward;
  Vec3 right;
  Vec3 up;
};

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

float smoothstep01(float value)
{
  const float t = clampf(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
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

CameraBasis build_camera_basis(DebugCameraPreset preset)
{
  Vec3 forward;
  switch (preset)
  {
  case DebugCameraPreset::Front:
    forward = normalized({0.0f, -0.08f, 1.0f});
    break;
  case DebugCameraPreset::LeftThreeQuarter:
    forward = normalized({0.75f, -0.12f, 1.0f});
    break;
  case DebugCameraPreset::RightThreeQuarter:
    forward = normalized({-0.75f, -0.12f, 1.0f});
    break;
  default:
    forward = normalized({0.0f, 0.0f, 1.0f});
    break;
  }

  Vec3 up_seed = {0.0f, 1.0f, 0.0f};
  Vec3 right = normalized(cross(up_seed, forward));
  if (length_squared(right) <= 1.0e-8f)
  {
    up_seed = {1.0f, 0.0f, 0.0f};
    right = normalized(cross(up_seed, forward));
  }

  CameraBasis basis;
  basis.forward = forward;
  basis.right = right;
  basis.up = normalized(cross(forward, right));
  return basis;
}

std::array<Vec3, 8> aabb_corners(const Aabb &bounds)
{
  return {{
    {bounds.min.x, bounds.min.y, bounds.min.z},
    {bounds.max.x, bounds.min.y, bounds.min.z},
    {bounds.max.x, bounds.max.y, bounds.min.z},
    {bounds.min.x, bounds.max.y, bounds.min.z},
    {bounds.min.x, bounds.min.y, bounds.max.z},
    {bounds.max.x, bounds.min.y, bounds.max.z},
    {bounds.max.x, bounds.max.y, bounds.max.z},
    {bounds.min.x, bounds.max.y, bounds.max.z}
  }};
}

Vec3 sample_cosine_hemisphere(const Vec3 &normal, Rng *rng)
{
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
  const RayHit &hit,
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
    const Vec3 direction = sample_cosine_hemisphere(hit.shading_normal, &rng);
    Ray ray;
    ray.origin = hit.position + hit.shading_normal * bias;
    ray.direction = direction;

    if (!is_occluded(scene, ray, max_distance))
    {
      ++unoccluded;
    }
  }

  return static_cast<float>(unoccluded) / static_cast<float>(sample_count);
}

unsigned char to_byte(float value)
{
  return static_cast<unsigned char>(clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

}  // namespace

bool render_debug_image(
  const RayScene &scene,
  const DebugRenderSettings &settings,
  Rgb8Image *image,
  std::string *error_message)
{
  if (image == nullptr)
  {
    if (error_message != nullptr)
    {
      *error_message = "render_debug_image requires a non-null image output";
    }
    return false;
  }

  if (settings.width <= 0 || settings.height <= 0)
  {
    if (error_message != nullptr)
    {
      *error_message = "debug render dimensions must be strictly positive";
    }
    return false;
  }

  if (scene.triangles.empty())
  {
    if (error_message != nullptr)
    {
      *error_message = "debug render requires a non-empty ray scene";
    }
    return false;
  }

  const CameraBasis basis = build_camera_basis(settings.camera_preset);
  const Vec3 center = (scene.bounds.min + scene.bounds.max) * 0.5f;
  const std::array<Vec3, 8> corners = aabb_corners(scene.bounds);

  float min_x = 1.0e30f;
  float max_x = -1.0e30f;
  float min_y = 1.0e30f;
  float max_y = -1.0e30f;
  float min_z = 1.0e30f;
  float max_z = -1.0e30f;

  for (const Vec3 &corner : corners)
  {
    const Vec3 delta = corner - center;
    min_x = std::min(min_x, dot(delta, basis.right));
    max_x = std::max(max_x, dot(delta, basis.right));
    min_y = std::min(min_y, dot(delta, basis.up));
    max_y = std::max(max_y, dot(delta, basis.up));
    min_z = std::min(min_z, dot(delta, basis.forward));
    max_z = std::max(max_z, dot(delta, basis.forward));
  }

  float half_width = std::max(std::fabs(min_x), std::fabs(max_x));
  float half_height = std::max(std::fabs(min_y), std::fabs(max_y));
  const float half_depth = std::max(std::fabs(min_z), std::fabs(max_z));
  const float aspect = static_cast<float>(settings.width) / static_cast<float>(settings.height);

  if (half_height <= 1.0e-6f)
  {
    half_height = 1.0f;
  }
  if (half_width <= 1.0e-6f)
  {
    half_width = half_height * aspect;
  }

  if ((half_width / half_height) < aspect)
  {
    half_width = half_height * aspect;
  }
  else
  {
    half_height = half_width / aspect;
  }

  half_width *= std::max(settings.ortho_margin, 1.0f);
  half_height *= std::max(settings.ortho_margin, 1.0f);

  const float scene_diagonal = length(scene.bounds.max - scene.bounds.min);
  const float camera_distance = half_depth + scene_diagonal * 0.75f + 1.0f;
  const float max_distance = camera_distance * 2.0f;

  image->width = settings.width;
  image->height = settings.height;
  image->pixels.assign(static_cast<std::size_t>(settings.width * settings.height * 3), 0u);

  for (int y = 0; y < settings.height; ++y)
  {
    const float v = settings.height > 1 ? static_cast<float>(y) / static_cast<float>(settings.height - 1) : 0.5f;
    const float sy = (1.0f - v * 2.0f) * half_height;

    for (int x = 0; x < settings.width; ++x)
    {
      const float u = settings.width > 1 ? static_cast<float>(x) / static_cast<float>(settings.width - 1) : 0.5f;
      const float sx = (u * 2.0f - 1.0f) * half_width;

      Ray ray;
      ray.origin = center - basis.forward * camera_distance + basis.right * sx + basis.up * sy;
      ray.direction = basis.forward;

      RayHit hit;
      const bool did_hit = intersect_ray(scene, ray, max_distance, &hit);
      const std::size_t pixel_index = static_cast<std::size_t>((x + y * settings.width) * 3);
      if (!did_hit)
      {
        image->pixels[pixel_index + 0] = 0;
        image->pixels[pixel_index + 1] = 0;
        image->pixels[pixel_index + 2] = 0;
        continue;
      }

      if (settings.mode == DebugRenderMode::Depth)
      {
        const float value = 1.0f - clampf(hit.t / max_distance, 0.0f, 1.0f);
        const unsigned char gray = to_byte(value);
        image->pixels[pixel_index + 0] = gray;
        image->pixels[pixel_index + 1] = gray;
        image->pixels[pixel_index + 2] = gray;
        continue;
      }

      if (settings.mode == DebugRenderMode::Normal)
      {
        const Vec3 normal_color = hit.shading_normal * 0.5f + Vec3{0.5f, 0.5f, 0.5f};
        image->pixels[pixel_index + 0] = to_byte(normal_color.x);
        image->pixels[pixel_index + 1] = to_byte(normal_color.y);
        image->pixels[pixel_index + 2] = to_byte(normal_color.z);
        continue;
      }

      const std::uint32_t pixel_seed = hash_u32(
        settings.seed ^
        static_cast<std::uint32_t>(x * 1973) ^
        static_cast<std::uint32_t>(y * 9277));
      const float ao = estimate_ambient_occlusion(
        scene,
        hit,
        settings.ao_samples,
        settings.ao_max_distance,
        pixel_seed);
      const unsigned char gray = to_byte(ao);
      image->pixels[pixel_index + 0] = gray;
      image->pixels[pixel_index + 1] = gray;
      image->pixels[pixel_index + 2] = gray;
    }
  }

  return true;
}

}  // namespace sdf
