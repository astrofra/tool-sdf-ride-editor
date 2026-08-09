#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sdf/bake_ao.h"
#include "sdf/bake_normal.h"
#include "sdf/bake_surface_pack.h"
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
  return std::filesystem::path(SDF_PROJECT_SOURCE_DIR) / "app" / "sdf-scenes" / "frame_006_blockout.sdfscene";
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

class TimingSummary
{
public:
  void add_sample(const std::string &name, double seconds)
  {
    if (name.empty())
    {
      return;
    }

    std::scoped_lock lock(mutex_);
    const auto it = indices_.find(name);
    if (it == indices_.end())
    {
      indices_.emplace(name, entries_.size());
      entries_.push_back(Entry{name, seconds, 1u});
      return;
    }

    Entry &entry = entries_[it->second];
    entry.seconds += seconds;
    entry.occurrences += 1u;
  }

  void print(const std::string &header) const
  {
    std::scoped_lock lock(mutex_);
    if (entries_.empty())
    {
      return;
    }

    const std::ios::fmtflags old_flags = std::cout.flags();
    const std::streamsize old_precision = std::cout.precision();

    std::cout << header << '\n';
    std::cout << std::fixed << std::setprecision(3);

    double total = 0.0;
    for (const Entry &entry : entries_)
    {
      std::cout << "  " << entry.name << ": " << entry.seconds << " s";
      if (entry.occurrences > 1u)
      {
        std::cout << " (" << entry.occurrences << "x)";
      }
      std::cout << '\n';
      total += entry.seconds;
    }

    std::cout << "  Total: " << total << " s\n";
    std::cout.flags(old_flags);
    std::cout.precision(old_precision);
  }

private:
  struct Entry
  {
    std::string name;
    double seconds = 0.0;
    std::size_t occurrences = 0;
  };

  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
  std::unordered_map<std::string, std::size_t> indices_;
};

double elapsed_seconds(
  const std::chrono::steady_clock::time_point &start,
  const std::chrono::steady_clock::time_point &end = std::chrono::steady_clock::now())
{
  return std::chrono::duration<double>(end - start).count();
}

class ScopedPhaseTimer
{
public:
  ScopedPhaseTimer(TimingSummary *summary, std::string name)
    : summary_(summary), name_(std::move(name)), start_(std::chrono::steady_clock::now())
  {
  }

  ~ScopedPhaseTimer()
  {
    if (summary_ != nullptr)
    {
      summary_->add_sample(name_, elapsed_seconds(start_));
    }
  }

private:
  TimingSummary *summary_ = nullptr;
  std::string name_;
  std::chrono::steady_clock::time_point start_;
};

class ConsoleProgressPrinter
{
public:
  void print(const sdf::ProgressUpdate &update)
  {
    std::scoped_lock lock(mutex_);
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    const bool stage_changed = current_stage_ != update.stage;
    if (stage_changed)
    {
      finish_current_stage_locked(now);
    }

    if (stage_changed && line_open_)
    {
      std::cout << '\n';
      line_open_ = false;
    }

    if (stage_changed)
    {
      current_stage_ = std::string(update.stage);
      current_stage_start_ = now;
      has_current_stage_ = true;
    }

    std::cout << '\r' << "[progress] " << current_stage_ << ": " << update.percent << '%';
    std::cout.flush();

    if (update.completed >= update.total)
    {
      std::cout << '\n';
      finish_current_stage_locked(now);
      line_open_ = false;
      return;
    }

    line_open_ = true;
  }

  void finish_active_stage()
  {
    std::scoped_lock lock(mutex_);
    finish_current_stage_locked(std::chrono::steady_clock::now());
    if (line_open_)
    {
      std::cout << '\n';
      line_open_ = false;
    }
  }

  void print_timing_summary()
  {
    finish_active_stage();
    stage_timings_.print("Progress Stage Timings");
  }

private:
  void finish_current_stage_locked(const std::chrono::steady_clock::time_point &now)
  {
    if (!has_current_stage_)
    {
      return;
    }

    stage_timings_.add_sample(current_stage_, elapsed_seconds(current_stage_start_, now));
    current_stage_.clear();
    has_current_stage_ = false;
  }

