#include "sdf/scene_io.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace sdf
{

namespace
{

std::string trim(const std::string &text)
{
  const std::size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
  {
    return {};
  }

  const std::size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

std::vector<std::string> tokenize(const std::string &text)
{
  std::istringstream stream(text);
  std::vector<std::string> tokens;
  std::string token;

  while (stream >> token)
  {
    tokens.push_back(token);
  }

  return tokens;
}

bool parse_float(const std::string &text, float *value)
{
  try
  {
    std::size_t parsed_size = 0;
    const float parsed_value = std::stof(text, &parsed_size);
    if (parsed_size != text.size())
    {
      return false;
    }

    *value = parsed_value;
    return true;
  }
  catch (...)
  {
    return false;
  }
}

bool parse_uint32(const std::string &text, std::uint32_t *value)
{
  try
  {
    std::size_t parsed_size = 0;
    const unsigned long parsed_value = std::stoul(text, &parsed_size, 10);
    if (parsed_size != text.size())
    {
      return false;
    }

    *value = static_cast<std::uint32_t>(parsed_value);
    return true;
  }
  catch (...)
  {
    return false;
  }
}

bool parse_mask(const std::string &text, ModifierMask *mask)
{
  if (text == "all")
  {
    *mask = ModifierMask::All;
    return true;
  }
  if (text == "top")
  {
    *mask = ModifierMask::Top;
    return true;
  }
  if (text == "bottom")
  {
    *mask = ModifierMask::Bottom;
    return true;
  }
  if (text == "edges")
  {
    *mask = ModifierMask::Edges;
    return true;
  }
  if (text == "top_edges")
  {
    *mask = ModifierMask::TopEdges;
    return true;
  }
  return false;
}

bool fail_with_message(std::string *error_message, const std::filesystem::path &path, int line_number, const std::string &reason)
{
  if (error_message != nullptr)
  {
    *error_message = path.string() + ":" + std::to_string(line_number) + ": " + reason;
  }
  return false;
}

bool parse_box_line(
  const std::vector<std::string> &tokens,
  const std::filesystem::path &path,
  int line_number,
  SceneDocument *scene,
  std::string *error_message)
{
  if (tokens.size() != 9 && tokens.size() != 10)
  {
    return fail_with_message(error_message, path, line_number, "box expects 8 or 9 arguments");
  }

  SdfBox box;

  if (tokens[1] == "add")
  {
    box.op = CsgOp::Add;
  }
  else if (tokens[1] == "subtract")
  {
    box.op = CsgOp::Subtract;
  }
  else
  {
    return fail_with_message(error_message, path, line_number, "box operation must be 'add' or 'subtract'");
  }

  box.name = tokens[2];

  if (!parse_float(tokens[3], &box.transform.translation.x) ||
      !parse_float(tokens[4], &box.transform.translation.y) ||
      !parse_float(tokens[5], &box.transform.translation.z) ||
      !parse_float(tokens[6], &box.half_size.x) ||
      !parse_float(tokens[7], &box.half_size.y) ||
      !parse_float(tokens[8], &box.half_size.z))
  {
    return fail_with_message(error_message, path, line_number, "box numeric fields are invalid");
  }

  if (box.half_size.x <= 0.0f || box.half_size.y <= 0.0f || box.half_size.z <= 0.0f)
  {
    return fail_with_message(error_message, path, line_number, "box half sizes must be strictly positive");
  }

  if (tokens.size() == 10 && !parse_uint32(tokens[9], &box.material_id))
  {
    return fail_with_message(error_message, path, line_number, "box material id is invalid");
  }

  scene->boxes.push_back(box);
  return true;
}

bool parse_noise_modifier_line(
  const std::vector<std::string> &tokens,
  const std::filesystem::path &path,
  int line_number,
  SceneDocument *scene,
  std::string *error_message)
{
  if (tokens.size() != 9)
  {
    return fail_with_message(
      error_message,
      path,
      line_number,
      "noise_displace_masked expects 8 arguments");
  }

  NoiseDisplaceMaskedModifier modifier;
  modifier.name = tokens[1];
  modifier.target_box_name = tokens[2];

  if (!parse_float(tokens[3], &modifier.amplitude) ||
      !parse_float(tokens[4], &modifier.frequency) ||
      !parse_uint32(tokens[5], &modifier.seed) ||
      !parse_uint32(tokens[6], &modifier.octaves) ||
      !parse_mask(tokens[7], &modifier.mask) ||
      !parse_float(tokens[8], &modifier.mask_width))
  {
    return fail_with_message(error_message, path, line_number, "noise_displace_masked arguments are invalid");
  }

  if (modifier.amplitude < 0.0f)
  {
    return fail_with_message(error_message, path, line_number, "noise_displace_masked amplitude must be non-negative");
  }

  if (modifier.frequency <= 0.0f)
  {
    return fail_with_message(error_message, path, line_number, "noise_displace_masked frequency must be strictly positive");
  }

  if (modifier.octaves == 0)
  {
    return fail_with_message(error_message, path, line_number, "noise_displace_masked octaves must be at least 1");
  }

  if (modifier.mask_width <= 0.0f)
  {
    return fail_with_message(error_message, path, line_number, "noise_displace_masked mask_width must be strictly positive");
  }

  scene->noise_modifiers.push_back(modifier);
  return true;
}

bool parse_box_cut_modifier_line(
  const std::vector<std::string> &tokens,
  const std::filesystem::path &path,
  int line_number,
  SceneDocument *scene,
  std::string *error_message)
{
  if (tokens.size() != 9)
  {
    return fail_with_message(error_message, path, line_number, "box_cut expects 8 arguments");
  }

  BoxCutModifier modifier;
  modifier.name = tokens[1];
  modifier.target_box_name = tokens[2];

  if (!parse_float(tokens[3], &modifier.translation.x) ||
      !parse_float(tokens[4], &modifier.translation.y) ||
      !parse_float(tokens[5], &modifier.translation.z) ||
      !parse_float(tokens[6], &modifier.half_size.x) ||
      !parse_float(tokens[7], &modifier.half_size.y) ||
      !parse_float(tokens[8], &modifier.half_size.z))
  {
    return fail_with_message(error_message, path, line_number, "box_cut numeric fields are invalid");
  }

  if (modifier.half_size.x <= 0.0f || modifier.half_size.y <= 0.0f || modifier.half_size.z <= 0.0f)
  {
    return fail_with_message(error_message, path, line_number, "box_cut half sizes must be strictly positive");
  }

  scene->box_cut_modifiers.push_back(modifier);
  return true;
}

bool validate_modifier_targets(
  const SceneDocument &scene,
  const std::filesystem::path &path,
  std::string *error_message)
{
  for (std::size_t i = 0; i < scene.boxes.size(); ++i)
  {
    for (std::size_t j = i + 1; j < scene.boxes.size(); ++j)
    {
      if (scene.boxes[i].name == scene.boxes[j].name)
      {
        if (error_message != nullptr)
        {
          *error_message = "scene file has duplicate box name '" + scene.boxes[i].name + "': " + path.string();
        }
        return false;
      }
    }
  }

  auto box_exists = [&](const std::string &box_name) -> bool
  {
    for (const SdfBox &box : scene.boxes)
    {
      if (box.name == box_name)
      {
        return true;
      }
    }
    return false;
  };

  for (const NoiseDisplaceMaskedModifier &modifier : scene.noise_modifiers)
  {
    if (!box_exists(modifier.target_box_name))
    {
      if (error_message != nullptr)
      {
        *error_message = "noise_displace_masked target box '" + modifier.target_box_name + "' does not exist: " + path.string();
      }
      return false;
    }
  }

  for (const BoxCutModifier &modifier : scene.box_cut_modifiers)
  {
    if (!box_exists(modifier.target_box_name))
    {
      if (error_message != nullptr)
      {
        *error_message = "box_cut target box '" + modifier.target_box_name + "' does not exist: " + path.string();
      }
      return false;
    }
  }

  return true;
}

}  // namespace

bool load_scene_file(
  const std::filesystem::path &input_path,
  SceneFile *out_scene_file,
  std::string *error_message)
{
  if (out_scene_file == nullptr)
  {
    if (error_message != nullptr)
    {
      *error_message = "load_scene_file requires a non-null output pointer";
    }
    return false;
  }

  std::ifstream stream(input_path);
  if (!stream.is_open())
  {
    if (error_message != nullptr)
    {
      *error_message = "failed to open scene file: " + input_path.string();
    }
    return false;
  }

  SceneFile parsed_file;
  bool has_scene_name = false;
  bool has_bounds = false;

  std::string raw_line;
  int line_number = 0;
  while (std::getline(stream, raw_line))
  {
    ++line_number;

    const std::size_t comment_pos = raw_line.find('#');
    const std::string line = trim(comment_pos == std::string::npos ? raw_line : raw_line.substr(0, comment_pos));
    if (line.empty())
    {
      continue;
    }

    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty())
    {
      continue;
    }

    if (tokens[0] == "scene")
    {
      if (tokens.size() != 2)
      {
        return fail_with_message(error_message, input_path, line_number, "scene expects exactly one name");
      }

      parsed_file.scene.name = tokens[1];
      has_scene_name = true;
      continue;
    }

    if (tokens[0] == "bounds")
    {
      if (tokens.size() != 7)
      {
        return fail_with_message(error_message, input_path, line_number, "bounds expects 6 numeric values");
      }

      if (!parse_float(tokens[1], &parsed_file.build_settings.bounds.min.x) ||
          !parse_float(tokens[2], &parsed_file.build_settings.bounds.min.y) ||
          !parse_float(tokens[3], &parsed_file.build_settings.bounds.min.z) ||
          !parse_float(tokens[4], &parsed_file.build_settings.bounds.max.x) ||
          !parse_float(tokens[5], &parsed_file.build_settings.bounds.max.y) ||
          !parse_float(tokens[6], &parsed_file.build_settings.bounds.max.z))
      {
        return fail_with_message(error_message, input_path, line_number, "bounds numeric values are invalid");
      }

      has_bounds = true;
      continue;
    }

    if (tokens[0] == "cell_size")
    {
      if (tokens.size() != 2)
      {
        return fail_with_message(error_message, input_path, line_number, "cell_size expects one numeric value");
      }

      if (!parse_float(tokens[1], &parsed_file.build_settings.cell_size) || parsed_file.build_settings.cell_size <= 0.0f)
      {
        return fail_with_message(error_message, input_path, line_number, "cell_size must be a strictly positive number");
      }
      continue;
    }

    if (tokens[0] == "meshing_mode")
    {
      if (tokens.size() != 2)
      {
        return fail_with_message(error_message, input_path, line_number, "meshing_mode expects one symbolic value");
      }

      if (!parse_meshing_mode_name(tokens[1], &parsed_file.build_settings.meshing_mode))
      {
        return fail_with_message(
          error_message,
          input_path,
          line_number,
          "meshing_mode must be 'marching_tetrahedra', 'dual_contouring', or 'adaptive_dual_contouring'");
      }
      continue;
    }

    if (tokens[0] == "box")
    {
      if (!parse_box_line(tokens, input_path, line_number, &parsed_file.scene, error_message))
      {
        return false;
      }
      continue;
    }

    if (tokens[0] == "noise_displace_masked")
    {
      if (!parse_noise_modifier_line(tokens, input_path, line_number, &parsed_file.scene, error_message))
      {
        return false;
      }
      continue;
    }

    if (tokens[0] == "box_cut")
    {
      if (!parse_box_cut_modifier_line(tokens, input_path, line_number, &parsed_file.scene, error_message))
      {
        return false;
      }
      continue;
    }

    return fail_with_message(error_message, input_path, line_number, "unknown directive '" + tokens[0] + "'");
  }

  if (!has_scene_name)
  {
    if (error_message != nullptr)
    {
      *error_message = "scene file is missing the 'scene' directive: " + input_path.string();
    }
    return false;
  }

  if (!has_bounds)
  {
    if (error_message != nullptr)
    {
      *error_message = "scene file is missing the 'bounds' directive: " + input_path.string();
    }
    return false;
  }

  if (parsed_file.scene.boxes.empty())
  {
    if (error_message != nullptr)
    {
      *error_message = "scene file does not define any boxes: " + input_path.string();
    }
    return false;
  }

  if (!validate_modifier_targets(parsed_file.scene, input_path, error_message))
  {
    return false;
  }

  *out_scene_file = parsed_file;
  return true;
}

}  // namespace sdf
