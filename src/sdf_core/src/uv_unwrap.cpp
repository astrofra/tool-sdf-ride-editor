#include "sdf/uv_unwrap.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "progress_utils.h"
#include "xatlas.h"

namespace sdf
{

namespace
{

struct AtlasDeleter
{
  void operator()(xatlas::Atlas *atlas) const
  {
    if (atlas != nullptr)
    {
      xatlas::Destroy(atlas);
    }
  }
};

bool fail(std::string *error_message, const std::string &message)
{
  if (error_message != nullptr)
  {
    *error_message = message;
  }
  return false;
}

bool is_power_of_two(std::uint32_t value)
{
  return value != 0u && (value & (value - 1u)) == 0u;
}

xatlas::PackOptions make_pack_options(
  const UvUnwrapSettings &settings,
  float texels_per_unit,
  bool create_image)
{
  xatlas::PackOptions pack_options;
  pack_options.maxChartSize = settings.max_chart_size;
  pack_options.padding = settings.padding;
  pack_options.texelsPerUnit = texels_per_unit;
  pack_options.resolution = settings.resolution;
  pack_options.bilinear = settings.bilinear;
  pack_options.blockAlign = settings.block_align;
  pack_options.bruteForce = settings.brute_force;
  pack_options.createImage = create_image;
  pack_options.rotateChartsToAxis = settings.rotate_charts_to_axis;
  pack_options.rotateCharts = settings.rotate_charts;
  return pack_options;
}

bool pack_charts_once(
  xatlas::Atlas *atlas,
  const UvUnwrapSettings &settings,
  float texels_per_unit,
  bool create_image)
{
  xatlas::PackOptions pack_options = make_pack_options(settings, texels_per_unit, create_image);
  xatlas::PackCharts(atlas, pack_options);
  return atlas->atlasCount == 1 &&
         atlas->width == settings.resolution &&
         atlas->height == settings.resolution;
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

std::array<unsigned char, 3> chart_color(std::uint32_t chart_index)
{
  const std::uint32_t hash = hash_u32(chart_index + 0x9e3779b9u);
  return {{
    static_cast<unsigned char>(96u + ((hash >> 0) & 0x7fu)),
    static_cast<unsigned char>(96u + ((hash >> 8) & 0x7fu)),
    static_cast<unsigned char>(96u + ((hash >> 16) & 0x7fu))
  }};
}

unsigned char mix_byte(unsigned char lhs, unsigned char rhs, float rhs_weight)
{
  const float lhs_weight = 1.0f - rhs_weight;
  const float value =
    static_cast<float>(lhs) * lhs_weight +
    static_cast<float>(rhs) * rhs_weight;
  return static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

bool build_chart_debug_image(
  const xatlas::Atlas &atlas,
  Rgb8Image *chart_debug_image,
  std::string *error_message)
{
  if (chart_debug_image == nullptr)
  {
    return true;
  }

  if (atlas.image == nullptr || atlas.width == 0 || atlas.height == 0)
  {
    return fail(error_message, "uv chart debug image requires a populated xatlas image");
  }

  chart_debug_image->width = static_cast<int>(atlas.width);
  chart_debug_image->height = static_cast<int>(atlas.height);
  chart_debug_image->pixels.assign(
    static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height) * 3u,
    0u);

  const std::size_t texel_count = static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height);
  for (std::size_t texel_index = 0; texel_index < texel_count; ++texel_index)
  {
    const std::uint32_t data = atlas.image[texel_index];
    unsigned char *rgb = &chart_debug_image->pixels[texel_index * 3u];
    if ((data & xatlas::kImageHasChartIndexBit) == 0)
    {
      rgb[0] = rgb[1] = rgb[2] = 0u;
      continue;
    }

    const std::array<unsigned char, 3> base_color = chart_color(data & xatlas::kImageChartIndexMask);
    rgb[0] = base_color[0];
    rgb[1] = base_color[1];
    rgb[2] = base_color[2];

    if ((data & xatlas::kImageIsPaddingBit) != 0)
    {
      rgb[0] = mix_byte(rgb[0], 32u, 0.55f);
      rgb[1] = mix_byte(rgb[1], 64u, 0.55f);
      rgb[2] = mix_byte(rgb[2], 224u, 0.55f);
    }
    else if ((data & xatlas::kImageIsBilinearBit) != 0)
    {
      rgb[0] = mix_byte(rgb[0], 32u, 0.45f);
      rgb[1] = mix_byte(rgb[1], 224u, 0.45f);
      rgb[2] = mix_byte(rgb[2], 48u, 0.45f);
    }
  }

  return true;
}

const char *progress_stage_name(xatlas::ProgressCategory category)
{
  switch (category)
  {
  case xatlas::ProgressCategory::AddMesh:
    return "xatlas AddMesh";
  case xatlas::ProgressCategory::ComputeCharts:
    return "xatlas ComputeCharts";
  case xatlas::ProgressCategory::PackCharts:
    return "xatlas PackCharts";
  case xatlas::ProgressCategory::BuildOutputMeshes:
    return "xatlas BuildOutputMeshes";
  default:
    return "xatlas";
  }
}

struct XatlasProgressBridge
{
  const ProgressCallback *callback = nullptr;
};

bool forward_xatlas_progress(xatlas::ProgressCategory category, int progress, void *user_data)
{
  if (user_data == nullptr)
  {
    return true;
  }

  const XatlasProgressBridge *bridge = static_cast<const XatlasProgressBridge *>(user_data);
  if (bridge->callback == nullptr || !(*bridge->callback))
  {
    return true;
  }

  const int clamped_progress = std::clamp(progress, 0, 100);
  (*bridge->callback)(ProgressUpdate{
    progress_stage_name(category),
    static_cast<std::uint64_t>(clamped_progress),
    100u,
    clamped_progress
  });
  return true;
}

struct QuantizedPositionKey
{
  int x = 0;
  int y = 0;
  int z = 0;

