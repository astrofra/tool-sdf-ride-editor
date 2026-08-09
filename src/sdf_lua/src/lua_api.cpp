#include "sdf/lua_api.h"

#include <filesystem>
#include <utility>

#include "sdf/generator.h"
#include "sdf/obj_writer.h"

namespace sdf::lua_api
{

bool load_scene_file(
  const std::string &input_path,
  SceneFile &out_scene_file,
  std::string &error_message)
{
  return sdf::load_scene_file(std::filesystem::path(input_path), &out_scene_file, &error_message);
}

bool save_scene_file(
  const SceneFile &scene_file,
  const std::string &output_path,
  std::string &error_message)
{
  return sdf::save_scene_file(scene_file, std::filesystem::path(output_path), &error_message);
}

bool build_scene_file(
  const SceneFile &scene_file,
  const BuildRequest &request,
  BuildResult &out_build_result,
  std::string &error_message)
{
  if (request.export_obj && request.output_obj_path.empty())
  {
    error_message = "build_scene_file requires output_obj_path when export_obj is enabled";
    return false;
  }

  const SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  Mesh export_mesh = build.mesh;
  BuildResult result;
  result.sampled_cells = build.sampled_cells;
  result.occupied_cells = build.occupied_cells;
  result.generated_vertex_count = build.mesh.vertices.size();
  result.generated_triangle_count = build.mesh.triangles.size();

  if (request.unwrap_uvs)
  {
    Mesh unwrapped_mesh;
    UvUnwrapResult unwrap_result;
    if (!sdf::unwrap_mesh_uvs(
          build.mesh,
          request.uv_unwrap_settings,
          &unwrapped_mesh,
          &unwrap_result,
          &error_message))
    {
      return false;
    }

    export_mesh = std::move(unwrapped_mesh);
    result.uv_unwrap_performed = true;
    result.uv_unwrap_result = unwrap_result;
  }

  result.export_vertex_count = export_mesh.vertices.size();
  result.export_triangle_count = export_mesh.triangles.size();

  if (request.export_obj)
  {
    ObjWriteOptions options;
    options.object_name = scene_file.scene.name.empty() ? "blockout" : scene_file.scene.name;

    if (!sdf::write_obj(
          export_mesh,
          std::filesystem::path(request.output_obj_path),
          options,
          &error_message))
    {
      return false;
    }

    result.obj_written = true;
    result.output_obj_path = request.output_obj_path;
  }

  out_build_result = std::move(result);
  return true;
}

}  // namespace sdf::lua_api
