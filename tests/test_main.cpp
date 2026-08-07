#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sdf/bake_ao.h"
#include "sdf/debug_render.h"
#include "sdf/generator.h"
#include "sdf/image_write.h"
#include "sdf/obj_writer.h"
#include "sdf/raytrace.h"
#include "sdf/scene.h"
#include "sdf/scene_io.h"
#include "sdf/uv_unwrap.h"

namespace
{

#ifndef SDF_PROJECT_SOURCE_DIR
#define SDF_PROJECT_SOURCE_DIR "."
#endif

void expect_true(bool condition, const std::string &message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

std::filesystem::path sample_scene_path()
{
  return std::filesystem::path(SDF_PROJECT_SOURCE_DIR) / "scenes" / "frame_006_blockout.sdfscene";
}

void expect_same_noise_modifier(
  const sdf::NoiseDisplaceMaskedModifier &lhs,
  const sdf::NoiseDisplaceMaskedModifier &rhs)
{
  expect_true(lhs.name == rhs.name, "noise modifier names should match");
  expect_true(lhs.target_box_name == rhs.target_box_name, "noise modifier targets should match");
  expect_true(lhs.amplitude == rhs.amplitude, "noise modifier amplitudes should match");
  expect_true(lhs.frequency == rhs.frequency, "noise modifier frequencies should match");
  expect_true(lhs.seed == rhs.seed, "noise modifier seeds should match");
  expect_true(lhs.octaves == rhs.octaves, "noise modifier octaves should match");
  expect_true(lhs.mask == rhs.mask, "noise modifier masks should match");
  expect_true(lhs.mask_width == rhs.mask_width, "noise modifier mask widths should match");
}

void expect_same_box_cut_modifier(const sdf::BoxCutModifier &lhs, const sdf::BoxCutModifier &rhs)
{
  expect_true(lhs.name == rhs.name, "box_cut names should match");
  expect_true(lhs.target_box_name == rhs.target_box_name, "box_cut targets should match");
  expect_true(lhs.translation.x == rhs.translation.x, "box_cut translation x should match");
  expect_true(lhs.translation.y == rhs.translation.y, "box_cut translation y should match");
  expect_true(lhs.translation.z == rhs.translation.z, "box_cut translation z should match");
  expect_true(lhs.half_size.x == rhs.half_size.x, "box_cut half_size x should match");
  expect_true(lhs.half_size.y == rhs.half_size.y, "box_cut half_size y should match");
  expect_true(lhs.half_size.z == rhs.half_size.z, "box_cut half_size z should match");
}

void expect_same_box(const sdf::SdfBox &lhs, const sdf::SdfBox &rhs)
{
  expect_true(lhs.name == rhs.name, "box names should match");
  expect_true(lhs.op == rhs.op, "box operations should match");
  expect_true(lhs.material_id == rhs.material_id, "box material ids should match");
  expect_true(lhs.transform.translation.x == rhs.transform.translation.x, "box translation x should match");
  expect_true(lhs.transform.translation.y == rhs.transform.translation.y, "box translation y should match");
  expect_true(lhs.transform.translation.z == rhs.transform.translation.z, "box translation z should match");
  expect_true(lhs.half_size.x == rhs.half_size.x, "box half_size x should match");
  expect_true(lhs.half_size.y == rhs.half_size.y, "box half_size y should match");
  expect_true(lhs.half_size.z == rhs.half_size.z, "box half_size z should match");
}

void test_frame_scene_layout()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(ok, "sample scene file should load");

  const sdf::SceneDocument expected_scene = sdf::make_frame_006_blockout_scene();
  const sdf::BuildSettings expected_settings = sdf::make_frame_006_build_settings();

  expect_true(scene_file.scene.name == expected_scene.name, "scene should keep a stable blockout name");
  expect_true(scene_file.scene.boxes.size() == expected_scene.boxes.size(), "scene file should keep the expected box count");
  expect_true(
    scene_file.scene.noise_modifiers.size() == expected_scene.noise_modifiers.size(),
    "scene file should keep the expected noise modifier count");
  expect_true(
    scene_file.scene.box_cut_modifiers.size() == expected_scene.box_cut_modifiers.size(),
    "scene file should keep the expected box_cut modifier count");
  expect_true(scene_file.build_settings.cell_size == expected_settings.cell_size, "scene file should keep the expected default cell size");
  expect_true(scene_file.build_settings.meshing_mode == expected_settings.meshing_mode, "scene file should keep the expected meshing mode");
  expect_true(scene_file.build_settings.bounds.min.x == expected_settings.bounds.min.x, "scene bounds min x should match");
  expect_true(scene_file.build_settings.bounds.max.z == expected_settings.bounds.max.z, "scene bounds max z should match");

  for (std::size_t index = 0; index < expected_scene.boxes.size(); ++index)
  {
    expect_same_box(scene_file.scene.boxes[index], expected_scene.boxes[index]);
  }

  for (std::size_t index = 0; index < expected_scene.noise_modifiers.size(); ++index)
  {
    expect_same_noise_modifier(scene_file.scene.noise_modifiers[index], expected_scene.noise_modifiers[index]);
  }

  for (std::size_t index = 0; index < expected_scene.box_cut_modifiers.size(); ++index)
  {
    expect_same_box_cut_modifier(scene_file.scene.box_cut_modifiers[index], expected_scene.box_cut_modifiers[index]);
  }
}

void test_csg_opening_flips_the_sign()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(ok, "sample scene file should load");

