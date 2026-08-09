local sdf = require("sdf-generator")

local sdf_cell_factory = {}

local default_scene_build_cell_size = 1.0
local default_base_box_name = "socle"
local default_base_box_thickness = 5.0

local function assign_vec3(target, x, y, z)
  target.x = x
  target.y = y
  target.z = z
end

local function append_default_base_box(scene_file, world_document)
  local box = sdf.SdfBox()
  local half_thickness = default_base_box_thickness * 0.5

  box.name = default_base_box_name
  assign_vec3(box.transform.translation, 0.0, -half_thickness, 0.0)
  assign_vec3(box.half_size, world_document.cell_size * 0.5, half_thickness, world_document.cell_size * 0.5)

  scene_file.scene.boxes:push_back(box)
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
  append_default_base_box(scene_file, world_document)

  return scene_file
end

return sdf_cell_factory