  bool operator==(const QuantizedPositionKey &rhs) const
  {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }
};

struct QuantizedPositionKeyHash
{
  std::size_t operator()(const QuantizedPositionKey &key) const
  {
    const std::uint32_t hx = static_cast<std::uint32_t>(key.x) * 73856093u;
    const std::uint32_t hy = static_cast<std::uint32_t>(key.y) * 19349663u;
    const std::uint32_t hz = static_cast<std::uint32_t>(key.z) * 83492791u;
    return static_cast<std::size_t>(hx ^ hy ^ hz);
  }
};

struct WeldedVertexAccum
{
  Vec3 position_sum = {0.0f, 0.0f, 0.0f};
  Vec3 normal_sum = {0.0f, 0.0f, 0.0f};
  Vec2 uv_sum = {0.0f, 0.0f};
  std::uint32_t count = 0;
};

struct UnwrapInputMesh
{
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> face_materials;
};

bool resolve_triangle_chart_id(
  const xatlas::Mesh &atlas_mesh,
  std::uint32_t i0,
  std::uint32_t i1,
  std::uint32_t i2,
  std::int32_t *chart_id)
{
  if (chart_id == nullptr)
  {
    return false;
  }

  const std::int32_t c0 = atlas_mesh.vertexArray[i0].chartIndex;
  const std::int32_t c1 = atlas_mesh.vertexArray[i1].chartIndex;
  const std::int32_t c2 = atlas_mesh.vertexArray[i2].chartIndex;

  std::int32_t resolved = -1;
  for (std::int32_t candidate : {c0, c1, c2})
  {
    if (candidate >= 0)
    {
      if (resolved >= 0 && candidate != resolved)
      {
        return false;
      }
      resolved = candidate;
    }
  }

  *chart_id = resolved;
  return true;
}

void populate_chart_triangle_stats(const xatlas::Mesh &atlas_mesh, UvUnwrapResult *result)
{
  if (result == nullptr || atlas_mesh.chartCount == 0)
  {
    return;
  }

  std::size_t total_chart_triangles = 0;
  result->min_chart_triangle_count = std::numeric_limits<std::size_t>::max();
  result->max_chart_triangle_count = 0;
  result->single_triangle_chart_count = 0;
  for (std::uint32_t chart_index = 0; chart_index < atlas_mesh.chartCount; ++chart_index)
  {
    const std::size_t triangle_count = atlas_mesh.chartArray[chart_index].faceCount;
    result->min_chart_triangle_count = std::min(result->min_chart_triangle_count, triangle_count);
    result->max_chart_triangle_count = std::max(result->max_chart_triangle_count, triangle_count);
    result->single_triangle_chart_count += triangle_count == 1 ? 1u : 0u;
    total_chart_triangles += triangle_count;
  }

  result->average_chart_triangle_count =
    static_cast<float>(total_chart_triangles) / static_cast<float>(atlas_mesh.chartCount);
}

void populate_chart_texel_stats(const xatlas::Atlas &atlas, UvUnwrapResult *result)
{
  if (result == nullptr || atlas.image == nullptr || result->chart_count == 0)
  {
    return;
  }

  std::vector<std::size_t> chart_texel_counts(result->chart_count, 0u);
  const std::size_t texel_count = static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height);
  for (std::size_t texel_index = 0; texel_index < texel_count; ++texel_index)
  {
    const std::uint32_t data = atlas.image[texel_index];
    if ((data & xatlas::kImageHasChartIndexBit) == 0)
    {
      continue;
    }

    if ((data & xatlas::kImageIsPaddingBit) != 0)
    {
      ++result->padding_texel_count;
      continue;
    }

    const std::uint32_t chart_index = data & xatlas::kImageChartIndexMask;
    if (chart_index >= chart_texel_counts.size())
    {
      continue;
    }

    ++chart_texel_counts[chart_index];
    ++result->chart_texel_count;
  }

