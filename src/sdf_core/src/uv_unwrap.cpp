#include "sdf/uv_unwrap.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

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

QuantizedPositionKey quantize_position(const Vec3 &position, float epsilon)
{
  const float safe_epsilon = std::max(epsilon, 1.0e-7f);
  return {
    static_cast<int>(std::floor(position.x / safe_epsilon)),
    static_cast<int>(std::floor(position.y / safe_epsilon)),
    static_cast<int>(std::floor(position.z / safe_epsilon))
  };
}

bool build_unwrap_input_mesh(const Mesh &input_mesh, float epsilon, UnwrapInputMesh *unwrap_mesh)
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
  }

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

}  // namespace

bool unwrap_mesh_uvs(
  const Mesh &input_mesh,
  const UvUnwrapSettings &settings,
  Mesh *output_mesh,
  UvUnwrapResult *result,
  std::string *error_message)
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
  if (!build_unwrap_input_mesh(input_mesh, settings.epsilon, &unwrap_input_mesh))
  {
    return fail(error_message, "uv unwrap failed while welding the input mesh");
  }

  std::unique_ptr<xatlas::Atlas, AtlasDeleter> atlas(xatlas::Create());
  if (!atlas)
  {
    return fail(error_message, "xatlas failed to allocate an atlas");
  }

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

  xatlas::PackOptions pack_options;
  pack_options.maxChartSize = settings.max_chart_size;
  pack_options.padding = settings.padding;
  pack_options.texelsPerUnit = settings.texels_per_unit;
  pack_options.resolution = settings.resolution;
  pack_options.bilinear = settings.bilinear;
  pack_options.blockAlign = settings.block_align;
  pack_options.bruteForce = settings.brute_force;
  pack_options.rotateChartsToAxis = settings.rotate_charts_to_axis;
  pack_options.rotateCharts = settings.rotate_charts;

  xatlas::Generate(atlas.get(), chart_options, pack_options);

  if (atlas->meshCount != 1)
  {
    return fail(error_message, "xatlas returned an unexpected mesh count");
  }

  if (atlas->atlasCount != 1)
  {
    return fail(
      error_message,
      "uv unwrap currently supports a single atlas page only; increase resolution or reduce density");
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
  }

  if (error_message != nullptr)
  {
    error_message->clear();
  }

  return true;
}

}  // namespace sdf
