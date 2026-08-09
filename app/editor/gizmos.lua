local hg = require("harfang")

local gizmos = {}

local grid_half_extent = 25
local grid_spacing = 1
local grid_snap_step = grid_spacing * 2
local grid_line_thickness = 0.025
local grid_height = 0.0

local measurement_height = 0.06
local measurement_arrow_length = 1.25
local measurement_arrow_angle = math.rad(28)
local measurement_label_lift = 0.012
local measurement_label_scale = 0.04
local measurement_label_gap_padding = 0.25
local measurement_backdrop_lift = 0.002
local measurement_backdrop_thickness = 0.002
local measurement_backdrop_padding_x = 0.35
local measurement_backdrop_padding_z = 0.18

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

local function intersect_horizontal_plane(origin, direction, plane_y, fallback)
  if math.abs(direction.y) < 0.0001 then
    return fallback
  end

  local distance_to_plane = (plane_y - origin.y) / direction.y
  if not is_finite_number(distance_to_plane) then
    return fallback
  end

  local point = origin + direction * distance_to_plane
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

local function create_emissive_material(app, r, g, b)
  local material = hg.CreateMaterial(
    app.render.shader_ref,
    "uDiffuseColor", hg.Vec4(0.0, 0.0, 0.0, 1.0),
    "uSpecularColor", hg.Vec4(0.0, 0.0, 0.0, 1.0))
  hg.SetMaterialValue(material, "uSelfColor", make_default_shader_color(app, r, g, b))
  return material
end

local function set_line_transform(transform, position, yaw, length, thickness)
  transform:SetPos(position)
  transform:SetRot(hg.Vec3(0, yaw, 0))
  transform:SetScale(hg.Vec3(length, thickness, thickness))
end

local function rotate_xz(direction, angle)
  local c = math.cos(angle)
  local s = math.sin(angle)
  return hg.Vec3(
    direction.x * c - direction.z * s,
    0,
    direction.x * s + direction.z * c)
end

local function yaw_from_xz(direction)
  return math.atan(direction.z, direction.x)
end

local function distance_on_line(start_pos, line_direction, point)
  return (point.x - start_pos.x) * line_direction.x +
    (point.y - start_pos.y) * line_direction.y +
    (point.z - start_pos.z) * line_direction.z
end

local function clip_line_to_rect(x0, y0, x1, y1, min_x, min_y, max_x, max_y)
  local dx = x1 - x0
  local dy = y1 - y0
  local t0 = 0.0
  local t1 = 1.0

  local function clip(p, q)
    if math.abs(p) < 0.0001 then
      return q >= 0.0
    end

    local ratio = q / p
    if p < 0.0 then
      if ratio > t1 then
        return false
      end
      if ratio > t0 then
        t0 = ratio
      end
      return true
    end

    if ratio < t0 then
      return false
    end
    if ratio < t1 then
      t1 = ratio
    end
    return true
  end

  if not clip(-dx, x0 - min_x) then
    return false
  end
  if not clip(dx, max_x - x0) then
    return false
  end
  if not clip(-dy, y0 - min_y) then
    return false
  end
  if not clip(dy, max_y - y0) then
    return false
  end

  return true, x0 + dx * t0, y0 + dy * t0, x0 + dx * t1, y0 + dy * t1
end

local function unproject_screen_to_horizontal_plane(
  inverse_projection_matrix,
  camera_world,
  resolution,
  screen_x,
  screen_y,
  plane_y,
  fallback)
  local near_ok
  local near_view
  near_ok, near_view = hg.UnprojectFromScreenSpace(
    inverse_projection_matrix,
    hg.Vec3(screen_x, screen_y, 0.0),
    resolution)

  local far_ok
  local far_view
  far_ok, far_view = hg.UnprojectFromScreenSpace(
    inverse_projection_matrix,
    hg.Vec3(screen_x, screen_y, 1.0),
    resolution)

  if not near_ok or not far_ok then
    return false, fallback
  end

  local ray_origin = camera_world * near_view
  local ray_target = camera_world * far_view
  local ray_direction = hg.Normalize(ray_target - ray_origin)

  return true, intersect_horizontal_plane(ray_origin, ray_direction, plane_y, fallback)