  std::mutex mutex_;
  TimingSummary stage_timings_;
  std::string current_stage_;
  std::chrono::steady_clock::time_point current_stage_start_;
  bool has_current_stage_ = false;
  bool line_open_ = false;
};

void print_usage()
{
  std::cout
    << "Usage: sdf_cli [--scene PATH] [--out PATH] [--cell-size VALUE] [--meshing-mode MODE]\n"
    << "               [--unwrap-uvs] [--uv-resolution N] [--uv-padding N]\n"
    << "               [--debug-uv-charts PATH]\n"
    << "               [--bake-ao PATH] [--bake-ao-samples N] [--bake-ao-min-samples N]\n"
    << "               [--bake-ao-error-threshold F] [--bake-ao-max-distance F]\n"
    << "               [--bake-ao-denoise-passes N] [--bake-ao-denoise-radius N]\n"
    << "               [--bake-ao-dilation N]\n"
    << "               [--bake-normal PATH] [--bake-normal-dilation N]\n"
    << "               [--bake-surface-pack PATH] [--surface-thickness-max-distance F]\n"
    << "               [--debug-render PATH] [--debug-mode depth|normal|ao]\n"
    << "               [--render-width N] [--render-height N]\n"
    << "               [--camera-front | --camera-left-3q | --camera-right-3q]\n"
    << "               [--ao-samples N] [--ao-max-distance F]\n"
    << "               [--adaptive-normal-dot-threshold F]\n"
    << "               [--adaptive-plane-error-ratio F]\n"
    << "               [--adaptive-min-plane-error-cells F]\n"
    << "\n"
    << "Notes:\n"
    << "  --meshing-mode accepts 'marching_tetrahedra', 'dual_contouring', or 'adaptive_dual_contouring'.\n"
    << "  --bake-normal outputs a tangent-space normal map sampled from the original SDF.\n"
    << "  --bake-surface-pack outputs a packed RGB PNG with R=AO, G=curvature, B=thickness.\n"
    << "  --uv-resolution must be a power of two and is treated as an exact atlas size.\n";
}

}  // namespace