  const float tower_mass = sdf::evaluate_scene_sdf(scene_file.scene, {-18.0f, 10.0f, 18.0f});
  const float opening_center = sdf::evaluate_scene_sdf(scene_file.scene, {-18.0f, 20.0f, 18.0f});

  expect_true(tower_mass < 0.0f, "left tower body should remain inside the solid");
  expect_true(opening_center > 0.0f, "subtractive opening should carve a positive pocket");
}

void test_noise_modifier_changes_surface_distance()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(ok, "sample scene file should load");

  sdf::SceneDocument unmodified_scene = scene_file.scene;
  unmodified_scene.noise_modifiers.clear();

  const std::array<sdf::Vec3, 4> probe_points = {{
    {-11.7f, 33.2f, 20.4f},
    {30.2f, 41.1f, 22.7f},
    {27.5f, 39.8f, 4.4f},
    {-24.6f, 34.1f, 13.3f}
  }};

  float max_difference = 0.0f;
  for (const sdf::Vec3 &probe : probe_points)
  {
    const float base_distance = sdf::evaluate_scene_sdf(unmodified_scene, probe);
    const float modified_distance = sdf::evaluate_scene_sdf(scene_file.scene, probe);
    max_difference = std::max(max_difference, std::fabs(modified_distance - base_distance));
  }

  expect_true(max_difference > 0.02f, "noise modifier should change the sampled SDF near masked regions");
}

void test_mesh_generation_produces_triangles()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 2.0f;

  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  expect_true(build.sampled_cells > 0, "mesh build should sample at least one cell");
  expect_true(build.occupied_cells > 0, "mesh build should mark occupied voxels");
  expect_true(!build.mesh.vertices.empty(), "mesh build should emit vertices");
  expect_true(!build.mesh.triangles.empty(), "mesh build should emit triangles");
  expect_true(build.mesh.vertices.size() == build.mesh.triangles.size() * 3, "triangle emission should append three vertices per triangle");

  for (const sdf::MeshTriangle &triangle : build.mesh.triangles)
  {
    expect_true(triangle.i0 < build.mesh.vertices.size(), "triangle i0 should stay within the vertex buffer");
    expect_true(triangle.i1 < build.mesh.vertices.size(), "triangle i1 should stay within the vertex buffer");
    expect_true(triangle.i2 < build.mesh.vertices.size(), "triangle i2 should stay within the vertex buffer");
  }

  for (const sdf::MeshVertex &vertex : build.mesh.vertices)
  {
    const float normal_length = std::sqrt(
      vertex.normal.x * vertex.normal.x +
      vertex.normal.y * vertex.normal.y +
      vertex.normal.z * vertex.normal.z);
    expect_true(normal_length > 0.5f && normal_length < 1.5f, "surface normals should stay normalized");
  }
}