end

local function compute_visible_measurement_segment(
  projection_matrix,
  inverse_projection_matrix,
  view_matrix,
  camera_world,
  resolution,
  start_pos,
  end_pos)
  local delta = end_pos - start_pos
  local distance = hg.Len(delta)
  local line_direction = distance > 0.0001 and delta / distance or hg.Vec3(1, 0, 0)
  local _, start_screen = hg.ProjectToScreenSpace(projection_matrix, view_matrix * start_pos, resolution)
  local _, end_screen = hg.ProjectToScreenSpace(projection_matrix, view_matrix * end_pos, resolution)

  if start_screen.z < 0.0 and end_screen.z < 0.0 then
    return false, 0.0, 0.0
  end

  local is_visible
  local clip_x0
  local clip_y0
  local clip_x1
  local clip_y1
  is_visible, clip_x0, clip_y0, clip_x1, clip_y1 = clip_line_to_rect(
    start_screen.x,
    start_screen.y,
    end_screen.x,
    end_screen.y,
    0.0,
    0.0,
    resolution.x,
    resolution.y)

  if not is_visible then
    return false, 0.0, 0.0
  end

  local start_ok
  local visible_start_world
  start_ok, visible_start_world = unproject_screen_to_horizontal_plane(
    inverse_projection_matrix,
    camera_world,
    resolution,
    clip_x0,
    clip_y0,
    measurement_height,
    start_pos)

  local end_ok
  local visible_end_world
  end_ok, visible_end_world = unproject_screen_to_horizontal_plane(
    inverse_projection_matrix,
    camera_world,
    resolution,
    clip_x1,
    clip_y1,
    measurement_height,
    end_pos)

  if not start_ok or not end_ok then
    return false, 0.0, 0.0
  end

  local visible_start_distance = math.max(0.0, math.min(distance_on_line(start_pos, line_direction, visible_start_world), distance))
  local visible_end_distance = math.max(0.0, math.min(distance_on_line(start_pos, line_direction, visible_end_world), distance))

  if visible_start_distance > visible_end_distance then
    visible_start_distance, visible_end_distance = visible_end_distance, visible_start_distance
  end

  return visible_end_distance - visible_start_distance > 0.001, visible_start_distance, visible_end_distance
end

local function compute_measurement_label_transform(start_pos, end_pos, anchor_distance)
  local delta = end_pos - start_pos
  local distance = hg.Len(delta)
  local line_direction = distance > 0.0001 and delta / distance or hg.Vec3(1, 0, 0)
  local line_yaw = yaw_from_xz(line_direction)
  local clamped_anchor_distance = math.max(0.0, math.min(anchor_distance, distance))
  local label_anchor = start_pos + line_direction * clamped_anchor_distance
  local label_position = hg.Vec3(
    label_anchor.x,
    measurement_height + measurement_label_lift,
    label_anchor.z)
  local label_matrix = hg.TransformationMat4(
    label_position,
    hg.Vec3(hg.Deg(-90), hg.Deg(180) - line_yaw, 0),
    hg.Vec3(measurement_label_scale, measurement_label_scale, measurement_label_scale))

  return label_matrix, label_anchor, line_yaw
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

local function create_measurement_nodes(scene, line_ref, measurement_material, backdrop_material)
  local nodes = {
    shaft_a = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    shaft_b = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    start_a = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    start_b = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    end_a = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    end_b = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    backdrop = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {backdrop_material})
  }

  nodes.backdrop:Disable()
  return nodes
end