  result->min_chart_texel_count = std::numeric_limits<std::size_t>::max();
  result->max_chart_texel_count = 0;
  for (std::size_t texel_count_for_chart : chart_texel_counts)
  {
    result->min_chart_texel_count = std::min(result->min_chart_texel_count, texel_count_for_chart);
    result->max_chart_texel_count = std::max(result->max_chart_texel_count, texel_count_for_chart);
  }

  result->average_chart_texel_count =
    static_cast<float>(result->chart_texel_count) / static_cast<float>(result->chart_count);
}

QuantizedPositionKey quantize_position(const Vec3 &position, float epsilon)
{
  const float safe_epsilon = std::max(epsilon, 1.0e-7f);
  return {
    static_cast<int>(std::floor(position.x / safe_epsilon)),
    static_cast<int>(std::floor(position.y / safe_epsilon)),
    static_cast<int>(std::floor(position.z / safe_epsilon))
  };
}

bool build_unwrap_input_mesh(
  const Mesh &input_mesh,
  float epsilon,
  UnwrapInputMesh *unwrap_mesh,
  const ProgressCallback &progress_callback)
{
  if (unwrap_mesh == nullptr)
  {
    return false;
  }

  const float safe_epsilon = std::max(epsilon, 1.0e-7f);
  const float epsilon_squared = safe_epsilon * safe_epsilon;

  std::vector<std::uint32_t> original_to_welded(input_mesh.vertices.size(), 0u);
  std::vector<WeldedVertexAccum> accumulators;
  std::unordered_map<QuantizedPositionKey, std::vector<std::uint32_t>, QuantizedPositionKeyHash> buckets;
  buckets.reserve(input_mesh.vertices.size());
  accumulators.reserve(input_mesh.vertices.size());
  unwrap_mesh->vertices.clear();
  unwrap_mesh->indices.clear();
  unwrap_mesh->face_materials.clear();
  detail::ProgressScope weld_progress(
    progress_callback,
    "Weld unwrap input",
    static_cast<std::uint64_t>(input_mesh.vertices.size()));

  for (std::size_t original_index = 0; original_index < input_mesh.vertices.size(); ++original_index)
  {
    const MeshVertex &vertex = input_mesh.vertices[original_index];
    const QuantizedPositionKey base_key = quantize_position(vertex.position, safe_epsilon);

    std::uint32_t welded_index = std::numeric_limits<std::uint32_t>::max();
    for (int dz = -1; dz <= 1 && welded_index == std::numeric_limits<std::uint32_t>::max(); ++dz)
    {
      for (int dy = -1; dy <= 1 && welded_index == std::numeric_limits<std::uint32_t>::max(); ++dy)
      {
        for (int dx = -1; dx <= 1 && welded_index == std::numeric_limits<std::uint32_t>::max(); ++dx)
        {
          const QuantizedPositionKey neighbor_key = {
            base_key.x + dx,
            base_key.y + dy,
            base_key.z + dz
          };
          const auto bucket_it = buckets.find(neighbor_key);
          if (bucket_it == buckets.end())
          {
            continue;
          }

          for (std::uint32_t candidate_index : bucket_it->second)
          {
            const Vec3 delta = unwrap_mesh->vertices[candidate_index].position - vertex.position;
            if (length_squared(delta) <= epsilon_squared)
            {
              welded_index = candidate_index;
              break;
            }
          }
        }
      }
    }

    if (welded_index == std::numeric_limits<std::uint32_t>::max())
    {
      welded_index = static_cast<std::uint32_t>(unwrap_mesh->vertices.size());
      unwrap_mesh->vertices.push_back(vertex);
      accumulators.push_back({});
      buckets[base_key].push_back(welded_index);
    }

    WeldedVertexAccum &accum = accumulators[welded_index];
    accum.position_sum += vertex.position;
    accum.normal_sum += vertex.normal;
    accum.uv_sum.x += vertex.uv0.x;
    accum.uv_sum.y += vertex.uv0.y;
    accum.count += 1u;
    original_to_welded[original_index] = welded_index;
    weld_progress.update(static_cast<std::uint64_t>(original_index + 1));
  }

  weld_progress.finish();

  for (std::size_t welded_index = 0; welded_index < unwrap_mesh->vertices.size(); ++welded_index)
  {
    const WeldedVertexAccum &accum = accumulators[welded_index];
    const float count = accum.count > 0 ? static_cast<float>(accum.count) : 1.0f;
    MeshVertex &vertex = unwrap_mesh->vertices[welded_index];
    vertex.position = accum.position_sum / count;
    vertex.normal = normalized(accum.normal_sum);
    vertex.uv0 = {accum.uv_sum.x / count, accum.uv_sum.y / count};
  }

  unwrap_mesh->indices.reserve(input_mesh.triangles.size() * 3);
  unwrap_mesh->face_materials.reserve(input_mesh.triangles.size());
  for (const MeshTriangle &triangle : input_mesh.triangles)
  {
    if (triangle.i0 >= original_to_welded.size() ||
        triangle.i1 >= original_to_welded.size() ||
        triangle.i2 >= original_to_welded.size())
    {
      return false;
    }

    const std::uint32_t i0 = original_to_welded[triangle.i0];
    const std::uint32_t i1 = original_to_welded[triangle.i1];
    const std::uint32_t i2 = original_to_welded[triangle.i2];

    if (i0 == i1 || i1 == i2 || i0 == i2)
    {
      continue;
    }

    unwrap_mesh->indices.push_back(i0);
    unwrap_mesh->indices.push_back(i1);
    unwrap_mesh->indices.push_back(i2);
    unwrap_mesh->face_materials.push_back(triangle.material_id);
  }

  return !unwrap_mesh->vertices.empty() && !unwrap_mesh->indices.empty();
}

bool pack_charts_exact_resolution(
  xatlas::Atlas *atlas,
  const UvUnwrapSettings &settings,
  bool create_image,
  std::string *error_message)
{
  if (atlas == nullptr)
  {
    return fail(error_message, "uv unwrap requires a valid xatlas atlas");
  }

  if (settings.resolution == 0u)
  {
    return fail(error_message, "uv unwrap requires a strictly positive atlas resolution");
  }

  if (!is_power_of_two(settings.resolution))
  {
    return fail(error_message, "uv unwrap atlas resolution must be a power of two");
  }

  float resolved_texels_per_unit = settings.texels_per_unit;
  if (resolved_texels_per_unit <= 0.0f)
  {
    xatlas::PackOptions estimate_options = make_pack_options(settings, 0.0f, false);
    xatlas::PackCharts(atlas, estimate_options);
    if (atlas->texelsPerUnit <= 0.0f)
    {
      return fail(error_message, "xatlas failed to estimate texels-per-unit for the requested atlas resolution");
    }

    resolved_texels_per_unit = atlas->texelsPerUnit;
    if (!pack_charts_once(atlas, settings, resolved_texels_per_unit, false))
    {
      constexpr int kMaxHalvingSteps = 16;
      constexpr int kBinarySearchSteps = 6;
      float min_not_fitting_texels_per_unit = resolved_texels_per_unit;
      bool found_fit = false;
      for (int step = 0; step < kMaxHalvingSteps; ++step)
      {
        resolved_texels_per_unit *= 0.5f;
        if (resolved_texels_per_unit <= 1.0e-6f)
        {
          break;
        }

        if (pack_charts_once(atlas, settings, resolved_texels_per_unit, false))
        {
          found_fit = true;
          break;
        }

        min_not_fitting_texels_per_unit = resolved_texels_per_unit;
      }

      if (!found_fit)
      {
        return fail(
          error_message,
          "uv unwrap could not fit the mesh into the requested fixed atlas resolution; increase --uv-resolution");
      }

      for (int step = 0; step < kBinarySearchSteps; ++step)
      {
        const float mid_texels_per_unit = 0.5f * (resolved_texels_per_unit + min_not_fitting_texels_per_unit);
        if (pack_charts_once(atlas, settings, mid_texels_per_unit, false))
        {
          resolved_texels_per_unit = mid_texels_per_unit;
        }
        else
        {
          min_not_fitting_texels_per_unit = mid_texels_per_unit;
        }
      }
    }
  }
  else if (!pack_charts_once(atlas, settings, resolved_texels_per_unit, false))
  {
    return fail(
      error_message,
      "uv unwrap could not fit the mesh into the requested fixed atlas resolution with the requested texel density");
  }

  if (!pack_charts_once(atlas, settings, resolved_texels_per_unit, create_image))
  {
    return fail(
      error_message,
      "uv unwrap could not fit the mesh into a single fixed-resolution atlas; increase --uv-resolution or reduce density");
  }

  if (atlas->atlasCount != 1)
  {
    return fail(
      error_message,
      "uv unwrap could not fit the mesh into a single fixed-resolution atlas; increase --uv-resolution or reduce density");
  }

  if (atlas->width != settings.resolution || atlas->height != settings.resolution)
  {
    return fail(
      error_message,
      "uv unwrap failed to preserve the requested fixed atlas resolution");
  }

  return true;
}

}  // namespace