void test_obj_writer_emits_a_file()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 3.0f;

  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);
  const std::filesystem::path output_path = std::filesystem::current_path() / "test_output" / "frame_006_blockout.obj";

  const bool ok = sdf::write_obj(build.mesh, output_path, {}, &error_message);

  expect_true(ok, "obj export should succeed");
  expect_true(std::filesystem::exists(output_path), "obj export should create a file");
  expect_true(std::filesystem::file_size(output_path) > 0, "obj output should not be empty");

  std::ifstream stream(output_path);
  std::string first_line;
  std::getline(stream, first_line);
  expect_true(first_line == "# Generated by sdf_cli", "obj file should start with the generator banner");
}

void test_dual_contouring_generates_indexed_mesh_and_reduces_triangle_count()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  scene_file.build_settings.meshing_mode = sdf::MeshingMode::MarchingTetrahedra;
  const sdf::SceneBuildResult tetra_build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  scene_file.build_settings.meshing_mode = sdf::MeshingMode::DualContouring;
  const sdf::SceneBuildResult dual_build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  expect_true(dual_build.sampled_cells == tetra_build.sampled_cells, "dual contouring should reuse the same sampled cell count");
  expect_true(dual_build.occupied_cells > 0, "dual contouring should report occupied cells");
  expect_true(!dual_build.mesh.vertices.empty(), "dual contouring should emit vertices");
  expect_true(!dual_build.mesh.triangles.empty(), "dual contouring should emit triangles");
  expect_true(
    dual_build.mesh.vertices.size() < dual_build.mesh.triangles.size() * 3,
    "dual contouring should reuse vertices instead of emitting triangle soup");
  expect_true(
    dual_build.mesh.triangles.size() < tetra_build.mesh.triangles.size(),
    "dual contouring should reduce triangle count on the sample scene");

  for (const sdf::MeshTriangle &triangle : dual_build.mesh.triangles)
  {
    expect_true(triangle.i0 < dual_build.mesh.vertices.size(), "dual contouring triangle i0 should stay within the vertex buffer");
    expect_true(triangle.i1 < dual_build.mesh.vertices.size(), "dual contouring triangle i1 should stay within the vertex buffer");
    expect_true(triangle.i2 < dual_build.mesh.vertices.size(), "dual contouring triangle i2 should stay within the vertex buffer");
  }

  for (const sdf::MeshVertex &vertex : dual_build.mesh.vertices)
  {
    const float normal_length = std::sqrt(
      vertex.normal.x * vertex.normal.x +
      vertex.normal.y * vertex.normal.y +
      vertex.normal.z * vertex.normal.z);
    expect_true(normal_length > 0.5f && normal_length < 1.5f, "dual contouring normals should stay normalized");
  }
}

