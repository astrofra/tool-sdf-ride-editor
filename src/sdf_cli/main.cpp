#include <cstdlib>
#include <filesystem>
#include <iostream>
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

std::filesystem::path default_scene_path()
{
  return std::filesystem::path(SDF_PROJECT_SOURCE_DIR) / "scenes" / "frame_006_blockout.sdfscene";
}

bool parse_debug_mode(const std::string &text, sdf::DebugRenderMode *mode)
{
  if (text == "depth")
  {
    *mode = sdf::DebugRenderMode::Depth;
    return true;
  }
  if (text == "normal")
  {
    *mode = sdf::DebugRenderMode::Normal;
    return true;
  }
  if (text == "ao")
  {
    *mode = sdf::DebugRenderMode::Ao;
    return true;
  }
  return false;
}

void print_usage()
{
  std::cout
    << "Usage: sdf_cli [--scene PATH] [--out PATH] [--cell-size VALUE]\n"
    << "               [--unwrap-uvs] [--uv-resolution N] [--uv-padding N]\n"
    << "               [--bake-ao PATH] [--bake-ao-samples N] [--bake-ao-max-distance F]\n"
    << "               [--bake-ao-dilation N]\n"
    << "               [--debug-render PATH] [--debug-mode depth|normal|ao]\n"
    << "               [--render-width N] [--render-height N]\n"
    << "               [--camera-front | --camera-left-3q | --camera-right-3q]\n"
    << "               [--ao-samples N] [--ao-max-distance F]\n";
}

}  // namespace