int main(int argc, char **argv)
{
  const std::chrono::steady_clock::time_point wall_clock_start = std::chrono::steady_clock::now();
  std::filesystem::path scene_path = default_scene_path();
  std::filesystem::path output_path = "artifacts/generated/frame_006_blockout.obj";
  std::filesystem::path debug_render_path;
  std::filesystem::path debug_uv_charts_path;
  std::filesystem::path bake_ao_path;
  std::filesystem::path bake_normal_path;
  std::filesystem::path bake_surface_pack_path;
  bool has_cell_size_override = false;
  float cell_size_override = 0.0f;
  bool has_meshing_mode_override = false;
  sdf::MeshingMode meshing_mode_override = sdf::MeshingMode::MarchingTetrahedra;
  bool has_adaptive_normal_dot_threshold_override = false;
  float adaptive_normal_dot_threshold_override = 0.0f;
  bool has_adaptive_plane_error_ratio_override = false;
  float adaptive_plane_error_ratio_override = 0.0f;
  bool has_adaptive_min_plane_error_in_cells_override = false;
  float adaptive_min_plane_error_in_cells_override = 0.0f;
  bool unwrap_uvs = false;
  sdf::DebugRenderSettings debug_settings;
  sdf::UvUnwrapSettings unwrap_settings;
  sdf::AoBakeSettings bake_settings;
  sdf::NormalBakeSettings normal_bake_settings;
  sdf::SurfacePackSettings surface_pack_settings;
  TimingSummary phase_timings;
  ConsoleProgressPrinter progress_printer;

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

    if (argument == "--bake-normal" && index + 1 < argc)
    {
      bake_normal_path = argv[++index];
      continue;
    }

    if (argument == "--bake-surface-pack" && index + 1 < argc)
    {
      bake_surface_pack_path = argv[++index];
      continue;
    }

    if (argument == "--debug-uv-charts" && index + 1 < argc)
    {
      debug_uv_charts_path = argv[++index];
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

    if (argument == "--meshing-mode" && index + 1 < argc)
    {
      if (!sdf::parse_meshing_mode_name(argv[++index], &meshing_mode_override))
      {
        std::cerr << "Unknown meshing mode.\n";
        print_usage();
        return 1;
      }
      has_meshing_mode_override = true;
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
      bake_settings.max_ao_samples = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-min-samples" && index + 1 < argc)
    {
      bake_settings.min_ao_samples = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-error-threshold" && index + 1 < argc)
    {
      bake_settings.ao_error_threshold = std::stof(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-max-distance" && index + 1 < argc)
    {
      bake_settings.ao_max_distance = std::stof(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-denoise-passes" && index + 1 < argc)
    {
      bake_settings.denoise_passes = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-denoise-radius" && index + 1 < argc)
    {
      bake_settings.denoise_radius = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-ao-dilation" && index + 1 < argc)
    {
      bake_settings.dilation_passes = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--bake-normal-dilation" && index + 1 < argc)
    {
      normal_bake_settings.dilation_passes = std::stoi(argv[++index]);
      continue;
    }

    if (argument == "--surface-thickness-max-distance" && index + 1 < argc)
    {
      surface_pack_settings.thickness_max_distance = std::stof(argv[++index]);
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

    if (argument == "--adaptive-normal-dot-threshold" && index + 1 < argc)
    {
      adaptive_normal_dot_threshold_override = std::stof(argv[++index]);
      has_adaptive_normal_dot_threshold_override = true;
      continue;
    }

    if (argument == "--adaptive-plane-error-ratio" && index + 1 < argc)
    {
      adaptive_plane_error_ratio_override = std::stof(argv[++index]);
      has_adaptive_plane_error_ratio_override = true;
      continue;
    }

    if (argument == "--adaptive-min-plane-error-cells" && index + 1 < argc)
    {
      adaptive_min_plane_error_in_cells_override = std::stof(argv[++index]);
      has_adaptive_min_plane_error_in_cells_override = true;
      continue;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    print_usage();
    return 1;
  }

  const auto print_timing_summary =
    [&]()
    {
      progress_printer.finish_active_stage();
      std::cout << "Timing Summary\n";
      phase_timings.print("Phase Timings");
      progress_printer.print_timing_summary();

      const std::ios::fmtflags old_flags = std::cout.flags();
      const std::streamsize old_precision = std::cout.precision();
      std::cout << std::fixed << std::setprecision(3);
      std::cout << "Wall Time: " << elapsed_seconds(wall_clock_start) << " s\n";
      std::cout.flags(old_flags);
      std::cout.precision(old_precision);
    };

  const auto fail_with_timings =
    [&](const std::string &message)
    {
      std::cerr << message << '\n';
      print_timing_summary();
      return 1;
    };

  sdf::SceneFile scene_file;
  std::string error_message;
  bool scene_loaded = false;
  {
    ScopedPhaseTimer phase_timer(&phase_timings, "Load scene file");
    scene_loaded = sdf::load_scene_file(scene_path, &scene_file, &error_message);
  }
  if (!scene_loaded)
  {
    return fail_with_timings("Scene load failed: " + error_message);
  }

  if (has_cell_size_override)
  {
    scene_file.build_settings.cell_size = cell_size_override;
  }
  if (has_meshing_mode_override)
  {
    scene_file.build_settings.meshing_mode = meshing_mode_override;
  }
  if (has_adaptive_normal_dot_threshold_override)
  {
    scene_file.build_settings.adaptive_normal_dot_threshold = adaptive_normal_dot_threshold_override;
  }
  if (has_adaptive_plane_error_ratio_override)
  {
    scene_file.build_settings.adaptive_plane_error_ratio = adaptive_plane_error_ratio_override;
  }
  if (has_adaptive_min_plane_error_in_cells_override)
  {
    scene_file.build_settings.adaptive_min_plane_error_in_cells = adaptive_min_plane_error_in_cells_override;
  }

  const sdf::ProgressCallback progress_callback =
    [&](const sdf::ProgressUpdate &update)
    {
      progress_printer.print(update);
    };

  sdf::SceneBuildResult build;
  {
    ScopedPhaseTimer phase_timer(&phase_timings, "Build scene mesh");
    build = sdf::build_scene_mesh(scene_file.scene, scene_file.build_settings, progress_callback);
  }

  sdf::Mesh export_mesh = build.mesh;
  sdf::UvUnwrapResult unwrap_result;
  sdf::Rgb8Image uv_chart_debug_image;
  const bool needs_ao_bake = !bake_ao_path.empty() || !bake_surface_pack_path.empty();
  const bool needs_uv_unwrap =
    unwrap_uvs ||
    needs_ao_bake ||
    !bake_normal_path.empty() ||
    !bake_surface_pack_path.empty() ||
    !debug_uv_charts_path.empty();
  const bool needs_ray_scene = !debug_render_path.empty() || needs_ao_bake;

  sdf::ObjWriteOptions obj_write_options;
  obj_write_options.object_name = scene_file.scene.name;
  if (!bake_ao_path.empty())
  {
    obj_write_options.diffuse_texture_path = bake_ao_path;
  }

  sdf::AoBakeResult bake_result;
  sdf::NormalBakeResult normal_bake_result;
  sdf::SurfacePackResult surface_pack_result;
  sdf::Rgb8Image ao_image;
  if (needs_uv_unwrap)
  {
    bool unwrap_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "UV unwrap");
      unwrap_ok = sdf::unwrap_mesh_uvs(
        build.mesh,
        unwrap_settings,
        &export_mesh,
        &unwrap_result,
        &error_message,
        progress_callback,
        !debug_uv_charts_path.empty() ? &uv_chart_debug_image : nullptr);
    }
    if (!unwrap_ok)
    {
      return fail_with_timings("UV unwrap failed: " + error_message);
    }
  }

  if (!debug_uv_charts_path.empty())
  {
    bool chart_write_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Write UV chart debug image");
      chart_write_ok = sdf::write_png_rgb8(uv_chart_debug_image, debug_uv_charts_path, &error_message);
    }
    if (!chart_write_ok)
    {
      return fail_with_timings("UV chart debug image write failed: " + error_message);
    }
  }

  if (!bake_normal_path.empty())
  {
    normal_bake_settings.width = static_cast<int>(unwrap_result.atlas_width);
    normal_bake_settings.height = static_cast<int>(unwrap_result.atlas_height);
    if (normal_bake_settings.surface_epsilon <= 0.0f)
    {
      normal_bake_settings.surface_epsilon = std::max(scene_file.build_settings.cell_size * 0.05f, 1.0e-3f);
    }

    sdf::Rgb8Image image;
    bool normal_bake_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Bake normal map");
      normal_bake_ok = sdf::bake_sdf_normal_texture(
        scene_file.scene,
        export_mesh,
        normal_bake_settings,
        &image,
        &normal_bake_result,
        &error_message,
        progress_callback);
    }
    if (!normal_bake_ok)
    {
      return fail_with_timings("Normal bake failed: " + error_message);
    }

    bool normal_write_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Write normal bake image");
      normal_write_ok = sdf::write_png_rgb8(image, bake_normal_path, &error_message);
    }
    if (!normal_write_ok)
    {
      return fail_with_timings("Normal bake image write failed: " + error_message);
    }
  }

  sdf::RayScene ray_scene;
  if (needs_ray_scene)
  {
    ScopedPhaseTimer phase_timer(&phase_timings, "Build ray scene");
    ray_scene = sdf::build_ray_scene(build.mesh, progress_callback);
  }

  if (!debug_render_path.empty())
  {
    sdf::Rgb8Image image;
    bool render_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Render debug image");
      render_ok = sdf::render_debug_image(ray_scene, debug_settings, &image, &error_message, progress_callback);
    }
    if (!render_ok)
    {
      return fail_with_timings("Debug render failed: " + error_message);
    }

    bool debug_write_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Write debug image");
      debug_write_ok = sdf::write_png_rgb8(image, debug_render_path, &error_message);
    }
    if (!debug_write_ok)
    {
      return fail_with_timings("Debug image write failed: " + error_message);
    }
  }

  if (needs_ao_bake)
  {
    bake_settings.width = static_cast<int>(unwrap_result.atlas_width);
    bake_settings.height = static_cast<int>(unwrap_result.atlas_height);

    bool bake_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Bake ambient occlusion");
      bake_ok = sdf::bake_ambient_occlusion_texture(
        export_mesh,
        ray_scene,
        bake_settings,
        &ao_image,
        &bake_result,
        &error_message,
        progress_callback);
    }
    if (!bake_ok)
    {
      return fail_with_timings("AO bake failed: " + error_message);
    }

    if (!bake_ao_path.empty())
    {
      bool bake_write_ok = false;
      {
        ScopedPhaseTimer phase_timer(&phase_timings, "Write AO bake image");
        bake_write_ok = sdf::write_png_rgb8(ao_image, bake_ao_path, &error_message);
      }
      if (!bake_write_ok)
      {
        return fail_with_timings("AO bake image write failed: " + error_message);
      }
    }
  }

  if (!bake_surface_pack_path.empty())
  {
    surface_pack_settings.width = static_cast<int>(unwrap_result.atlas_width);
    surface_pack_settings.height = static_cast<int>(unwrap_result.atlas_height);
    if (surface_pack_settings.dilation_passes < 0 && bake_settings.dilation_passes >= 0)
    {
      surface_pack_settings.dilation_passes = bake_settings.dilation_passes;
    }
    if (surface_pack_settings.surface_epsilon <= 0.0f)
    {
      surface_pack_settings.surface_epsilon = std::max(scene_file.build_settings.cell_size * 0.05f, 1.0e-3f);
    }

    sdf::Rgb8Image image;
    bool bake_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Bake surface pack");
      bake_ok = sdf::bake_surface_pack_texture(
        scene_file.scene,
        export_mesh,
        ao_image,
        surface_pack_settings,
        &image,
        &surface_pack_result,
        &error_message,
        progress_callback);
    }
    if (!bake_ok)
    {
      return fail_with_timings("Surface pack bake failed: " + error_message);
    }

    bool bake_write_ok = false;
    {
      ScopedPhaseTimer phase_timer(&phase_timings, "Write surface pack image");
      bake_write_ok = sdf::write_png_rgb8(image, bake_surface_pack_path, &error_message);
    }
    if (!bake_write_ok)
    {
      return fail_with_timings("Surface pack image write failed: " + error_message);
    }
  }

  bool obj_write_ok = false;
  {
    ScopedPhaseTimer phase_timer(&phase_timings, "Write OBJ");
    obj_write_ok = sdf::write_obj(export_mesh, output_path, obj_write_options, &error_message);
  }
  if (!obj_write_ok)
  {
    return fail_with_timings("OBJ export failed: " + error_message);
  }

  std::cout << "Scene file: " << scene_path.string() << '\n';
  std::cout << "Scene: " << scene_file.scene.name << '\n';
  std::cout << "Boxes: " << scene_file.scene.boxes.size() << '\n';
  std::cout << "Meshing mode: " << sdf::meshing_mode_name(scene_file.build_settings.meshing_mode) << '\n';
  std::cout << "Cell size: " << scene_file.build_settings.cell_size << '\n';
  if (scene_file.build_settings.meshing_mode == sdf::MeshingMode::AdaptiveDualContouring)
  {
    std::cout << "Adaptive normal dot threshold: " << scene_file.build_settings.adaptive_normal_dot_threshold << '\n';
    std::cout << "Adaptive plane error ratio: " << scene_file.build_settings.adaptive_plane_error_ratio << '\n';
    std::cout << "Adaptive min plane error (cells): " << scene_file.build_settings.adaptive_min_plane_error_in_cells << '\n';
  }
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
    std::cout << "UV chart triangles min/avg/max: "
              << unwrap_result.min_chart_triangle_count << " / "
              << unwrap_result.average_chart_triangle_count << " / "
              << unwrap_result.max_chart_triangle_count << '\n';
    std::cout << "UV single-triangle charts: " << unwrap_result.single_triangle_chart_count << '\n';
    std::cout << "UV chart texels min/avg/max: "
              << unwrap_result.min_chart_texel_count << " / "
              << unwrap_result.average_chart_texel_count << " / "
              << unwrap_result.max_chart_texel_count << '\n';
    std::cout << "UV occupied texels: " << unwrap_result.chart_texel_count << '\n';
    std::cout << "UV padding texels: " << unwrap_result.padding_texel_count << '\n';
  }
  std::cout << "OBJ: " << output_path.string() << '\n';
  if (!debug_uv_charts_path.empty())
  {
    std::cout << "UV chart debug image: " << debug_uv_charts_path.string() << '\n';
  }
  if (!debug_render_path.empty())
  {
    std::cout << "Debug image: " << debug_render_path.string() << '\n';
  }
  if (needs_ao_bake)
  {
    if (!bake_ao_path.empty())
    {
      std::cout << "AO bake: " << bake_ao_path.string() << '\n';
    }
    else
    {
      std::cout << "AO bake: generated in memory for surface pack\n";
    }
    std::cout << "AO baked texels: " << bake_result.baked_texels << '\n';
    std::cout << "AO dilated texels: " << bake_result.dilated_texels << '\n';
    std::cout << "AO covered texels: " << bake_result.covered_texels << '\n';
    std::cout << "AO ray count: " << bake_result.ao_ray_count << '\n';
    std::cout << "AO average samples: " << bake_result.average_ao_samples_per_baked_texel << '\n';
    std::cout << "AO denoise passes: " << bake_result.denoise_passes << '\n';
    std::cout << "AO denoise radius: " << bake_result.denoise_radius << '\n';
    std::cout << "AO dilation passes: " << bake_result.dilation_passes << '\n';
  }
  if (!bake_normal_path.empty())
  {
    std::cout << "Normal bake: " << bake_normal_path.string() << '\n';
    std::cout << "Normal baked texels: " << normal_bake_result.baked_texels << '\n';
    std::cout << "Normal dilated texels: " << normal_bake_result.dilated_texels << '\n';
    std::cout << "Normal covered texels: " << normal_bake_result.covered_texels << '\n';
    std::cout << "Normal dilation passes: " << normal_bake_result.dilation_passes << '\n';
  }
  if (!bake_surface_pack_path.empty())
  {
    std::cout << "Surface pack: " << bake_surface_pack_path.string() << '\n';
    std::cout << "Surface pack channels: R=AO, G=curvature, B=thickness\n";
    std::cout << "Surface pack baked texels: " << surface_pack_result.baked_texels << '\n';
    std::cout << "Surface pack dilated texels: " << surface_pack_result.dilated_texels << '\n';
    std::cout << "Surface pack covered texels: " << surface_pack_result.covered_texels << '\n';
    std::cout << "Surface pack thickness max distance: " << surface_pack_settings.thickness_max_distance << '\n';
    std::cout << "Surface pack thickness cone angle: " << surface_pack_settings.thickness_cone_angle_degrees << '\n';
    std::cout << "Surface pack thickness filter passes: " << surface_pack_result.thickness_filter_passes << '\n';
    std::cout << "Surface pack thickness filter radius: " << surface_pack_result.thickness_filter_radius << '\n';
    std::cout << "Surface pack dilation passes: " << surface_pack_result.dilation_passes << '\n';
  }

  print_timing_summary();
  return 0;
}
