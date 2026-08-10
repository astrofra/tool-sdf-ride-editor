local hg = require("harfang")
local ground_plane = require("editor.ground_plane")

local gizmos = {}

local ray_epsilon = 0.0001
local grid_half_extent = 25
local grid_spacing = 1
local grid_snap_step = grid_spacing * 2
local grid_line_thickness = 0.025
local grid_height = 0.0
local placement_cursor_height = 0.02
local placement_cursor_line_thickness = 0.12
local translation_axis_base_thickness = 0.12
local translation_axis_active_thickness = 0.18
local translation_axis_length_factor = 0.14
local translation_axis_min_length = 4.0
local translation_axis_max_length = 18.0
local translation_axis_pick_threshold_pixels = 14.0

local translation_axis_definitions = {
  {
    name = "x",
    direction = hg.Vec3(1.0, 0.0, 0.0),
    rotation = hg.Vec3(0.0, 0.0, 0.0),
    base_rgb = {188, 84, 84}
  },
  {
    name = "y",
    direction = hg.Vec3(0.0, 1.0, 0.0),
    rotation = hg.Vec3(0.0, 0.0, hg.Deg(90)),
    base_rgb = {96, 176, 96}
  },
  {
    name = "z",
    direction = hg.Vec3(0.0, 0.0, 1.0),
    rotation = hg.Vec3(0.0, hg.Deg(90), 0.0),
    base_rgb = {84, 128, 196}
  }
}

local translation_axis_by_name = {}
for index = 1, #translation_axis_definitions do
  local axis_definition = translation_axis_definitions[index]
  translation_axis_by_name[axis_definition.name] = axis_definition
end

local function snap_down_to_step(value, step)
  return math.floor(value / step) * step
end

local function snap_to_nearest_step(value, step)
  if step == nil or step <= 0.0 then
    return value
  end

  return math.floor(value / step + 0.5) * step
end

local function snap_grid_center(position)
  return hg.Vec3(
    snap_down_to_step(position.x, grid_snap_step),
    0.0,
    snap_down_to_step(position.z, grid_snap_step))
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

local function copy_vec3(vec3)
  return hg.Vec3(vec3.x, vec3.y, vec3.z)
end

local function set_line_transform(transform, position, rotation, length, thickness)
  transform:SetPos(position)
  transform:SetRot(rotation)
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

local function create_translation_axis_nodes(scene, line_ref, base_material, active_material)
  local base_node = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {base_material})
  local active_node = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {active_material})

  active_node:Disable()

  return {
    base = base_node,
    active = active_node
  }
end

local function get_world_state(app)
  return app.sdf_world or app.sdf
end

local function get_active_selected_box(world_state)
  if world_state == nil then
    return nil, nil, nil
  end

  local active_cell_index = world_state.active_cell_index
  if active_cell_index == nil or active_cell_index < 1 or active_cell_index > #world_state.cells then
    return nil, nil, nil
  end

  local active_cell = world_state.cells[active_cell_index]
  if active_cell == nil or active_cell.scene_file == nil then
    return nil, nil, active_cell
  end

  local selected_box_index = world_state.selection ~= nil and world_state.selection.active_box_index or nil
  if selected_box_index == nil then
    return nil, nil, active_cell
  end

  local boxes = active_cell.scene_file.scene.boxes
  if selected_box_index < 1 or selected_box_index > #boxes then
    return nil, nil, active_cell
  end

  return boxes[selected_box_index], selected_box_index, active_cell
end

local function get_selected_box_world_pivot(active_cell, selected_box)
  return hg.Vec3(
    active_cell.world_translation.x + selected_box.transform.translation.x,
    active_cell.world_translation.y + selected_box.transform.translation.y,
    active_cell.world_translation.z + selected_box.transform.translation.z)
end

local function update_grid_nodes(grid_x_nodes, grid_z_nodes, center)
  for index = 1, #grid_x_nodes do
    local offset = -grid_half_extent + (index - 1) * grid_spacing
    set_line_transform(
      grid_x_nodes[index]:GetTransform(),
      hg.Vec3(center.x, grid_height, center.z + offset),
      hg.Vec3(0.0, 0.0, 0.0),
      grid_half_extent * 2,
      grid_line_thickness)
    set_line_transform(
      grid_z_nodes[index]:GetTransform(),
      hg.Vec3(center.x + offset, grid_height, center.z),
      hg.Vec3(0.0, hg.Deg(90), 0.0),
      grid_half_extent * 2,
      grid_line_thickness)
  end
end