local function update_measurement_backdrop_node(node, center_pos, line_yaw, width, depth, is_visible)
  if not is_visible then
    node:Disable()
    return
  end

  local transform = node:GetTransform()
  transform:SetPos(hg.Vec3(center_pos.x, measurement_height + measurement_backdrop_lift, center_pos.z))
  transform:SetRot(hg.Vec3(0, line_yaw, 0))
  transform:SetScale(hg.Vec3(width, measurement_backdrop_thickness, depth))
  node:Enable()
end

local function update_measurement_nodes(nodes, start_pos, end_pos, gap_center_distance, gap_half_length)
  local delta = end_pos - start_pos
  local distance = hg.Len(delta)
  local line_direction = distance > 0.0001 and delta / distance or hg.Vec3(1, 0, 0)
  local line_yaw = yaw_from_xz(line_direction)

  local function update_shaft_segment(node, start_distance, segment_length)
    local safe_length = math.max(segment_length, 0.001)
    local segment_mid = start_pos + line_direction * (start_distance + segment_length * 0.5)
    set_line_transform(
      node:GetTransform(),
      hg.Vec3(segment_mid.x, measurement_height, segment_mid.z),
      line_yaw,
      safe_length,
      grid_line_thickness * 1.35)
  end

  if gap_center_distance ~= nil and gap_half_length > 0.0 then
    local gap_start = math.max(0.0, math.min(gap_center_distance - gap_half_length, distance))
    local gap_end = math.max(gap_start, math.min(gap_center_distance + gap_half_length, distance))
    update_shaft_segment(nodes.shaft_a, 0.0, gap_start)
    update_shaft_segment(nodes.shaft_b, gap_end, math.max(distance - gap_end, 0.0))
  else
    update_shaft_segment(nodes.shaft_a, 0.0, distance)
    update_shaft_segment(nodes.shaft_b, distance, 0.0)
  end

  local arrow_length = math.min(measurement_arrow_length, math.max(distance * 0.25, 0.35))
  local start_base_dir = hg.Vec3(-line_direction.x, -line_direction.y, -line_direction.z)
  local start_dir_a = rotate_xz(start_base_dir, measurement_arrow_angle)
  local start_dir_b = rotate_xz(start_base_dir, -measurement_arrow_angle)
  local end_dir_a = rotate_xz(line_direction, measurement_arrow_angle)
  local end_dir_b = rotate_xz(line_direction, -measurement_arrow_angle)

  set_line_transform(
    nodes.start_a:GetTransform(),
    hg.Vec3(start_pos.x, measurement_height, start_pos.z) + start_dir_a * (arrow_length * 0.5),
    yaw_from_xz(start_dir_a),
    arrow_length,
    grid_line_thickness * 1.35)
  set_line_transform(
    nodes.start_b:GetTransform(),
    hg.Vec3(start_pos.x, measurement_height, start_pos.z) + start_dir_b * (arrow_length * 0.5),
    yaw_from_xz(start_dir_b),
    arrow_length,
    grid_line_thickness * 1.35)
  set_line_transform(
    nodes.end_a:GetTransform(),
    hg.Vec3(end_pos.x, measurement_height, end_pos.z) + end_dir_a * (arrow_length * 0.5),
    yaw_from_xz(end_dir_a),
    arrow_length,
    grid_line_thickness * 1.35)
  set_line_transform(
    nodes.end_b:GetTransform(),
    hg.Vec3(end_pos.x, measurement_height, end_pos.z) + end_dir_b * (arrow_length * 0.5),
    yaw_from_xz(end_dir_b),
    arrow_length,
    grid_line_thickness * 1.35)
end

function gizmos.attach(app)
  local background = app.theme.background_clear
  local scene = app.scene.handle
  local line_ref = app.render.line_ref
  local grid_material = create_material(app, 92, 98, 108)
  local x_axis_material = create_material(app, 176, 72, 72)
  local z_axis_material = create_material(app, 72, 120, 176)
  local measurement_material = create_material(app, 232, 232, 240)
  local backdrop_material = create_emissive_material(app, background[1], background[2], background[3])
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
    },
    measurement = {
      nodes = create_measurement_nodes(scene, line_ref, measurement_material, backdrop_material),
      label_visible = false,
      label_text = "",
      label_matrix = nil
    }
  }