bool unwrap_mesh_uvs(
  const Mesh &input_mesh,
  const UvUnwrapSettings &settings,
  Mesh *output_mesh,
  UvUnwrapResult *result,
  std::string *error_message,
  const ProgressCallback &progress_callback,
  Rgb8Image *chart_debug_image)
{
  if (output_mesh == nullptr)
  {
    return fail(error_message, "uv unwrap requires a valid output mesh");
  }

  if (input_mesh.vertices.empty() || input_mesh.triangles.empty())
  {
    return fail(error_message, "uv unwrap requires a non-empty mesh");
  }

  UnwrapInputMesh unwrap_input_mesh;
  if (!build_unwrap_input_mesh(input_mesh, settings.epsilon, &unwrap_input_mesh, progress_callback))
  {
    return fail(error_message, "uv unwrap failed while welding the input mesh");
  }

  std::unique_ptr<xatlas::Atlas, AtlasDeleter> atlas(xatlas::Create());
  if (!atlas)
  {
    return fail(error_message, "xatlas failed to allocate an atlas");
  }

  XatlasProgressBridge progress_bridge;
  progress_bridge.callback = &progress_callback;
  xatlas::SetProgressCallback(atlas.get(), forward_xatlas_progress, &progress_bridge);

  xatlas::MeshDecl mesh_decl;
  mesh_decl.vertexCount = static_cast<std::uint32_t>(unwrap_input_mesh.vertices.size());
  mesh_decl.vertexPositionData = &unwrap_input_mesh.vertices.front().position;
  mesh_decl.vertexPositionStride = sizeof(MeshVertex);
  mesh_decl.vertexNormalData = &unwrap_input_mesh.vertices.front().normal;
  mesh_decl.vertexNormalStride = sizeof(MeshVertex);
  mesh_decl.vertexUvData = nullptr;
  mesh_decl.vertexUvStride = 0;
  mesh_decl.indexData = unwrap_input_mesh.indices.data();
  mesh_decl.indexCount = static_cast<std::uint32_t>(unwrap_input_mesh.indices.size());
  mesh_decl.faceMaterialData = unwrap_input_mesh.face_materials.data();
  mesh_decl.faceCount = static_cast<std::uint32_t>(unwrap_input_mesh.face_materials.size());
  mesh_decl.indexFormat = xatlas::IndexFormat::UInt32;
  mesh_decl.epsilon = std::max(settings.epsilon, 1.0e-7f);

  const xatlas::AddMeshError add_error = xatlas::AddMesh(atlas.get(), mesh_decl);
  if (add_error != xatlas::AddMeshError::Success)
  {
    return fail(error_message, std::string("xatlas AddMesh failed: ") + xatlas::StringForEnum(add_error));
  }

  xatlas::ChartOptions chart_options;
  chart_options.fixWinding = true;
  chart_options.maxIterations = settings.chart_max_iterations;
  chart_options.maxCost = settings.chart_max_cost;
  chart_options.normalDeviationWeight = settings.normal_deviation_weight;
  chart_options.roundnessWeight = settings.roundness_weight;
  chart_options.straightnessWeight = settings.straightness_weight;
  chart_options.normalSeamWeight = settings.normal_seam_weight;
  chart_options.textureSeamWeight = settings.texture_seam_weight;

  xatlas::ComputeCharts(atlas.get(), chart_options);
  if (!pack_charts_exact_resolution(
        atlas.get(),
        settings,
        result != nullptr || chart_debug_image != nullptr,
        error_message))
  {
    return false;
  }

  if (atlas->meshCount != 1)
  {
    return fail(error_message, "xatlas returned an unexpected mesh count");
  }

  if (atlas->width == 0 || atlas->height == 0)
  {
    return fail(error_message, "xatlas returned an empty atlas");
  }

  const xatlas::Mesh &atlas_mesh = atlas->meshes[0];
  if (atlas_mesh.indexCount % 3 != 0)
  {
    return fail(error_message, "xatlas returned a non-triangulated output index buffer");
  }

  Mesh unwrapped_mesh;
  unwrapped_mesh.vertices.resize(atlas_mesh.vertexCount);

  const float inverse_width = 1.0f / static_cast<float>(atlas->width);
  const float inverse_height = 1.0f / static_cast<float>(atlas->height);

  for (std::uint32_t vertex_index = 0; vertex_index < atlas_mesh.vertexCount; ++vertex_index)
  {
    const xatlas::Vertex &source_vertex = atlas_mesh.vertexArray[vertex_index];
    if (source_vertex.xref >= unwrap_input_mesh.vertices.size())
    {
      return fail(error_message, "xatlas returned a vertex reference outside the input mesh");
    }

    MeshVertex output_vertex = unwrap_input_mesh.vertices[source_vertex.xref];
    output_vertex.uv0 = {
      source_vertex.uv[0] * inverse_width,
      source_vertex.uv[1] * inverse_height
    };
    unwrapped_mesh.vertices[vertex_index] = output_vertex;
  }

  const std::size_t triangle_count = atlas_mesh.indexCount / 3;
  unwrapped_mesh.triangles.reserve(triangle_count);

  for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index)
  {
    const std::uint32_t i0 = atlas_mesh.indexArray[triangle_index * 3 + 0];
    const std::uint32_t i1 = atlas_mesh.indexArray[triangle_index * 3 + 1];
    const std::uint32_t i2 = atlas_mesh.indexArray[triangle_index * 3 + 2];

    if (i0 >= unwrapped_mesh.vertices.size() ||
        i1 >= unwrapped_mesh.vertices.size() ||
        i2 >= unwrapped_mesh.vertices.size())
    {
      return fail(error_message, "xatlas returned an index outside the unwrapped vertex buffer");
    }

    MeshTriangle triangle;
    triangle.i0 = i0;
    triangle.i1 = i1;
    triangle.i2 = i2;
    triangle.material_id = triangle_index < unwrap_input_mesh.face_materials.size()
      ? unwrap_input_mesh.face_materials[triangle_index]
      : 0;
    if (!resolve_triangle_chart_id(atlas_mesh, i0, i1, i2, &triangle.uv_chart_id))
    {
      return fail(error_message, "xatlas returned a triangle spanning multiple chart indices");
    }
    unwrapped_mesh.triangles.push_back(triangle);
  }

  *output_mesh = std::move(unwrapped_mesh);

  if (result != nullptr)
  {
    result->atlas_width = atlas->width;
    result->atlas_height = atlas->height;
    result->atlas_count = atlas->atlasCount;
    result->chart_count = atlas->chartCount;
    result->vertex_count = output_mesh->vertices.size();
    result->triangle_count = output_mesh->triangles.size();
    result->texels_per_unit = atlas->texelsPerUnit;
    result->utilization = atlas->utilization != nullptr ? atlas->utilization[0] : 0.0f;
    populate_chart_triangle_stats(atlas_mesh, result);
    populate_chart_texel_stats(*atlas, result);
  }

  if (!build_chart_debug_image(*atlas, chart_debug_image, error_message))
  {
    return false;
  }

  if (error_message != nullptr)
  {
    error_message->clear();
  }

  return true;
}

}  // namespace sdf