local function update_square_outline_nodes(nodes, center, size, height, thickness)
  local half_extent = size * 0.5

  set_line_transform(
    nodes[1]:GetTransform(),
    hg.Vec3(center.x, height, center.z - half_extent),
    hg.Vec3(0.0, 0.0, 0.0),
    size,
    thickness)
  set_line_transform(
    nodes[2]:GetTransform(),
    hg.Vec3(center.x, height, center.z + half_extent),
    hg.Vec3(0.0, 0.0, 0.0),
    size,
    thickness)
  set_line_transform(
    nodes[3]:GetTransform(),
    hg.Vec3(center.x - half_extent, height, center.z),
    hg.Vec3(0.0, hg.Deg(90), 0.0),
    size,
    thickness)
  set_line_transform(
    nodes[4]:GetTransform(),
    hg.Vec3(center.x + half_extent, height, center.z),
    hg.Vec3(0.0, hg.Deg(90), 0.0),
    size,
    thickness)
end

local function compute_translation_axis_length(frame, pivot)
  local camera_position = hg.GetT(frame.camera_world)
  local distance = hg.Len(camera_position - pivot)
  return hg.Clamp(distance * translation_axis_length_factor, translation_axis_min_length, translation_axis_max_length)
end

local function project_world_to_screen(frame, world_position)
  local view_position = frame.view_matrix * world_position
  if view_position.z <= ray_epsilon then
    return false, nil
  end

  return hg.ProjectToScreenSpace(frame.projection_matrix, view_position, frame.resolution)
end

local function point_segment_distance_squared(point_x, point_y, start_x, start_y, end_x, end_y)
  local dx = end_x - start_x
  local dy = end_y - start_y
  local segment_length_squared = dx * dx + dy * dy

  if segment_length_squared <= ray_epsilon then
    local point_dx = point_x - start_x
    local point_dy = point_y - start_y
    return point_dx * point_dx + point_dy * point_dy
  end

  local t = ((point_x - start_x) * dx + (point_y - start_y) * dy) / segment_length_squared
  t = hg.Clamp(t, 0.0, 1.0)

  local projected_x = start_x + dx * t
  local projected_y = start_y + dy * t
  local point_dx = point_x - projected_x
  local point_dy = point_y - projected_y

  return point_dx * point_dx + point_dy * point_dy
end

local function pick_translation_axis(frame, pivot, axis_length, mouse_x, mouse_y)
  local pivot_ok
  local pivot_screen
  pivot_ok, pivot_screen = project_world_to_screen(frame, pivot)
  if not pivot_ok then
    return nil
  end

  local hovered_axis_name = nil
  local nearest_distance_squared = translation_axis_pick_threshold_pixels * translation_axis_pick_threshold_pixels

  for index = 1, #translation_axis_definitions do
    local axis_definition = translation_axis_definitions[index]
    local axis_end = pivot + axis_definition.direction * axis_length
    local axis_end_ok
    local axis_end_screen
    axis_end_ok, axis_end_screen = project_world_to_screen(frame, axis_end)

    if axis_end_ok then
      local distance_squared = point_segment_distance_squared(
        mouse_x,
        mouse_y,
        pivot_screen.x,
        pivot_screen.y,
        axis_end_screen.x,
        axis_end_screen.y)

      if distance_squared <= nearest_distance_squared then
        hovered_axis_name = axis_definition.name
        nearest_distance_squared = distance_squared
      end
    end
  end

  return hovered_axis_name
end

local function intersect_ray_with_plane(ray_origin, ray_direction, plane_point, plane_normal)
  local denominator = hg.Dot(ray_direction, plane_normal)
  if math.abs(denominator) < ray_epsilon then
    return false, nil
  end

  local hit_distance = hg.Dot(plane_point - ray_origin, plane_normal) / denominator
  if hit_distance < 0.0 then
    return false, nil
  end

  return true, ray_origin + ray_direction * hit_distance
end

local function compute_axis_drag_plane_normal(axis_direction, camera_forward)
  local plane_tangent = hg.Cross(camera_forward, axis_direction)
  if hg.Len(plane_tangent) <= ray_epsilon then
    plane_tangent = hg.Cross(hg.Vec3(0.0, 1.0, 0.0), axis_direction)
  end

  if hg.Len(plane_tangent) <= ray_epsilon then
    plane_tangent = hg.Cross(hg.Vec3(1.0, 0.0, 0.0), axis_direction)
  end

  if hg.Len(plane_tangent) <= ray_epsilon then
    return false, nil
  end

  local plane_normal = hg.Cross(axis_direction, plane_tangent)
  if hg.Len(plane_normal) <= ray_epsilon then
    return false, nil
  end

  return true, hg.Normalize(plane_normal)