void test_obj_writer_emits_mtl_for_ao_preview()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);
  const std::filesystem::path output_dir = std::filesystem::current_path() / "test_output";
  const std::filesystem::path output_path = output_dir / "frame_006_preview.obj";
  const std::filesystem::path ao_texture_path = output_dir / "frame_006_preview_ao.png";

  {
    std::ofstream texture_stream(ao_texture_path, std::ios::out | std::ios::trunc);
    texture_stream << "placeholder";
  }

  sdf::ObjWriteOptions options;
  options.object_name = "frame_006_blockout";
  options.material_name = "ao_preview";
  options.diffuse_texture_path = ao_texture_path;

  const bool ok = sdf::write_obj(build.mesh, output_path, options, &error_message);
  expect_true(ok, "obj export with ao preview material should succeed");

  const std::filesystem::path material_path = output_dir / "frame_006_preview.mtl";
  expect_true(std::filesystem::exists(material_path), "obj export with ao preview should emit an mtl file");

  std::ifstream obj_stream(output_path);
  std::string obj_contents((std::istreambuf_iterator<char>(obj_stream)), std::istreambuf_iterator<char>());
  expect_true(obj_contents.find("mtllib frame_006_preview.mtl") != std::string::npos, "obj should reference its companion mtl");
  expect_true(obj_contents.find("usemtl ao_preview") != std::string::npos, "obj should bind the ao preview material");

  std::ifstream mtl_stream(material_path);
  std::string mtl_contents((std::istreambuf_iterator<char>(mtl_stream)), std::istreambuf_iterator<char>());
  expect_true(mtl_contents.find("newmtl ao_preview") != std::string::npos, "mtl should declare the preview material");
  expect_true(mtl_contents.find("map_Kd frame_006_preview_ao.png") != std::string::npos, "mtl should bind the ao texture as diffuse");
}

void test_ray_scene_intersection_on_generated_mesh()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 4.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);
  const sdf::RayScene ray_scene = sdf::build_ray_scene(build.mesh);

  expect_true(!ray_scene.triangles.empty(), "ray scene should contain triangles");
  expect_true(!ray_scene.bvh_nodes.empty(), "ray scene should contain BVH nodes");

  const sdf::Vec3 center = (ray_scene.bounds.min + ray_scene.bounds.max) * 0.5f;

  sdf::Ray hit_ray;
  hit_ray.origin = {center.x, center.y, ray_scene.bounds.min.z - 24.0f};
  hit_ray.direction = {0.0f, 0.0f, 1.0f};

  sdf::RayHit hit;
  const bool did_hit = sdf::intersect_ray(ray_scene, hit_ray, 512.0f, &hit);
  expect_true(did_hit, "front-facing probe ray should hit generated geometry");
  expect_true(hit.triangle_index >= 0, "ray hit should return a triangle index");
  expect_true(length_squared(hit.shading_normal) > 0.25f, "ray hit should return a usable shading normal");

  sdf::Ray miss_ray;
  miss_ray.origin = {ray_scene.bounds.max.x + 40.0f, ray_scene.bounds.max.y + 40.0f, ray_scene.bounds.min.z - 24.0f};
  miss_ray.direction = {0.0f, 0.0f, 1.0f};

  sdf::RayHit miss_hit;
  const bool did_miss = sdf::intersect_ray(ray_scene, miss_ray, 512.0f, &miss_hit);
  expect_true(!did_miss, "off-scene probe ray should miss generated geometry");
}

void test_debug_render_writes_png()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 4.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);
  const sdf::RayScene ray_scene = sdf::build_ray_scene(build.mesh);

  sdf::DebugRenderSettings settings;
  settings.width = 64;
  settings.height = 64;
  settings.mode = sdf::DebugRenderMode::Ao;
  settings.camera_preset = sdf::DebugCameraPreset::Front;
  settings.ao_samples = 4;
  settings.ao_max_distance = 8.0f;
  settings.seed = 99;

  sdf::Rgb8Image image;
  const bool render_ok = sdf::render_debug_image(ray_scene, settings, &image, &error_message);
  expect_true(render_ok, "debug render should succeed");
  expect_true(image.width == 64 && image.height == 64, "debug render should preserve requested dimensions");
  expect_true(image.pixels.size() == static_cast<std::size_t>(64 * 64 * 3), "debug render should output RGB pixels");

  const std::filesystem::path output_path = std::filesystem::current_path() / "test_output" / "frame_006_debug_ao.png";
  const bool write_ok = sdf::write_png_rgb8(image, output_path, &error_message);
  expect_true(write_ok, "debug PNG write should succeed");
  expect_true(std::filesystem::exists(output_path), "debug render should produce a PNG file");
  expect_true(std::filesystem::file_size(output_path) > 0, "debug PNG output should not be empty");
}

