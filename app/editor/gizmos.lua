local hg = require("harfang")

local gizmos = {}

local grid_half_extent = 25
local grid_spacing = 1
local grid_snap_step = grid_spacing * 2
local grid_line_thickness = 0.025
local grid_height = 0.0

local function snap_to_step(value, step)
  return math.floor(value / step) * step
end

local function is_finite_number(value)
  return value == value and value ~= math.huge and value ~= -math.huge
end

local function is_finite_vec3(value)
  return is_finite_number(value.x) and is_finite_number(value.y) and is_finite_number(value.z)
end

local function intersect_ground_plane(origin, direction, fallback)
  if math.abs(direction.y) < 0.0001 then
    return fallback
  end

  local distance_to_ground = -origin.y / direction.y
  if not is_finite_number(distance_to_ground) then
    return fallback
  end

  local point = origin + direction * distance_to_ground
  if not is_finite_vec3(point) then
    return fallback
  end

  return point
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

local function create_material(app, r, g, b)
  local shader_color = make_default_shader_color(app, r, g, b)
  return hg.CreateMaterial(
    app.render.shader_ref,
    "uDiffuseColor", shader_color,
    "uSpecularColor", shader_color)
end

local function set_line_transform(transform, position, yaw, length, thickness)
  transform:SetPos(position)
  transform:SetRot(hg.Vec3(0, yaw, 0))
  transform:SetScale(hg.Vec3(length, thickness, thickness))
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

function gizmos.attach(app)
  local scene = app.scene.handle
  local line_ref = app.render.line_ref
  local grid_material = create_material(app, 92, 98, 108)
  local x_axis_material = create_material(app, 176, 72, 72)
  local z_axis_material = create_material(app, 72, 120, 176)
  local grid_x_nodes
  local grid_z_nodes
  grid_x_nodes, grid_z_nodes = create_grid_nodes(
    scene,
    line_ref,
    grid_material,
    x_axis_material,
    z_axis_material)

  app.gizmos = {
    grid = {
      center = hg.Vec3(0, 0, 0),
      x_nodes = grid_x_nodes,
      z_nodes = grid_z_nodes
    }
  }
end

function gizmos.update(app, frame)
  local grid_state = app.gizmos.grid

  grid_state.center = snap_grid_center(
    intersect_ground_plane(
      hg.GetT(frame.camera_world),
      frame.camera_forward,
      grid_state.center))
  update_grid_nodes(grid_state.x_nodes, grid_state.z_nodes, grid_state.center)
end

return gizmos
