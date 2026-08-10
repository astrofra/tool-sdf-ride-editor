local hg = require("harfang")
local ground_plane = require("editor.ground_plane")

local gizmos = {}

local grid_half_extent = 25
local grid_spacing = 1
local grid_snap_step = grid_spacing * 2
local grid_line_thickness = 0.025
local grid_height = 0.0
local placement_cursor_height = 0.02
local placement_cursor_line_thickness = 0.12

local function snap_to_step(value, step)
  return math.floor(value / step) * step
end

local function snap_grid_center(position)
  return hg.Vec3(
    snap_to_step(position.x, grid_snap_step),
    0,
    snap_to_step(position.z, grid_snap_step))
end

local function shader_color_channel_from_byte(value, gamma)
  return math.max(0.0, math.min(value / 255.0, 1.0)) ^ gamma
end

local function make_default_shader_color(app, r, g, b)
  return hg.Vec4(
    shader_color_channel_from_byte(r, app.theme.default_shader_gamma),
    shader_color_channel_from_byte(g, app.theme.default_shader_gamma),
    shader_color_channel_from_byte(b, app.theme.default_shader_gamma),
    1.0)
end

local function create_locked_default_material(app, diffuse_color, specular_color, self_color)
  local material = hg.CreateMaterial(app.render.shader_ref)
  hg.SetMaterialValue(material, "uDiffuseColor", diffuse_color)
  hg.SetMaterialValue(material, "uSpecularColor", specular_color)
  hg.SetMaterialValue(material, "uSelfColor", self_color)
  return material
end

local function create_material(app, r, g, b)
  local shader_color = make_default_shader_color(app, r, g, b)
  return create_locked_default_material(
    app,
    shader_color,
    shader_color,
    hg.Vec4(0.0, 0.0, 0.0, 1.0))
end

local function set_line_transform(transform, position, yaw, length, thickness)
  transform:SetPos(position)
  transform:SetRot(hg.Vec3(0, yaw, 0))
  transform:SetScale(hg.Vec3(length, thickness, thickness))
end

local function set_nodes_enabled(nodes, is_enabled)
  for index = 1, #nodes do
    if is_enabled then
      nodes[index]:Enable()
    else
      nodes[index]:Disable()
    end
  end
end

local function create_grid_nodes(scene, line_ref, grid_material, x_axis_material, z_axis_material)
  local line_count = math.floor((grid_half_extent * 2) / grid_spacing)
  local grid_x_nodes = {}
  local grid_z_nodes = {}

  for index = 0, line_count do
    local coordinate = -grid_half_extent + index * grid_spacing
    local x_line_material = coordinate == 0 and x_axis_material or grid_material
    local z_line_material = coordinate == 0 and z_axis_material or grid_material

    grid_x_nodes[#grid_x_nodes + 1] = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {x_line_material})
    grid_z_nodes[#grid_z_nodes + 1] = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {z_line_material})
  end

  return grid_x_nodes, grid_z_nodes
end

local function create_square_outline_nodes(scene, line_ref, material)
  local nodes = {}
  for _ = 1, 4 do
    nodes[#nodes + 1] = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {material})
  end
  set_nodes_enabled(nodes, false)

  return nodes
end

local function update_grid_nodes(grid_x_nodes, grid_z_nodes, center)
  for index = 1, #grid_x_nodes do
    local offset = -grid_half_extent + (index - 1) * grid_spacing
    set_line_transform(
      grid_x_nodes[index]:GetTransform(),
      hg.Vec3(center.x, grid_height, center.z + offset),
      0,
      grid_half_extent * 2,
      grid_line_thickness)
    set_line_transform(
      grid_z_nodes[index]:GetTransform(),
      hg.Vec3(center.x + offset, grid_height, center.z),
      hg.Deg(90),
      grid_half_extent * 2,
      grid_line_thickness)
  end
end

local function update_square_outline_nodes(nodes, center, size, height, thickness)
  local half_extent = size * 0.5

  set_line_transform(
    nodes[1]:GetTransform(),
    hg.Vec3(center.x, height, center.z - half_extent),
    0,
    size,
    thickness)
  set_line_transform(
    nodes[2]:GetTransform(),
    hg.Vec3(center.x, height, center.z + half_extent),
    0,
    size,
    thickness)
  set_line_transform(
    nodes[3]:GetTransform(),
    hg.Vec3(center.x - half_extent, height, center.z),
    hg.Deg(90),
    size,
    thickness)
  set_line_transform(
    nodes[4]:GetTransform(),
    hg.Vec3(center.x + half_extent, height, center.z),
    hg.Deg(90),
    size,
    thickness)
end

function gizmos.attach(app)
  local scene = app.scene.handle
  local line_ref = app.render.line_ref
  local grid_material = create_material(app, 92, 98, 108)
  local x_axis_material = create_material(app, 176, 72, 72)
  local z_axis_material = create_material(app, 72, 120, 176)
  local placement_cursor_material = create_material(app, 212, 56, 56)
  local grid_x_nodes
  local grid_z_nodes
  grid_x_nodes, grid_z_nodes = create_grid_nodes(
    scene,
    line_ref,
    grid_material,
    x_axis_material,
    z_axis_material)
  local placement_cursor_nodes = create_square_outline_nodes(scene, line_ref, placement_cursor_material)

  app.gizmos = {
    grid = {
      center = hg.Vec3(0, 0, 0),
      x_nodes = grid_x_nodes,
      z_nodes = grid_z_nodes
    },
    cell_placement_cursor = {
      nodes = placement_cursor_nodes,
      visible = false
    }
  }
end

function gizmos.update(app, frame)
  local grid_state = app.gizmos.grid
  local cursor_state = app.gizmos.cell_placement_cursor
  local center_hit_ok
  local center_hit
  center_hit_ok, center_hit = ground_plane.screen_to_ground(
    frame,
    frame.window_width * 0.5,
    frame.window_height * 0.5,
    0.0)

  if center_hit_ok then
    grid_state.center = snap_grid_center(center_hit)
  end
  update_grid_nodes(grid_state.x_nodes, grid_state.z_nodes, grid_state.center)

  local world_state = app.sdf_world or app.sdf
  local placement_state = world_state ~= nil and world_state.cell_placement or nil
  local world_document = world_state ~= nil and world_state.world_document or nil
  local placement_visible = placement_state ~= nil and placement_state.active and placement_state.valid and world_document ~= nil

  if placement_visible then
    update_square_outline_nodes(
      cursor_state.nodes,
      placement_state.snapped_world_position,
      world_document.cell_size,
      placement_cursor_height,
      placement_cursor_line_thickness)
  end

  if placement_visible ~= cursor_state.visible then
    set_nodes_enabled(cursor_state.nodes, placement_visible)
    cursor_state.visible = placement_visible
  end
end

return gizmos