end

local function apply_axis_value_to_translation(translation, axis_name, value)
  local updated_translation = copy_vec3(translation)

  if axis_name == "x" then
    updated_translation.x = value
  elseif axis_name == "y" then
    updated_translation.y = value
  else
    updated_translation.z = value
  end

  return updated_translation
end

local function get_axis_value_from_translation(translation, axis_name)
  if axis_name == "x" then
    return translation.x
  elseif axis_name == "y" then
    return translation.y
  end

  return translation.z
end

local function get_translation_step(world_state)
  local step = world_state ~= nil and world_state.box_translation_step or nil
  if step == nil then
    return nil
  end

  step = math.abs(step)
  if step <= 0.0 then
    return nil
  end

  return step
end

local function solve_axis_drag_translation(drag_state, ray_origin, ray_direction, step)
  local hit_ok
  local hit_position
  hit_ok, hit_position = intersect_ray_with_plane(
    ray_origin,
    ray_direction,
    drag_state.axis_origin,
    drag_state.plane_normal)
  if not hit_ok then
    return false, nil
  end

  local axis_scalar = hg.Dot(hit_position - drag_state.axis_origin, drag_state.axis_direction)
  local dragged_axis_value = get_axis_value_from_translation(drag_state.start_local_translation, drag_state.axis_name) +
    (axis_scalar - drag_state.start_axis_scalar)
  local snapped_axis_value = snap_to_nearest_step(dragged_axis_value, step)

  return true, apply_axis_value_to_translation(drag_state.start_local_translation, drag_state.axis_name, snapped_axis_value)
end

local function cancel_translation_drag(translation_state)
  translation_state.active_axis_name = nil
  translation_state.drag = {
    active = false,
    axis_name = nil,
    box_index = nil,
    cell_name = nil,
    axis_origin = hg.Vec3(0.0, 0.0, 0.0),
    axis_direction = hg.Vec3(0.0, 0.0, 0.0),
    plane_normal = hg.Vec3(0.0, 1.0, 0.0),
    start_axis_scalar = 0.0,
    start_local_translation = hg.Vec3(0.0, 0.0, 0.0),
    last_translation = hg.Vec3(0.0, 0.0, 0.0),
    had_translation_change = false
  }
end

local function begin_translation_drag(frame, translation_state, axis_definition, active_cell, selected_box, selected_box_index, pivot)
  local plane_ok
  local plane_normal
  plane_ok, plane_normal = compute_axis_drag_plane_normal(axis_definition.direction, frame.camera_forward)
  if not plane_ok then
    return false
  end

  local ray_ok
  local ray_origin
  local ray_direction
  ray_ok, ray_origin, ray_direction = ground_plane.screen_to_world_ray(frame, frame.mouse:X(), frame.mouse:Y())
  if not ray_ok then
    return false
  end

  local hit_ok
  local hit_position
  hit_ok, hit_position = intersect_ray_with_plane(ray_origin, ray_direction, pivot, plane_normal)
  if not hit_ok then
    return false
  end

  translation_state.active_axis_name = axis_definition.name
  translation_state.drag = {
    active = true,
    axis_name = axis_definition.name,
    box_index = selected_box_index,
    cell_name = active_cell.name,
    axis_origin = copy_vec3(pivot),
    axis_direction = copy_vec3(axis_definition.direction),
    plane_normal = plane_normal,
    start_axis_scalar = hg.Dot(hit_position - pivot, axis_definition.direction),
    start_local_translation = copy_vec3(selected_box.transform.translation),
    last_translation = copy_vec3(selected_box.transform.translation),
    had_translation_change = false
  }

  return true
end

local function update_translation_axis_nodes(axis_nodes, pivot, axis_definition, axis_length, axis_name, hovered_axis_name, active_axis_name)
  local is_highlighted = hovered_axis_name == axis_name or active_axis_name == axis_name
  local thickness = is_highlighted and translation_axis_active_thickness or translation_axis_base_thickness
  local axis_position = pivot + axis_definition.direction * (axis_length * 0.5)

  set_line_transform(
    axis_nodes.base:GetTransform(),
    axis_position,
    axis_definition.rotation,
    axis_length,
    thickness)
  set_line_transform(
    axis_nodes.active:GetTransform(),
    axis_position,
    axis_definition.rotation,
    axis_length,
    translation_axis_active_thickness)

  if is_highlighted then
    axis_nodes.base:Disable()
    axis_nodes.active:Enable()
  else
    axis_nodes.base:Enable()
    axis_nodes.active:Disable()
  end