void test_uv_unwrap_generates_normalized_uvs()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 6.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 512;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult result;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(build.mesh, settings, &unwrapped_mesh, &result, &error_message);

  expect_true(unwrap_ok, "uv unwrap should succeed");
  expect_true(!unwrapped_mesh.vertices.empty(), "uv unwrap should output vertices");
  expect_true(!unwrapped_mesh.triangles.empty(), "uv unwrap should output triangles");
  expect_true(
    unwrapped_mesh.triangles.size() <= build.mesh.triangles.size(),
    "uv unwrap should not increase triangle count");
  expect_true(result.atlas_count == 1, "uv unwrap should keep the sample scene in a single atlas");
  expect_true(result.atlas_width == 512 && result.atlas_height == 512, "uv unwrap should preserve the requested fixed atlas resolution");
  expect_true(result.chart_count > 0, "uv unwrap should create at least one chart");

  for (const sdf::MeshTriangle &triangle : unwrapped_mesh.triangles)
  {
    expect_true(triangle.i0 < unwrapped_mesh.vertices.size(), "unwrapped triangle i0 should stay within the vertex buffer");
    expect_true(triangle.i1 < unwrapped_mesh.vertices.size(), "unwrapped triangle i1 should stay within the vertex buffer");
    expect_true(triangle.i2 < unwrapped_mesh.vertices.size(), "unwrapped triangle i2 should stay within the vertex buffer");
    expect_true(triangle.uv_chart_id >= 0, "unwrapped triangles should preserve a valid chart id");
  }

  for (const sdf::MeshVertex &vertex : unwrapped_mesh.vertices)
  {
    expect_true(vertex.uv0.x >= -0.001f && vertex.uv0.x <= 1.001f, "unwrapped u should stay normalized");
    expect_true(vertex.uv0.y >= -0.001f && vertex.uv0.y <= 1.001f, "unwrapped v should stay normalized");
  }
}

void test_uv_unwrap_rejects_non_power_of_two_resolution()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 300;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(build.mesh, settings, &unwrapped_mesh, nullptr, &error_message);
  expect_true(!unwrap_ok, "uv unwrap should reject non-power-of-two atlas resolutions");
  expect_true(
    error_message.find("power of two") != std::string::npos,
    "uv unwrap should explain that atlas resolution must be a power of two");
}

void test_dual_contouring_uv_unwrap_succeeds()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  scene_file.build_settings.meshing_mode = sdf::MeshingMode::DualContouring;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 256;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult result;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(build.mesh, settings, &unwrapped_mesh, &result, &error_message);

  expect_true(unwrap_ok, "dual contouring mesh should unwrap successfully");
  expect_true(!unwrapped_mesh.vertices.empty(), "dual contouring unwrap should output vertices");
  expect_true(!unwrapped_mesh.triangles.empty(), "dual contouring unwrap should output triangles");
  expect_true(result.atlas_width == 256 && result.atlas_height == 256, "dual contouring unwrap should preserve requested atlas resolution");
}

