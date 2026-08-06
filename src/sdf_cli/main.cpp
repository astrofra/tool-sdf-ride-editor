#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "sdf/generator.h"
#include "sdf/obj_writer.h"
#include "sdf/scene.h"

namespace
{

void print_usage()
{
  std::cout << "Usage: sdf_cli [--out PATH] [--cell-size VALUE]\n";
}

}  // namespace

int main(int argc, char **argv)
{
  std::filesystem::path output_path = "artifacts/generated/frame_006_blockout.obj";
  sdf::BuildSettings settings = sdf::make_frame_006_build_settings();

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];

    if (argument == "--help" || argument == "-h")
    {
      print_usage();
      return 0;
    }

    if (argument == "--out" && index + 1 < argc)
    {
      output_path = argv[++index];
      continue;
    }

    if (argument == "--cell-size" && index + 1 < argc)
    {
      settings.cell_size = std::stof(argv[++index]);
      continue;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    print_usage();
    return 1;
  }

  const sdf::SceneDocument scene = sdf::make_frame_006_blockout_scene();
  const sdf::SceneBuildResult build = sdf::build_scene_mesh(scene, settings);

  std::string error_message;
  if (!sdf::write_obj(build.mesh, output_path, &error_message))
  {
    std::cerr << "OBJ export failed: " << error_message << '\n';
    return 1;
  }

  std::cout << "Scene: " << scene.name << '\n';
  std::cout << "Boxes: " << scene.boxes.size() << '\n';
  std::cout << "Sampled cells: " << build.sampled_cells << '\n';
  std::cout << "Occupied cells: " << build.occupied_cells << '\n';
  std::cout << "Vertices: " << build.mesh.vertices.size() << '\n';
  std::cout << "Triangles: " << build.mesh.triangles.size() << '\n';
  std::cout << "OBJ: " << output_path.string() << '\n';

  return 0;
}

