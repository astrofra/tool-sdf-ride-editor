#include "sdf/uv_unwrap.h"

#include <algorithm>
#include <memory>
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

  std::vector<std::uint32_t> indices;
  indices.reserve(input_mesh.triangles.size() * 3);

  std::vector<std::uint32_t> face_materials;
  face_materials.reserve(input_mesh.triangles.size());

  for (const MeshTriangle &triangle : input_mesh.triangles)
  {
    if (triangle.i0 >= input_mesh.vertices.size() ||
        triangle.i1 >= input_mesh.vertices.size() ||
        triangle.i2 >= input_mesh.vertices.size())
    {
      return fail(error_message, "uv unwrap received a mesh with out-of-range triangle indices");
    }

    indices.push_back(triangle.i0);
    indices.push_back(triangle.i1);
    indices.push_back(triangle.i2);
    face_materials.push_back(triangle.material_id);
  }

  std::unique_ptr<xatlas::Atlas, AtlasDeleter> atlas(xatlas::Create());
  if (!atlas)
  {
    return fail(error_message, "xatlas failed to allocate an atlas");
  }

  xatlas::MeshDecl mesh_decl;
  mesh_decl.vertexCount = static_cast<std::uint32_t>(input_mesh.vertices.size());
  mesh_decl.vertexPositionData = &input_mesh.vertices.front().position;
  mesh_decl.vertexPositionStride = sizeof(MeshVertex);
  mesh_decl.vertexNormalData = &input_mesh.vertices.front().normal;
  mesh_decl.vertexNormalStride = sizeof(MeshVertex);
  mesh_decl.vertexUvData = &input_mesh.vertices.front().uv0;
  mesh_decl.vertexUvStride = sizeof(MeshVertex);
  mesh_decl.indexData = indices.data();
  mesh_decl.indexCount = static_cast<std::uint32_t>(indices.size());
  mesh_decl.faceMaterialData = face_materials.data();
  mesh_decl.faceCount = static_cast<std::uint32_t>(input_mesh.triangles.size());
  mesh_decl.indexFormat = xatlas::IndexFormat::UInt32;
  mesh_decl.epsilon = std::max(settings.epsilon, 1.0e-7f);

  const xatlas::AddMeshError add_error = xatlas::AddMesh(atlas.get(), mesh_decl);
  if (add_error != xatlas::AddMeshError::Success)
  {
    return fail(error_message, std::string("xatlas AddMesh failed: ") + xatlas::StringForEnum(add_error));
  }

  xatlas::ChartOptions chart_options;
  chart_options.fixWinding = true;

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
    if (source_vertex.xref >= input_mesh.vertices.size())
    {
      return fail(error_message, "xatlas returned a vertex reference outside the input mesh");
    }

    MeshVertex output_vertex = input_mesh.vertices[source_vertex.xref];
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
    triangle.material_id = triangle_index < input_mesh.triangles.size()
      ? input_mesh.triangles[triangle_index].material_id
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