void test_adaptive_dual_contouring_reduces_triangle_count_further_than_uniform_dual()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");
  expect_true(
    sdf::parse_meshing_mode_name("adaptive_dual_contouring", &scene_file.build_settings.meshing_mode),
    "adaptive dual contouring mode name should parse");

  scene_file.build_settings.cell_size = 4.0f;
  scene_file.build_settings.meshing_mode = sdf::MeshingMode::DualContouring;
  const sdf::SceneBuildResult uniform_dual_build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  scene_file.build_settings.meshing_mode = sdf::MeshingMode::AdaptiveDualContouring;
  const sdf::SceneBuildResult adaptive_build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  expect_true(adaptive_build.sampled_cells == uniform_dual_build.sampled_cells, "adaptive dual contouring should keep the same finest sampled cell count");
  expect_true(adaptive_build.occupied_cells == uniform_dual_build.occupied_cells, "adaptive dual contouring should keep the same occupied finest cell count");
  expect_true(!adaptive_build.mesh.vertices.empty(), "adaptive dual contouring should emit vertices");
  expect_true(!adaptive_build.mesh.triangles.empty(), "adaptive dual contouring should emit triangles");
  expect_true(
    adaptive_build.mesh.triangles.size() < uniform_dual_build.mesh.triangles.size(),
    "adaptive dual contouring should reduce triangle count beyond uniform dual contouring on the sample scene");
  expect_true(
    adaptive_build.mesh.vertices.size() < uniform_dual_build.mesh.vertices.size(),
    "adaptive dual contouring should reduce vertex count beyond uniform dual contouring on the sample scene");
  expect_true(
    adaptive_build.mesh.vertices.size() < adaptive_build.mesh.triangles.size() * 3,
    "adaptive dual contouring should keep indexed vertex reuse");

  for (const sdf::MeshTriangle &triangle : adaptive_build.mesh.triangles)
  {
    expect_true(triangle.i0 < adaptive_build.mesh.vertices.size(), "adaptive dual contouring triangle i0 should stay within the vertex buffer");
    expect_true(triangle.i1 < adaptive_build.mesh.vertices.size(), "adaptive dual contouring triangle i1 should stay within the vertex buffer");
    expect_true(triangle.i2 < adaptive_build.mesh.vertices.size(), "adaptive dual contouring triangle i2 should stay within the vertex buffer");
  }

  for (const sdf::MeshVertex &vertex : adaptive_build.mesh.vertices)
  {
    const float normal_length = std::sqrt(
      vertex.normal.x * vertex.normal.x +
      vertex.normal.y * vertex.normal.y +
      vertex.normal.z * vertex.normal.z);
    expect_true(normal_length > 0.5f && normal_length < 1.5f, "adaptive dual contouring normals should stay normalized");
  }
}

void test_adaptive_dual_contouring_uv_unwrap_succeeds()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 4.0f;
  scene_file.build_settings.meshing_mode = sdf::MeshingMode::AdaptiveDualContouring;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 256;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult result;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(build.mesh, settings, &unwrapped_mesh, &result, &error_message);

  expect_true(unwrap_ok, "adaptive dual contouring mesh should unwrap successfully");
  expect_true(!unwrapped_mesh.vertices.empty(), "adaptive dual contouring unwrap should output vertices");
  expect_true(!unwrapped_mesh.triangles.empty(), "adaptive dual contouring unwrap should output triangles");
  expect_true(result.atlas_width == 256 && result.atlas_height == 256, "adaptive dual contouring unwrap should preserve requested atlas resolution");
}

void test_uv_unwrap_welded_input_reduces_topology_size()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 256;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult result;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(
    build.mesh,
    settings,
    &unwrapped_mesh,
    &result,
    &error_message);
  expect_true(unwrap_ok, "welded uv unwrap should succeed");
  expect_true(
    unwrapped_mesh.vertices.size() < build.mesh.vertices.size(),
    "welded uv unwrap should reduce the exported vertex count");
  expect_true(
    unwrapped_mesh.triangles.size() < build.mesh.triangles.size(),
    "welded uv unwrap should reduce the exported triangle count on the sample scene");
}

