local sdf = require("sdf-generator")

local sdf_cell_factory = {}

local default_scene_build_cell_size = 1.0

local function assign_vec3(target, x, y, z)
  target.x = x
  target.y = y
  target.z = z
end

local function make_default_scene_bounds_values(world_document)
  local horizontal_half_extent = world_document.cell_size * 0.5 + world_document.cell_bounds_padding
  local vertical_min = -world_document.cell_bounds_padding
  local vertical_max = world_document.cell_size + world_document.cell_bounds_padding

  return {
    min_x = -horizontal_half_extent,
    min_y = vertical_min,
    min_z = -horizontal_half_extent,
    max_x = horizontal_half_extent,
    max_y = vertical_max,
    max_z = horizontal_half_extent
  }
end

function sdf_cell_factory.make_default_scene_bounds(world_document)
  local values = make_default_scene_bounds_values(world_document)
  local bounds = sdf.SdfAabb()

  assign_vec3(bounds.min, values.min_x, values.min_y, values.min_z)
  assign_vec3(bounds.max, values.max_x, values.max_y, values.max_z)

  return bounds
end

function sdf_cell_factory.make_default_scene_file(world_document, scene_name)
  local scene_file = sdf.SceneFile()
  local bounds = scene_file.build_settings.bounds
  local values = make_default_scene_bounds_values(world_document)

  scene_file.scene.name = scene_name or "unnamed_cell"
  assign_vec3(bounds.min, values.min_x, values.min_y, values.min_z)
  assign_vec3(bounds.max, values.max_x, values.max_y, values.max_z)
  scene_file.build_settings.cell_size = default_scene_build_cell_size

  return scene_file
end

return sdf_cell_factory