int main(int argc, char **argv)
{
  std::filesystem::path scene_path = default_scene_path();
  std::filesystem::path output_path = "artifacts/generated/frame_006_blockout.obj";
  std::filesystem::path debug_render_path;
  std::filesystem::path bake_ao_path;
  bool has_cell_size_override = false;
  float cell_size_override = 0.0f;
  bool unwrap_uvs = false;
  sdf::DebugRenderSettings debug_settings;
  sdf::UvUnwrapSettings unwrap_settings;
  sdf::AoBakeSettings bake_settings;

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

    if (argument == "--debug-render" && index + 1 < argc)
    {
      debug_render_path = argv[++index];
      continue;
    }

    if (argument == "--bake-ao" && index + 1 < argc)
    {
      bake_ao_path = argv[++index];
      continue;
    }

    if (argument == "--unwrap-uvs")
    {
      unwrap_uvs = true;
      continue;
    }

    if (argument == "--debug-mode" && index + 1 < argc)
    {
      if (!parse_debug_mode(argv[++index], &debug_settings.mode))
      {
        std::cerr << "Unknown debug mode.\n";
        print_usage();
        return 1;
      }
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

    if (argument == "--uv-resolution" && index + 1 < argc)
    {
      unwrap_settings.resolution = static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }

    if (argument == "--uv-padding" && index + 1 < argc)
    {
      unwrap_settings.padding = static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }

    if (argument == "--bake-ao-samples" && index + 1 < argc)
    {
      bake_settings.ao_samples = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-max-distance" && index + 1 < argc)
    {
      bake_settings.ao_max_distance = std::stof(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-dilation" && index + 1 < argc)
    {
      bake_settings.dilation_passes = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--render-width" && index + 1 < argc)
    {
      debug_settings.width = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--render-height" && index + 1 < argc)
    {
      debug_settings.height = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--camera-front")
    {
      debug_settings.camera_preset = sdf::DebugCameraPreset::Front;
      continue;
    }

    if (argument == "--camera-left-3q")
    {
      debug_settings.camera_preset = sdf::DebugCameraPreset::LeftThreeQuarter;
      continue;
    }

    if (argument == "--camera-right-3q")
    {
      debug_settings.camera_preset = sdf::DebugCameraPreset::RightThreeQuarter;
      continue;
    }

    if (argument == "--ao-samples" && index + 1 < argc)
    {
      debug_settings.ao_samples = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--ao-max-distance" && index + 1 < argc)
    {
      debug_settings.ao_max_distance = std::stof(argv[++index]);
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
  sdf::Mesh export_mesh = build.mesh;
  sdf::UvUnwrapResult unwrap_result;
  const bool needs_uv_unwrap = unwrap_uvs || !bake_ao_path.empty();

  if (needs_uv_unwrap)
  {
    if (!sdf::unwrap_mesh_uvs(build.mesh, unwrap_settings, &export_mesh, &unwrap_result, &error_message))
    {
      std::cerr << "UV unwrap failed: " << error_message << '\n';
      return 1;
    }
  }

  const sdf::RayScene ray_scene = sdf::build_ray_scene(build.mesh);

  if (!sdf::write_obj(export_mesh, output_path, &error_message))
  {
    std::cerr << "OBJ export failed: " << error_message << '\n';
    return 1;
  }

  if (!debug_render_path.empty())
  {
    sdf::Rgb8Image image;
    if (!sdf::render_debug_image(ray_scene, debug_settings, &image, &error_message))
    {
      std::cerr << "Debug render failed: " << error_message << '\n';
      return 1;
    }

    if (!sdf::write_png_rgb8(image, debug_render_path, &error_message))
    {
      std::cerr << "Debug image write failed: " << error_message << '\n';
      return 1;
    }
  }

  sdf::AoBakeResult bake_result;
  if (!bake_ao_path.empty())
  {
    bake_settings.width = static_cast<int>(unwrap_result.atlas_width);
    bake_settings.height = static_cast<int>(unwrap_result.atlas_height);
    bake_settings.dilation_passes = std::max(
      bake_settings.dilation_passes,
      static_cast<int>(unwrap_settings.padding));

    sdf::Rgb8Image image;
    if (!sdf::bake_ambient_occlusion_texture(export_mesh, ray_scene, bake_settings, &image, &bake_result, &error_message))
    {
      std::cerr << "AO bake failed: " << error_message << '\n';
      return 1;
    }

    if (!sdf::write_png_rgb8(image, bake_ao_path, &error_message))
    {
      std::cerr << "AO bake image write failed: " << error_message << '\n';
      return 1;
    }
  }

  std::cout << "Scene file: " << scene_path.string() << '\n';
  std::cout << "Scene: " << scene_file.scene.name << '\n';
  std::cout << "Boxes: " << scene_file.scene.boxes.size() << '\n';
  std::cout << "Cell size: " << scene_file.build_settings.cell_size << '\n';
  std::cout << "Sampled cells: " << build.sampled_cells << '\n';
  std::cout << "Occupied cells: " << build.occupied_cells << '\n';
  std::cout << "Generated vertices: " << build.mesh.vertices.size() << '\n';
  std::cout << "Generated triangles: " << build.mesh.triangles.size() << '\n';
  std::cout << "Export vertices: " << export_mesh.vertices.size() << '\n';
  std::cout << "Export triangles: " << export_mesh.triangles.size() << '\n';
  if (needs_uv_unwrap)
  {
    std::cout << "UV unwrap: enabled\n";
    std::cout << "UV charts: " << unwrap_result.chart_count << '\n';
    std::cout << "UV atlas pages: " << unwrap_result.atlas_count << '\n';
    std::cout << "UV atlas resolution: " << unwrap_result.atlas_width << 'x' << unwrap_result.atlas_height << '\n';
    std::cout << "UV atlas utilization: " << unwrap_result.utilization * 100.0f << "%\n";
    std::cout << "UV texels per unit: " << unwrap_result.texels_per_unit << '\n';
  }
  std::cout << "OBJ: " << output_path.string() << '\n';
  if (!debug_render_path.empty())
  {
    std::cout << "Debug image: " << debug_render_path.string() << '\n';
  }
  if (!bake_ao_path.empty())
  {
    std::cout << "AO bake: " << bake_ao_path.string() << '\n';
    std::cout << "AO baked texels: " << bake_result.baked_texels << '\n';
    std::cout << "AO dilated texels: " << bake_result.dilated_texels << '\n';
    std::cout << "AO covered texels: " << bake_result.covered_texels << '\n';
  }

  return 0;
}