void test_uv_unwrap_chart_debug_image_and_fragmentation_stats()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings settings;
  settings.resolution = 256;
  settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult result;
  sdf::Rgb8Image chart_debug_image;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(
    build.mesh,
    settings,
    &unwrapped_mesh,
    &result,
    &error_message,
    {},
    &chart_debug_image);

  expect_true(unwrap_ok, "uv unwrap with chart debug image should succeed");
  expect_true(chart_debug_image.width == static_cast<int>(result.atlas_width), "chart debug image should match atlas width");
  expect_true(chart_debug_image.height == static_cast<int>(result.atlas_height), "chart debug image should match atlas height");
  expect_true(
    chart_debug_image.pixels.size() == static_cast<std::size_t>(chart_debug_image.width * chart_debug_image.height * 3),
    "chart debug image should contain RGB pixels");
  expect_true(result.min_chart_triangle_count > 0, "chart triangle stats should be populated");
  expect_true(result.max_chart_triangle_count >= result.min_chart_triangle_count, "chart triangle min/max should be ordered");
  expect_true(result.average_chart_triangle_count >= static_cast<float>(result.min_chart_triangle_count), "average chart triangle count should be sensible");
  expect_true(result.single_triangle_chart_count <= result.chart_count, "single-triangle chart count should stay bounded");
  expect_true(result.chart_texel_count > 0, "chart texel coverage should be populated");
  expect_true(result.max_chart_texel_count >= result.min_chart_texel_count, "chart texel min/max should be ordered");
  expect_true(result.average_chart_texel_count > 0.0f, "average chart texel count should be populated");

  std::size_t non_black_pixels = 0;
  for (unsigned char value : chart_debug_image.pixels)
  {
    non_black_pixels += value != 0 ? 1u : 0u;
  }
  expect_true(non_black_pixels > 0, "chart debug image should contain visible chart colors");
}

void test_ao_bake_writes_png()
{
  sdf::SceneFile scene_file;
  std::string error_message;
  const bool load_ok = sdf::load_scene_file(sample_scene_path(), &scene_file, &error_message);

  expect_true(load_ok, "sample scene file should load");

  scene_file.build_settings.cell_size = 8.0f;
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  sdf::UvUnwrapSettings unwrap_settings;
  unwrap_settings.resolution = 128;
  unwrap_settings.padding = 4;

  sdf::Mesh unwrapped_mesh;
  sdf::UvUnwrapResult unwrap_result;
  const bool unwrap_ok = sdf::unwrap_mesh_uvs(build.mesh, unwrap_settings, &unwrapped_mesh, &unwrap_result, &error_message);
  expect_true(unwrap_ok, "uv unwrap should succeed before ao baking");
  expect_true(unwrap_result.atlas_width == 128 && unwrap_result.atlas_height == 128, "ao bake unwrap should preserve the requested fixed atlas resolution");

  const sdf::RayScene ray_scene = sdf::build_ray_scene(build.mesh);

  sdf::AoBakeSettings bake_settings;
  bake_settings.width = static_cast<int>(unwrap_result.atlas_width);
  bake_settings.height = static_cast<int>(unwrap_result.atlas_height);
  bake_settings.min_ao_samples = 4;
  bake_settings.max_ao_samples = 8;
  bake_settings.denoise_passes = 1;
  bake_settings.denoise_radius = 1;
  bake_settings.ao_error_threshold = 1.0f;
  bake_settings.ao_max_distance = 8.0f;
  bake_settings.seed = 123;

  sdf::Rgb8Image image;
  sdf::AoBakeResult bake_result;
  const bool bake_ok = sdf::bake_ambient_occlusion_texture(
    unwrapped_mesh,
    ray_scene,
    bake_settings,
    &image,
    &bake_result,
    &error_message);

  expect_true(bake_ok, "ao bake should succeed");
  expect_true(image.width == bake_settings.width, "ao bake should preserve bake width");
  expect_true(image.height == bake_settings.height, "ao bake should preserve bake height");
  expect_true(bake_result.baked_texels > 0, "ao bake should cover at least one texel");
  expect_true(bake_result.covered_texels >= bake_result.baked_texels, "dilation should never reduce texel coverage");
  expect_true(bake_result.ao_ray_count >= bake_result.baked_texels * 4, "ao bake should trace at least the minimum sample count");
  expect_true(
    bake_result.average_ao_samples_per_baked_texel >= 4.0f &&
    bake_result.average_ao_samples_per_baked_texel <= 8.0f,
    "ao bake average samples should stay within the adaptive sampling bounds");
  expect_true(
    std::fabs(bake_result.average_ao_samples_per_baked_texel - 4.0f) < 0.01f,
    "very loose adaptive error threshold should stop at the minimum sample count");
  expect_true(bake_result.denoise_passes == 1, "ao bake should report the configured denoise pass count");
  expect_true(bake_result.denoise_radius == 1, "ao bake should report the configured denoise radius");
  expect_true(bake_result.dilation_passes >= 16, "ao bake auto dilation should use a non-trivial pass count");

  std::size_t non_black_pixels = 0;
  for (unsigned char value : image.pixels)
  {
    non_black_pixels += value != 0 ? 1u : 0u;
  }
  expect_true(non_black_pixels > 0, "ao bake should produce non-black pixels");

  const std::filesystem::path output_path = std::filesystem::current_path() / "test_output" / "frame_006_bake_ao.png";
  const bool write_ok = sdf::write_png_rgb8(image, output_path, &error_message);
  expect_true(write_ok, "ao bake PNG write should succeed");
  expect_true(std::filesystem::exists(output_path), "ao bake should produce a PNG file");
  expect_true(std::filesystem::file_size(output_path) > 0, "ao bake PNG output should not be empty");
}