end

local function set_translation_nodes_enabled(translation_state, is_enabled)
  for axis_name, axis_nodes in pairs(translation_state.axis_nodes) do
    if is_enabled then
      if translation_state.hovered_axis_name == axis_name or translation_state.active_axis_name == axis_name then
        axis_nodes.base:Disable()
        axis_nodes.active:Enable()
      else
        axis_nodes.base:Enable()
        axis_nodes.active:Disable()
      end
    else
      axis_nodes.base:Disable()
      axis_nodes.active:Disable()
    end
  end
end

function gizmos.attach(app)
  local scene = app.scene.handle
  local line_ref = app.render.line_ref
  local grid_material = create_material(app, 92, 98, 108)
  local x_axis_material = create_material(app, 176, 72, 72)
  local z_axis_material = create_material(app, 72, 120, 176)
  local placement_cursor_material = create_material(app, 212, 56, 56)
  local translation_active_material = create_material(app, 244, 212, 122)
  local grid_x_nodes
  local grid_z_nodes
  grid_x_nodes, grid_z_nodes = create_grid_nodes(
    scene,
    line_ref,
    grid_material,
    x_axis_material,
    z_axis_material)
  local placement_cursor_nodes = create_square_outline_nodes(scene, line_ref, placement_cursor_material)

  local translation_axis_nodes = {}
  for index = 1, #translation_axis_definitions do
    local axis_definition = translation_axis_definitions[index]
    translation_axis_nodes[axis_definition.name] = create_translation_axis_nodes(
      scene,
      line_ref,
      create_material(app, axis_definition.base_rgb[1], axis_definition.base_rgb[2], axis_definition.base_rgb[3]),
      translation_active_material)
    translation_axis_nodes[axis_definition.name].base:Disable()
  end

  app.gizmos = {
    grid = {
      center = hg.Vec3(0.0, 0.0, 0.0),
      x_nodes = grid_x_nodes,
      z_nodes = grid_z_nodes
    },
    cell_placement_cursor = {
      nodes = placement_cursor_nodes,
      visible = false
    },
    translation = {
      visible = false,
      pivot = hg.Vec3(0.0, 0.0, 0.0),
      axis_length = translation_axis_min_length,
      hovered_axis_name = nil,
      active_axis_name = nil,
      left_button_was_down = false,
      axis_nodes = translation_axis_nodes,
      drag = {}
    }
  }

  cancel_translation_drag(app.gizmos.translation)
end

function gizmos.cancel_translation_drag(app)
  if app == nil or app.gizmos == nil or app.gizmos.translation == nil then
    return
  end

  cancel_translation_drag(app.gizmos.translation)
end