end

function gizmos.update(app, frame)
  local grid_state = app.gizmos.grid
  local measurement_state = app.gizmos.measurement

  grid_state.center = snap_grid_center(
    intersect_ground_plane(
      hg.GetT(frame.camera_world),
      frame.camera_forward,
      grid_state.center))
  update_grid_nodes(grid_state.x_nodes, grid_state.z_nodes, grid_state.center)

  local measurement_start_world = hg.Vec3(grid_state.center.x, measurement_height, grid_state.center.z)
  local measurement_end_world = hg.Vec3(app.scene.origin.x, measurement_height, app.scene.origin.z)
  local measurement_distance = hg.Len(measurement_end_world - measurement_start_world)
  local label_text = string.format("%.0fm", measurement_distance)
  local label_rect = hg.ComputeTextRect(app.render.font, label_text)
  local label_width_world = (label_rect.ex - label_rect.sx) * measurement_label_scale
  local label_height_world = math.abs(label_rect.ey - label_rect.sy) * measurement_label_scale
  local backdrop_width_world = label_width_world + measurement_backdrop_padding_x * 2
  local backdrop_depth_world = label_height_world + measurement_backdrop_padding_z * 2
  local label_gap_half_length = math.min(
    measurement_distance * 0.45,
    backdrop_width_world * 0.5 + measurement_label_gap_padding)
  local label_visible = false
  local label_anchor_distance = measurement_distance * 0.5
  local visible_start_distance = 0.0
  local visible_end_distance = 0.0

  if frame.inverse_projection_ok then
    label_visible, visible_start_distance, visible_end_distance = compute_visible_measurement_segment(
      frame.projection_matrix,
      frame.inverse_projection_matrix,
      frame.view_matrix,
      frame.camera_world,
      frame.resolution,
      measurement_start_world,
      measurement_end_world)
  end

  if label_visible then
    local visible_segment_length = visible_end_distance - visible_start_distance
    local required_visible_length = backdrop_width_world + measurement_label_gap_padding * 2
    label_visible = visible_segment_length >= required_visible_length
    label_anchor_distance = (visible_start_distance + visible_end_distance) * 0.5
  end

  update_measurement_nodes(
    measurement_state.nodes,
    measurement_start_world,
    measurement_end_world,
    label_visible and label_anchor_distance or nil,
    label_visible and label_gap_half_length or 0.0)

  measurement_state.label_visible = label_visible
  measurement_state.label_text = label_text
  measurement_state.label_matrix = nil

  if label_visible then
    local label_anchor_world
    local label_line_yaw
    measurement_state.label_matrix, label_anchor_world, label_line_yaw = compute_measurement_label_transform(
      measurement_start_world,
      measurement_end_world,
      label_anchor_distance)
    update_measurement_backdrop_node(
      measurement_state.nodes.backdrop,
      label_anchor_world,
      label_line_yaw,
      backdrop_width_world,
      backdrop_depth_world,
      true)
  else
    update_measurement_backdrop_node(measurement_state.nodes.backdrop, nil, 0.0, 0.0, 0.0, false)
  end
end

function gizmos.draw(app, frame)
  local view_id = app.render.view_ids.label
  local measurement_state = app.gizmos.measurement

  hg.SetViewRect(view_id, 0, 0, frame.window_width, frame.window_height)
  hg.SetViewClear(view_id, 0)
  hg.SetViewTransform(view_id, frame.view_matrix, frame.projection_matrix)

  if measurement_state.label_visible then
    hg.DrawText(
      view_id,
      app.render.font,
      measurement_state.label_text,
      app.render.font_program,
      "u_tex",
      0,
      measurement_state.label_matrix,
      hg.Vec3(0, 0, 0),
      hg.DTHA_Center,
      hg.DTVA_Center,
      app.render.text_uniform_values,
      {},
      app.render.text_render_state)
  end
end

return gizmos
