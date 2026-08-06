#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "sdf/generator.h"
#include "sdf/obj_writer.h"
#include "sdf/scene.h"
#include "sdf/scene_io.h"

namespace
{

#ifndef SDF_PROJECT_SOURCE_DIR
#define SDF_PROJECT_SOURCE_DIR "."
#endif

std::filesystem::path default_scene_path()
{
  return std::filesystem::path(SDF_PROJECT_SOURCE_DIR) / "scenes" / "frame_006_blockout.sdfscene";
}

void print_usage()
{
  std::cout << "Usage: sdf_cli [--scene PATH] [--out PATH] [--cell-size VALUE]\n";
}

}  // namespace

int main(int argc, char **argv)
{
  std::filesystem::path scene_path = default_scene_path();
  std::filesystem::path output_path = "artifacts/generated/frame_006_blockout.obj";
  bool has_cell_size_override = false;
  float cell_size_override = 0.0f;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];

    if (argument == "--help" || argument == "-h")
    {
      print_usage();
      return 0;
    }

    if (argument == "--scene" && index + 1 < argc)
    {
      scene_path = argv[++index];
      continue;
    }

    if (argument == "--out" && index + 1 < argc)
    {
      output_path = argv[++index];
      continue;
    }

    if (argument == "--cell-size" && index + 1 < argc)
    {
      cell_size_override = std::stof(argv[++index]);
      has_cell_size_override = true;
      continue;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    print_usage();
    return 1;
  }

  sdf::SceneFile scene_file;
  std::string error_message;
  if (!sdf::load_scene_file(scene_path, &scene_file, &error_message))
  {
    std::cerr << "Scene load failed: " << error_message << '\n';
    return 1;
  }

  if (has_cell_size_override)
  {
    scene_file.build_settings.cell_size = cell_size_override;
  }

  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings);

  if (!sdf::write_obj(build.mesh, output_path, &error_message))
  {
    std::cerr << "OBJ export failed: " << error_message << '\n';
    return 1;
  }

  std::cout << "Scene file: " << scene_path.string() << '\n';
  std::cout << "Scene: " << scene_file.scene.name << '\n';
  std::cout << "Boxes: " << scene_file.scene.boxes.size() << '\n';
  std::cout << "Cell size: " << scene_file.build_settings.cell_size << '\n';
  std::cout << "Sampled cells: " << build.sampled_cells << '\n';
  std::cout << "Occupied cells: " << build.occupied_cells << '\n';
  std::cout << "Vertices: " << build.mesh.vertices.size() << '\n';
  std::cout << "Triangles: " << build.mesh.triangles.size() << '\n';
  std::cout << "OBJ: " << output_path.string() << '\n';

  return 0;
}