function gizmos.handle_interaction(app, frame)
  if app == nil or app.gizmos == nil or app.gizmos.translation == nil then
    return false, nil
  end

  local translation_state = app.gizmos.translation
  local world_state = get_world_state(app)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_active_selected_box(world_state)

  local left_button_down = frame.mouse:Button(hg.MB_0)
  local clicked_this_frame = left_button_down and not translation_state.left_button_was_down
  local released_this_frame = not left_button_down and translation_state.left_button_was_down

  local target_is_editable = selected_box ~= nil and
    active_cell ~= nil and
    (world_state == nil or world_state.cell_placement == nil or not world_state.cell_placement.active) and
    (world_state == nil or world_state.preview_visible ~= false)

  if not target_is_editable then
    translation_state.visible = false
    translation_state.hovered_axis_name = nil
    cancel_translation_drag(translation_state)
    set_translation_nodes_enabled(translation_state, false)
    translation_state.left_button_was_down = left_button_down
    return false, nil
  end

  local pivot = get_selected_box_world_pivot(active_cell, selected_box)
  translation_state.visible = true
  translation_state.pivot = pivot
  translation_state.axis_length = compute_translation_axis_length(frame, pivot)

  if translation_state.drag.active and
    (translation_state.drag.box_index ~= selected_box_index or translation_state.drag.cell_name ~= active_cell.name) then
    cancel_translation_drag(translation_state)
  end

  if translation_state.drag.active then
    local action = nil
    local ray_ok
    local ray_origin
    local ray_direction
    ray_ok, ray_origin, ray_direction = ground_plane.screen_to_world_ray(frame, frame.mouse:X(), frame.mouse:Y())

    if ray_ok then
      local translation_ok
      local updated_translation
      translation_ok, updated_translation = solve_axis_drag_translation(
        translation_state.drag,
        ray_origin,
        ray_direction,
        get_translation_step(world_state))

      if translation_ok then
        local current_axis_value = get_axis_value_from_translation(translation_state.drag.last_translation, translation_state.drag.axis_name)
        local updated_axis_value = get_axis_value_from_translation(updated_translation, translation_state.drag.axis_name)

        if math.abs(updated_axis_value - current_axis_value) > ray_epsilon then
          translation_state.drag.last_translation = updated_translation
          translation_state.drag.had_translation_change = true
          action = {
            kind = "gizmo_translate_selected_box",
            translation = updated_translation,
            changed = true,
            finalize = false,
            had_translation_change = true
          }
        end
      end
    end

    if released_this_frame then
      local had_translation_change = translation_state.drag.had_translation_change
      cancel_translation_drag(translation_state)
      translation_state.hovered_axis_name = nil
      translation_state.left_button_was_down = left_button_down

      if action ~= nil then
        action.finalize = true
        return true, action
      end

      return true, {
        kind = "gizmo_translate_selected_box",
        changed = false,
        finalize = true,
        had_translation_change = had_translation_change
      }
    end

    translation_state.hovered_axis_name = translation_state.active_axis_name
    translation_state.left_button_was_down = left_button_down
    return true, action
  end

  if hg.ImGuiWantCaptureMouse() then
    translation_state.hovered_axis_name = nil
    translation_state.left_button_was_down = left_button_down
    return false, nil
  end

  translation_state.hovered_axis_name = pick_translation_axis(
    frame,
    translation_state.pivot,
    translation_state.axis_length,
    frame.mouse:X(),
    frame.mouse:Y())

  if clicked_this_frame and translation_state.hovered_axis_name ~= nil then
    local axis_definition = translation_axis_by_name[translation_state.hovered_axis_name]
    local started = begin_translation_drag(
      frame,
      translation_state,
      axis_definition,
      active_cell,
      selected_box,
      selected_box_index,
      pivot)

    translation_state.left_button_was_down = left_button_down

    if started then
      return true, nil
    end
  else
    translation_state.left_button_was_down = left_button_down
  end

  return false, nil
end

function gizmos.update(app, frame)
  local grid_state = app.gizmos.grid
  local cursor_state = app.gizmos.cell_placement_cursor
  local translation_state = app.gizmos.translation
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

  local world_state = get_world_state(app)
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

  local selected_box
  local _
  local active_cell
  selected_box, _, active_cell = get_active_selected_box(world_state)
  local translation_visible = translation_state.visible and selected_box ~= nil and active_cell ~= nil

  if translation_visible then
    for index = 1, #translation_axis_definitions do
      local axis_definition = translation_axis_definitions[index]
      update_translation_axis_nodes(
        translation_state.axis_nodes[axis_definition.name],
        translation_state.pivot,
        axis_definition,
        translation_state.axis_length,
        axis_definition.name,
        translation_state.hovered_axis_name,
        translation_state.active_axis_name)
    end
  end

  set_translation_nodes_enabled(translation_state, translation_visible)
end

function gizmos.debug_solve_axis_translation(start_local_translation, axis_name, axis_origin, camera_forward, start_ray_origin, start_ray_direction, current_ray_origin, current_ray_direction, step)
  local axis_definition = translation_axis_by_name[axis_name]
  if axis_definition == nil then
    return false, nil
  end

  local plane_ok
  local plane_normal
  plane_ok, plane_normal = compute_axis_drag_plane_normal(axis_definition.direction, camera_forward)
  if not plane_ok then
    return false, nil
  end

  local start_hit_ok
  local start_hit
  start_hit_ok, start_hit = intersect_ray_with_plane(start_ray_origin, start_ray_direction, axis_origin, plane_normal)
  if not start_hit_ok then
    return false, nil
  end

  local drag_state = {
    axis_name = axis_name,
    axis_origin = copy_vec3(axis_origin),
    axis_direction = copy_vec3(axis_definition.direction),
    plane_normal = plane_normal,
    start_axis_scalar = hg.Dot(start_hit - axis_origin, axis_definition.direction),
    start_local_translation = copy_vec3(start_local_translation),
    last_translation = copy_vec3(start_local_translation)
  }

  return solve_axis_drag_translation(drag_state, current_ray_origin, current_ray_direction, step)
end

return gizmos
