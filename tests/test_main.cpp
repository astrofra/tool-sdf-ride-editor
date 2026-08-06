#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sdf/debug_render.h"
#include "sdf/generator.h"
#include "sdf/image_write.h"
#include "sdf/obj_writer.h"
#include "sdf/raytrace.h"
#include "sdf/scene.h"
#include "sdf/scene_io.h"

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

  const bool ok = sdf::write_obj(build.mesh, output_path, &error_message);

  expect_true(ok, "obj export should succeed");
  expect_true(std::filesystem::exists(output_path), "obj export should create a file");
  expect_true(std::filesystem::file_size(output_path) > 0, "obj output should not be empty");

  std::ifstream stream(output_path);
  std::string first_line;
  std::getline(stream, first_line);
  expect_true(first_line == "# Generated by sdf_cli", "obj file should start with the generator banner");
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
    test_ray_scene_intersection_on_generated_mesh();
    test_debug_render_writes_png();
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