void test_invalid_scene_file_reports_an_error()
{
  const std::filesystem::path invalid_path = std::filesystem::current_path() / "test_output" / "invalid_scene_missing_bounds.sdfscene";
  std::filesystem::create_directories(invalid_path.parent_path());

  {
    std::ofstream stream(invalid_path);
    stream << "scene broken\n";
    stream << "cell_size 1.0\n";
    stream << "box add orphan 0 0 0 1 1 1\n";
  }

  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(invalid_path, &scene_file, &error_message);

  expect_true(!ok, "invalid scene file should be rejected");
  expect_true(error_message.find("bounds") != std::string::npos, "invalid scene file error should mention missing bounds");
}

void test_invalid_modifier_target_reports_an_error()
{
  const std::filesystem::path invalid_path = std::filesystem::current_path() / "test_output" / "invalid_scene_bad_modifier_target.sdfscene";
  std::filesystem::create_directories(invalid_path.parent_path());

  {
    std::ofstream stream(invalid_path);
    stream << "scene broken_modifier_target\n";
    stream << "bounds -1 -1 -1 1 1 1\n";
    stream << "cell_size 1.0\n";
    stream << "box add box_a 0 0 0 1 1 1\n";
    stream << "noise_displace_masked decay missing_box 0.5 0.25 1 2 top 1.0\n";
  }

  sdf::SceneFile scene_file;
  std::string error_message;
  const bool ok = sdf::load_scene_file(invalid_path, &scene_file, &error_message);

  expect_true(!ok, "modifier target validation should reject unknown boxes");
  expect_true(error_message.find("missing_box") != std::string::npos, "modifier target error should mention the missing box name");
}

}  // namespace

int main()
{
  try
  {
    test_frame_scene_layout();
    test_csg_opening_flips_the_sign();
    test_noise_modifier_changes_surface_distance();
    test_mesh_generation_produces_triangles();
    test_obj_writer_emits_a_file();
    test_dual_contouring_generates_indexed_mesh_and_reduces_triangle_count();
    test_obj_writer_emits_mtl_for_ao_preview();
    test_ray_scene_intersection_on_generated_mesh();
    test_debug_render_writes_png();
    test_uv_unwrap_generates_normalized_uvs();
    test_uv_unwrap_rejects_non_power_of_two_resolution();
    test_dual_contouring_uv_unwrap_succeeds();
    test_adaptive_dual_contouring_reduces_triangle_count_further_than_uniform_dual();
    test_adaptive_dual_contouring_uv_unwrap_succeeds();
    test_uv_unwrap_welded_input_reduces_topology_size();
    test_uv_unwrap_chart_debug_image_and_fragmentation_stats();
    test_ao_bake_writes_png();
    test_invalid_scene_file_reports_an_error();
    test_invalid_modifier_target_reports_an_error();
  }
  catch (const std::exception &exception)
  {
    std::cerr << "Test failure: " << exception.what() << '\n';
    return 1;
  }

  std::cout << "All sdf tests passed.\n";
  return 0;
}
