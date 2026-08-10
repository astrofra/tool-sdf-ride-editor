local hg = require("harfang")
local sdf = require("sdf-generator")
local log_panel = require("editor.log_panel")
local sdf_selection = require("editor.sdf_selection")
local sdf_world = require("editor.sdf_world")
local sdf_cell_factory = require("editor.sdf_cell_factory")
local ground_plane = require("editor.ground_plane")

local sdf_scene = {}

local default_world_path_candidates = {
  "sdf-worlds/default.sdfworld"
}
local preview_mode_flat = 0
local preview_mode_wireframe = 1
local add_box_color = {166, 174, 186}
local subtract_box_color = {208, 112, 112}
local selected_add_box_color = {244, 212, 122}
local selected_subtract_box_color = {255, 166, 128}
local wireframe_line_thickness = 0.08
local box_name_input_max_size = 96
local box_half_size_min = 0.01
local box_value_epsilon = 0.0001
local box_translation_step_min = 0.01
local default_box_translation_step = 1.0
local box_op_items = {"Add", "Subtract"}
local default_new_box_name = "box"
local duplicate_box_name_suffix = "_copy"
local default_new_box_half_extent_ratio = 0.05
local minimum_box_offset = 1.0
local create_preview_nodes_for_cell
local rebuild_cell_preview

local function file_exists(path)
  local handle = io.open(path, "rb")
  if handle == nil then
    return false
  end

  handle:close()
  return true
end

local function normalize_path(path)
  return path:gsub("\\", "/")
end

local function path_dirname(path)
  local normalized_path = normalize_path(path)
  return normalized_path:match("^(.*)/[^/]+$") or ""
end

local function path_join(base, leaf)
  if base == nil or base == "" then
    return leaf
  end

  return base .. "/" .. leaf
end

local function resolve_default_world_path()
  for index = 1, #default_world_path_candidates do
    local candidate = default_world_path_candidates[index]
    if file_exists(candidate) then
      return candidate
    end
  end

  return default_world_path_candidates[1]
end

local function shader_color_channel_from_byte(value, gamma)
  return math.max(0.0, math.min(value / 255.0, 1.0)) ^ gamma
end

local function make_default_shader_color(app, rgb)
  return hg.Vec4(
    shader_color_channel_from_byte(rgb[1], app.theme.default_shader_gamma),
    shader_color_channel_from_byte(rgb[2], app.theme.default_shader_gamma),
    shader_color_channel_from_byte(rgb[3], app.theme.default_shader_gamma),
    1.0)
end

local function create_locked_default_material(app, diffuse_color, specular_color, self_color)
  local material = hg.CreateMaterial(app.render.shader_ref)
  hg.SetMaterialValue(material, "uDiffuseColor", diffuse_color)
  hg.SetMaterialValue(material, "uSpecularColor", specular_color)
  hg.SetMaterialValue(material, "uSelfColor", self_color)
  return material
end

local function create_material(app, rgb)
  local shader_color = make_default_shader_color(app, rgb)
  return create_locked_default_material(
    app,
    shader_color,
    shader_color,
    hg.Vec4(0.0, 0.0, 0.0, 1.0))
end

local function create_wireframe_material(app, rgb)
  return create_locked_default_material(
    app,
    hg.Vec4(0.0, 0.0, 0.0, 1.0),
    hg.Vec4(0.0, 0.0, 0.0, 1.0),
    make_default_shader_color(app, rgb))
end

local function set_preview_visibility(nodes, is_visible)
  for index = 1, #nodes do
    if is_visible then
      nodes[index]:Enable()
    else
      nodes[index]:Disable()
    end
  end
end

local function destroy_preview_nodes(scene, nodes)
  for index = 1, #nodes do
    scene:DestroyNode(nodes[index])
  end
end

local function append_wireframe_edge(nodes, scene, line_ref, material, position, rotation, length)
  local node = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {material})
  local transform = node:GetTransform()

  transform:SetPos(position)
  transform:SetRot(rotation)
  transform:SetScale(hg.Vec3(length, wireframe_line_thickness, wireframe_line_thickness))

  nodes[#nodes + 1] = node
end

local function create_wireframe_box_nodes(scene, line_ref, material, translation, half_size, out_nodes)
  for y_sign = -1, 1, 2 do
    for z_sign = -1, 1, 2 do
      append_wireframe_edge(
        out_nodes,
        scene,
        line_ref,
        material,
        hg.Vec3(
          translation.x,
          translation.y + half_size.y * y_sign,
          translation.z + half_size.z * z_sign),
        hg.Vec3(0, 0, 0),
        half_size.x * 2.0)
    end
  end

  for x_sign = -1, 1, 2 do
    for z_sign = -1, 1, 2 do
      append_wireframe_edge(
        out_nodes,
        scene,
        line_ref,
        material,
        hg.Vec3(
          translation.x + half_size.x * x_sign,
          translation.y,
          translation.z + half_size.z * z_sign),
        hg.Vec3(0, 0, hg.Deg(90)),
        half_size.y * 2.0)
    end
  end

  for x_sign = -1, 1, 2 do
    for y_sign = -1, 1, 2 do
      append_wireframe_edge(
        out_nodes,
        scene,
        line_ref,
        material,
        hg.Vec3(
          translation.x + half_size.x * x_sign,
          translation.y + half_size.y * y_sign,
          translation.z),
        hg.Vec3(0, hg.Deg(90), 0),
        half_size.z * 2.0)
    end
  end
end

local function snap_to_nearest_step(value, step)
  if step <= 0.0 then
    return value
  end

  return math.floor(value / step + 0.5) * step
end

local function snap_world_position_to_cell_grid(world_document, position)
  return hg.Vec3(
    snap_to_nearest_step(position.x, world_document.cell_size),
    0.0,
    snap_to_nearest_step(position.z, world_document.cell_size))
end

local function compute_bounds_span(bounds)
  return {
    x = bounds.max.x - bounds.min.x,
    y = bounds.max.y - bounds.min.y,
    z = bounds.max.z - bounds.min.z
  }
end

local function compute_bounds_overflow(span, max_span)
  return {
    x = math.max(0.0, span.x - max_span),
    y = math.max(0.0, span.y - max_span),
    z = math.max(0.0, span.z - max_span)
  }
end

local function update_cell_bounds_policy_diagnostics(state, cell_state)
  if state.world_document == nil or cell_state.scene_file == nil then
    return
  end

  local span = compute_bounds_span(cell_state.scene_file.build_settings.bounds)
  local overflow = compute_bounds_overflow(span, state.world_document.effective_cell_span)

  cell_state.bounds_span = span
  cell_state.bounds_policy_overflow = overflow
  cell_state.exceeds_world_bounds_policy = overflow.x > 0.0 or overflow.y > 0.0 or overflow.z > 0.0
end

local function get_active_cell(state)
  if state.active_cell_index == nil or state.active_cell_index < 1 or state.active_cell_index > #state.cells then
    return nil
  end

  return state.cells[state.active_cell_index]
end

local function copy_vec3(vec3)
  return hg.Vec3(vec3.x, vec3.y, vec3.z)
end

local function assign_vec3(target, source)
  target.x = source.x
  target.y = source.y
  target.z = source.z
end

local function trim_string(value)
  if value == nil then
    return ""
  end

  return tostring(value):match("^%s*(.-)%s*$")
end

local function clear_box_inspector(state)
  if state.inspector == nil then
    return
  end

  state.inspector.bound_cell_name = nil
  state.inspector.bound_box_index = nil
  state.inspector.name = ""
  state.inspector.op_index = 0
  state.inspector.translation = hg.Vec3(0.0, 0.0, 0.0)
  state.inspector.half_size = hg.Vec3(1.0, 1.0, 1.0)
end

local function get_selected_box_index(state)
  if state.selection == nil then
    return nil
  end

  return state.selection.active_box_index
end

local function clear_selected_box_index(state)
  if state.selection == nil then
    return
  end

  state.selection.active_box_index = nil
  clear_box_inspector(state)
end

local function get_selected_box(state)
  local active_cell = get_active_cell(state)
  local selected_box_index = get_selected_box_index(state)
  if active_cell == nil or active_cell.scene_file == nil or selected_box_index == nil then
    return nil, nil, active_cell
  end

  local boxes = active_cell.scene_file.scene.boxes
  if selected_box_index < 1 or selected_box_index > #boxes then
    return nil, nil, active_cell
  end

  return boxes[selected_box_index], selected_box_index, active_cell
end

local function format_vec3_components(vec3)
  return string.format("%.2f, %.2f, %.2f", vec3.x, vec3.y, vec3.z)
end

local function format_box_op(box)
  if box.op == sdf.CsgOpSubtract then
    return "subtract"
  end

  return "add"
end

local function box_op_to_combo_index(op)
  if op == sdf.CsgOpSubtract then
    return 1
  end

  return 0
end

local function combo_index_to_box_op(index)
  if index == 0 then
    return sdf.CsgOpAdd
  elseif index == 1 then
    return sdf.CsgOpSubtract
  end

  return nil
end

local function clamp_half_size_vec3(half_size)
  local clamped_half_size = hg.Vec3(
    math.max(box_half_size_min, half_size.x),
    math.max(box_half_size_min, half_size.y),
    math.max(box_half_size_min, half_size.z))
  local was_clamped = clamped_half_size.x ~= half_size.x or
    clamped_half_size.y ~= half_size.y or
    clamped_half_size.z ~= half_size.z

  return clamped_half_size, was_clamped
end

local function vec3_approx_equal(lhs, rhs)
  return math.abs(lhs.x - rhs.x) < box_value_epsilon and
    math.abs(lhs.y - rhs.y) < box_value_epsilon and
    math.abs(lhs.z - rhs.z) < box_value_epsilon
end

local function normalize_positive_step(step, minimum_step, fallback_step)
  local numeric_step = tonumber(step)
  if numeric_step == nil then
    return fallback_step
  end

  numeric_step = math.abs(numeric_step)
  if numeric_step < minimum_step then
    return minimum_step
  end

  return numeric_step
end

local function get_box_translation_step(state)
  state.box_translation_step = normalize_positive_step(
    state.box_translation_step,
    box_translation_step_min,
    default_box_translation_step)
  return state.box_translation_step
end

local function snap_translation_vec3_to_step(translation, step)
  return hg.Vec3(
    snap_to_nearest_step(translation.x, step),
    snap_to_nearest_step(translation.y, step),
    snap_to_nearest_step(translation.z, step))
end

local function offset_translation_vec3_along_axis(translation, axis_name, delta)
  local offset_translation = copy_vec3(translation)

  if axis_name == "x" then
    offset_translation.x = offset_translation.x + delta
  elseif axis_name == "y" then
    offset_translation.y = offset_translation.y + delta
  elseif axis_name == "z" then
    offset_translation.z = offset_translation.z + delta
  else
    return nil
  end

  return offset_translation
end

local function sync_inspector_with_selected_box(state)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_selected_box(state)

  if selected_box == nil or active_cell == nil then
    clear_box_inspector(state)
    return nil, nil, nil
  end

  local inspector = state.inspector
  if inspector.bound_cell_name ~= active_cell.name or inspector.bound_box_index ~= selected_box_index then
    inspector.bound_cell_name = active_cell.name
    inspector.bound_box_index = selected_box_index
    inspector.name = selected_box.name
    inspector.op_index = box_op_to_combo_index(selected_box.op)
    inspector.translation = copy_vec3(selected_box.transform.translation)
    inspector.half_size = copy_vec3(selected_box.half_size)
  end

  return inspector, selected_box, active_cell
end

local function reset_inspector_to_selected_box(state)
  if state.inspector == nil then
    return nil, nil, nil
  end

  state.inspector.bound_cell_name = nil
  state.inspector.bound_box_index = nil
  return sync_inspector_with_selected_box(state)
end

local function get_selected_box_inspector(state)
  local inspector = sync_inspector_with_selected_box(state)
  if inspector == nil then
    return nil, "No box is selected"
  end

  return inspector
end

local function normalize_box_update_input(box_data, fallback_box)
  local translation = box_data.translation ~= nil and copy_vec3(box_data.translation) or copy_vec3(fallback_box.transform.translation)
  local requested_half_size = box_data.half_size ~= nil and copy_vec3(box_data.half_size) or copy_vec3(fallback_box.half_size)
  local half_size
  local half_size_was_clamped
  half_size, half_size_was_clamped = clamp_half_size_vec3(requested_half_size)

  local op = box_data.op
  if op == nil then
    local op_index = box_data.op_index ~= nil and box_data.op_index or box_op_to_combo_index(fallback_box.op)
    op = combo_index_to_box_op(op_index)
  end

  if op == nil then
    return nil, "Invalid box operation"
  end

  return {
    name = trim_string(box_data.name ~= nil and box_data.name or fallback_box.name),
    op = op,
    op_index = box_op_to_combo_index(op),
    translation = translation,
    half_size = half_size,
    half_size_was_clamped = half_size_was_clamped
  }, nil
end

local function does_normalized_box_match(box, normalized_box_data)
  return box.name == normalized_box_data.name and
    box.op == normalized_box_data.op and
    vec3_approx_equal(box.transform.translation, normalized_box_data.translation) and
    vec3_approx_equal(box.half_size, normalized_box_data.half_size)
end

local function has_box_name_conflict(boxes, selected_box_index, candidate_name)
  for index = 1, #boxes do
    if index ~= selected_box_index and trim_string(boxes[index].name) == candidate_name then
      return true
    end
  end

  return false
end

local function box_name_exists(boxes, candidate_name)
  return has_box_name_conflict(boxes, nil, candidate_name)
end

local function allocate_unique_box_name(boxes, preferred_name)
  local base_name = trim_string(preferred_name)
  if base_name == "" then
    base_name = default_new_box_name
  end

  if not box_name_exists(boxes, base_name) then
    return base_name
  end

  local index = 1
  while true do
    local candidate_name = string.format("%s_%03d", base_name, index)
    if not box_name_exists(boxes, candidate_name) then
      return candidate_name
    end
    index = index + 1
  end
end

local function make_box_clone(box)
  local clone = sdf.SdfBox()
  clone.name = box.name
  clone.material_id = box.material_id
  clone.op = box.op
  assign_vec3(clone.transform.translation, box.transform.translation)
  assign_vec3(clone.half_size, box.half_size)
  return clone
end

local function make_default_new_box_half_size(state)
  local cell_size = 100.0
  if state.world_document ~= nil and state.world_document.cell_size ~= nil then
    cell_size = state.world_document.cell_size
  end

  local half_extent = math.max(1.0, cell_size * default_new_box_half_extent_ratio)
  return hg.Vec3(half_extent, half_extent, half_extent)
end

local function make_default_add_box(state, boxes, anchor_box)
  local new_box = sdf.SdfBox()
  local half_size = make_default_new_box_half_size(state)
  local translation = hg.Vec3(0.0, half_size.y, 0.0)

  if anchor_box ~= nil then
    translation.x = anchor_box.transform.translation.x + anchor_box.half_size.x + half_size.x + minimum_box_offset
    translation.y = anchor_box.transform.translation.y
    translation.z = anchor_box.transform.translation.z
  end

  new_box.name = allocate_unique_box_name(boxes, default_new_box_name)
  new_box.op = sdf.CsgOpAdd
  assign_vec3(new_box.transform.translation, translation)
  assign_vec3(new_box.half_size, half_size)

  return new_box
end

local function make_duplicate_box(boxes, source_box)
  local duplicate_box = make_box_clone(source_box)
  local offset = math.max(source_box.half_size.x * 2.0, minimum_box_offset)

  duplicate_box.name = allocate_unique_box_name(boxes, source_box.name .. duplicate_box_name_suffix)
  duplicate_box.transform.translation.x = duplicate_box.transform.translation.x + offset

  return duplicate_box
end

local function log_selected_box_state(app, state, prefix)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_selected_box(state)
  if selected_box == nil or active_cell == nil then
    log_panel.info(app, string.format("%s: none", prefix or "Selection"))
    return
  end

  log_panel.info(
    app,
    string.format(
      "%s: %s in %s (box %d, %s, local T %s, half-size %s)",
      prefix or "Selection",
      selected_box.name,
      active_cell.name,
      selected_box_index,
      format_box_op(selected_box),
      format_vec3_components(selected_box.transform.translation),
      format_vec3_components(selected_box.half_size)))
end

local function log_active_cell_state(app, state, prefix)
  local active_cell = get_active_cell(state)
  if active_cell == nil then
    log_panel.warn(app, string.format("%s: no active cell", prefix or "Active cell"))
    return
  end

  if active_cell.load_error ~= nil then
    log_panel.error(app, string.format("%s: %s failed to load (%s)", prefix or "Active cell", active_cell.name, active_cell.load_error))
    return
  end

  log_panel.info(
    app,
    string.format(
      "%s: %s at %s (%d boxes)",
      prefix or "Active cell",
      active_cell.name,
      format_vec3_components(active_cell.world_translation),
      active_cell.box_count))

  if active_cell.exceeds_world_bounds_policy then
    log_panel.warn(
      app,
      string.format(
        "%s exceeds the shared %.2f m bounds policy by x %.2f, y %.2f, z %.2f",
        active_cell.name,
        state.world_document.effective_cell_span,
        active_cell.bounds_policy_overflow.x,
        active_cell.bounds_policy_overflow.y,
        active_cell.bounds_policy_overflow.z))
  end
end

local function log_loaded_world_state(app, state)
  if state.load_error ~= nil then
    log_panel.error(app, state.load_error)
    return
  end

  log_panel.info(
    app,
    string.format(
      "Loaded world %s from %s (%d cells, %d loaded, %d boxes)",
      state.world_document.name,
      state.path,
      #state.cells,
      state.loaded_cell_count,
      state.total_box_count))

  log_active_cell_state(app, state, "Active cell")

  for index = 1, #state.cells do
    local cell_state = state.cells[index]
    if cell_state.load_error ~= nil then
      log_panel.error(app, string.format("%s failed to load (%s)", cell_state.name, cell_state.load_error))
    end
  end
end

local function cancel_cell_placement(state)
  state.cell_placement.active = false
  state.cell_placement.valid = false
end

local function update_preview_visibility(state)
  local active_cell_index = state.active_cell_index

  for index = 1, #state.cells do
    local cell_state = state.cells[index]
    local is_visible = state.preview_visible and (state.show_inactive_cells or index == active_cell_index)
    local show_flat = is_visible and state.preview_mode == preview_mode_flat

    set_preview_visibility(cell_state.preview_nodes.flat, show_flat)
    set_preview_visibility(cell_state.preview_nodes.wireframe, is_visible and not show_flat)
  end
end

local function set_active_cell_index(state, new_index, app, log_prefix)
  local cell_count = #state.cells
  if cell_count == 0 then
    state.active_cell_index = nil
    return
  end

  local previous_index = state.active_cell_index
  local previous_cell = previous_index ~= nil and state.cells[previous_index] or nil
  local had_selection = get_selected_box_index(state) ~= nil

  if new_index < 1 then
    new_index = cell_count
  elseif new_index > cell_count then
    new_index = 1
  end

  if previous_cell ~= nil and had_selection and app ~= nil then
    clear_selected_box_index(state)
    rebuild_cell_preview(app, state, previous_cell)
  else
    clear_selected_box_index(state)
  end

  state.active_cell_index = new_index
  state.world_document.active_cell_index = new_index
  state.world_document.active_cell_name = state.cells[new_index].name
  state.delete_cell_confirmation_armed = false
  update_preview_visibility(state)

  if app ~= nil and previous_index ~= new_index then
    log_active_cell_state(app, state, log_prefix or "Active cell")
  end
end

local function update_cell_placement_cursor(state, frame)
  local placement_state = state.cell_placement
  if not placement_state.active or state.world_document == nil then
    placement_state.valid = false
    return
  end

  if hg.ImGuiWantCaptureMouse() then
    return
  end

  local hit_ok, hit_position = ground_plane.screen_to_ground(frame, frame.mouse:X(), frame.mouse:Y(), 0.0)
  if not hit_ok then
    return
  end

  placement_state.world_position = hit_position
  placement_state.snapped_world_position = snap_world_position_to_cell_grid(state.world_document, hit_position)
  placement_state.valid = true
end

local function handle_cell_placement_confirmation(app, state, frame)
  local placement_state = state.cell_placement
  local left_button_down = frame.mouse:Button(hg.MB_0)
  local clicked_this_frame = left_button_down and not placement_state.left_button_was_down

  placement_state.left_button_was_down = left_button_down

  if not placement_state.active or not placement_state.valid then
    return false
  end

  if hg.ImGuiWantCaptureMouse() then
    return false
  end

  if not clicked_this_frame then
    return false
  end

  local ok = sdf_scene.add_cell_at_position(app, placement_state.snapped_world_position)
  return ok
end

local function set_selected_box_index(app, state, new_index, log_prefix)
  local active_cell = get_active_cell(state)
  local previous_index = get_selected_box_index(state)

  if previous_index == new_index then
    return new_index ~= nil, new_index
  end

  state.selection.active_box_index = new_index

  if active_cell ~= nil and app ~= nil then
    rebuild_cell_preview(app, state, active_cell)
  end

  if app ~= nil then
    if new_index ~= nil then
      log_selected_box_state(app, state, log_prefix or "Selected box")
    elseif previous_index ~= nil then
      log_panel.info(app, string.format("%s cleared", log_prefix or "Selection"))
    end
  end

  return new_index ~= nil, new_index
end

local function handle_box_selection(app, state, frame)
  local selection_state = state.selection
  local left_button_down = frame.mouse:Button(hg.MB_0)
  local clicked_this_frame = left_button_down and not selection_state.left_button_was_down

  selection_state.left_button_was_down = left_button_down

  if state.cell_placement.active or not state.preview_visible then
    return false, nil
  end

  if hg.ImGuiWantCaptureMouse() or not clicked_this_frame then
    return false, nil
  end

  return sdf_scene.select_box_at_viewport_position(app, frame, frame.mouse:X(), frame.mouse:Y())
end

local function make_cell_state(cell_document)
  local cell_state = {
    name = cell_document.name,
    scene_path = cell_document.scene_path,
    world_translation = hg.Vec3(
      cell_document.world_translation.x,
      cell_document.world_translation.y,
      cell_document.world_translation.z),
    scene_file = nil,
    load_error = nil,
    box_count = 0,
    bounds_span = nil,
    bounds_policy_overflow = nil,
    exceeds_world_bounds_policy = false,
    preview_nodes = {
      flat = {},
      wireframe = {}
    }
  }

  local ok, scene_file, error_message = sdf.load_scene_file(cell_state.scene_path)
  if not ok then
    cell_state.load_error = error_message
    return cell_state
  end

  cell_state.scene_file = scene_file
  cell_state.box_count = #scene_file.scene.boxes
  return cell_state
end

local function append_cell_state(state, cell_document)
  local cell_state = make_cell_state(cell_document)
  state.cells[#state.cells + 1] = cell_state

  if cell_state.scene_file ~= nil then
    state.loaded_cell_count = state.loaded_cell_count + 1
    state.total_box_count = state.total_box_count + cell_state.box_count
    update_cell_bounds_policy_diagnostics(state, cell_state)
  end

  return cell_state
end

local function resolve_scene_directory(state)
  if #state.cells > 0 then
    local cell_state = state.cells[1]
    if cell_state ~= nil and cell_state.scene_path ~= nil and cell_state.scene_path ~= "" then
      local directory = path_dirname(cell_state.scene_path)
      if directory ~= "" then
        return directory
      end
    end
  end

  return "sdf-scenes"
end

local function build_known_cell_name_set(state)
  local names = {}
  for index = 1, #state.cells do
    names[state.cells[index].name] = true
  end
  return names
end

local function allocate_new_cell_identity(state)
  local scene_directory = resolve_scene_directory(state)
  local known_names = build_known_cell_name_set(state)
  local index = 0

  while true do
    local cell_name = string.format("tile_%03d", index)
    local scene_path = path_join(scene_directory, cell_name .. ".sdfscene")
    if not known_names[cell_name] and not file_exists(scene_path) then
      return cell_name, scene_path
    end
    index = index + 1
  end
end

local function create_cell_document(cell_name, scene_path, world_position)
  return {
    name = cell_name,
    scene_path = scene_path,
    world_translation = {
      x = world_position.x,
      y = world_position.y,
      z = world_position.z
    }
  }
end

local function create_cell_preview(app, state, cell_document)
  local cell_state = append_cell_state(state, cell_document)
  create_preview_nodes_for_cell(app, state, cell_state)
  update_preview_visibility(state)
  set_active_cell_index(state, #state.cells, app, "Active cell")
end

local function remove_cell_preview(app, cell_state)
  destroy_preview_nodes(app.scene.handle, cell_state.preview_nodes.flat)
  destroy_preview_nodes(app.scene.handle, cell_state.preview_nodes.wireframe)
  cell_state.preview_nodes.flat = {}
  cell_state.preview_nodes.wireframe = {}
end

local function clone_box_list(boxes)
  local copy = sdf.SdfBoxList()

  for index = 1, #boxes do
    copy:push_back(boxes[index])
  end

  return copy
end

local function build_box_list_with_replacement(boxes, replacement_index, replacement_box)
  local updated_boxes = sdf.SdfBoxList()

  for index = 1, #boxes do
    if index == replacement_index then
      updated_boxes:push_back(replacement_box)
    else
      updated_boxes:push_back(boxes[index])
    end
  end

  return updated_boxes
end

local function build_box_list_with_appended_box(boxes, appended_box)
  local updated_boxes = clone_box_list(boxes)
  updated_boxes:push_back(appended_box)
  return updated_boxes
end

local function replace_cell_boxes(state, cell_state, new_boxes)
  local new_box_count = #new_boxes

  cell_state.scene_file.scene.boxes = new_boxes
  state.total_box_count = state.total_box_count - cell_state.box_count + new_box_count
  cell_state.box_count = new_box_count
end

local function remove_cell_state_at_index(app, state, cell_index)
  local cell_state = table.remove(state.cells, cell_index)
  if cell_state == nil then
    return nil
  end

  if cell_state.scene_file ~= nil then
    state.loaded_cell_count = state.loaded_cell_count - 1
    state.total_box_count = state.total_box_count - cell_state.box_count
  end

  remove_cell_preview(app, cell_state)
  return cell_state
end

local function make_world_state(path)
  local state = {
    path = path,
    world_document = nil,
    load_error = nil,
    preview_visible = true,
    preview_mode = preview_mode_flat,
    show_inactive_cells = true,
    active_cell_index = nil,
    cells = {},
    total_box_count = 0,
    loaded_cell_count = 0,
    cell_placement = {
      active = false,
      valid = false,
      world_position = hg.Vec3(0.0, 0.0, 0.0),
      snapped_world_position = hg.Vec3(0.0, 0.0, 0.0),
      left_button_was_down = false
    },
    selection = {
      active_box_index = nil,
      left_button_was_down = false
    },
    inspector = {
      bound_cell_name = nil,
      bound_box_index = nil,
      name = "",
      op_index = 0,
      translation = hg.Vec3(0.0, 0.0, 0.0),
      half_size = hg.Vec3(1.0, 1.0, 1.0)
    },
    box_translation_step = default_box_translation_step,
    delete_cell_confirmation_armed = false,
    materials = {}
  }

  local ok, world_document, error_message = sdf_world.load_world_file(path)
  if not ok then
    state.load_error = error_message
    return state
  end

  state.world_document = world_document
  state.active_cell_index = world_document.active_cell_index

  for index = 1, #world_document.cells do
    append_cell_state(state, world_document.cells[index])
  end

  return state
end

create_preview_nodes_for_cell = function(app, state, cell_state)
  if cell_state.scene_file == nil then
    return
  end

  local scene = app.scene.handle
  local boxes = cell_state.scene_file.scene.boxes
  local active_cell = get_active_cell(state)
  local selected_box_index = get_selected_box_index(state)

  for index = 1, #boxes do
    local box = boxes[index]
    local is_subtractive = box.op == sdf.CsgOpSubtract
    local is_selected = active_cell == cell_state and selected_box_index == index
    local flat_material
    local wireframe_material

    if is_selected then
      flat_material = is_subtractive and state.materials.flat_selected_subtract_box or state.materials.flat_selected_add_box
      wireframe_material = is_subtractive and state.materials.wireframe_selected_subtract_box or state.materials.wireframe_selected_add_box
    else
      flat_material = is_subtractive and state.materials.flat_subtract_box or state.materials.flat_add_box
      wireframe_material = is_subtractive and state.materials.wireframe_subtract_box or state.materials.wireframe_add_box
    end

    local node = hg.CreateObject(scene, hg.Mat4.Identity, app.render.line_ref, {flat_material})
    local transform = node:GetTransform()
    local translation = hg.Vec3(
      cell_state.world_translation.x + box.transform.translation.x,
      cell_state.world_translation.y + box.transform.translation.y,
      cell_state.world_translation.z + box.transform.translation.z)
    local half_size = hg.Vec3(
      box.half_size.x,
      box.half_size.y,
      box.half_size.z)

    transform:SetPos(translation)
    transform:SetRot(hg.Vec3(0, 0, 0))
    transform:SetScale(hg.Vec3(
      half_size.x * 2.0,
      half_size.y * 2.0,
      half_size.z * 2.0))

    cell_state.preview_nodes.flat[#cell_state.preview_nodes.flat + 1] = node
    create_wireframe_box_nodes(
      scene,
      app.render.line_ref,
      wireframe_material,
      translation,
      half_size,
      cell_state.preview_nodes.wireframe)
  end
end

rebuild_cell_preview = function(app, state, cell_state)
  remove_cell_preview(app, cell_state)

  if cell_state.scene_file ~= nil then
    create_preview_nodes_for_cell(app, state, cell_state)
  end

  update_preview_visibility(state)
end

local function create_preview_nodes(app, state)
  if state.world_document == nil then
    return
  end

  state.materials.flat_add_box = create_material(app, add_box_color)
  state.materials.flat_subtract_box = create_material(app, subtract_box_color)
  state.materials.flat_selected_add_box = create_material(app, selected_add_box_color)
  state.materials.flat_selected_subtract_box = create_material(app, selected_subtract_box_color)
  state.materials.wireframe_add_box = create_wireframe_material(app, add_box_color)
  state.materials.wireframe_subtract_box = create_wireframe_material(app, subtract_box_color)
  state.materials.wireframe_selected_add_box = create_wireframe_material(app, selected_add_box_color)
  state.materials.wireframe_selected_subtract_box = create_wireframe_material(app, selected_subtract_box_color)

  for index = 1, #state.cells do
    create_preview_nodes_for_cell(app, state, state.cells[index])
  end

  update_preview_visibility(state)
end

local function add_cell_at_position(app, state, world_position)
  local cell_name, scene_path = allocate_new_cell_identity(state)
  local scene_file = sdf_cell_factory.make_default_scene_file(state.world_document, cell_name)
  local scene_saved, scene_save_error = sdf.save_scene_file(scene_file, scene_path)
  if not scene_saved then
    return false, scene_save_error
  end

  local cell_document = create_cell_document(cell_name, scene_path, world_position)
  local document = state.world_document
  document.cells[#document.cells + 1] = cell_document
  document.active_cell_name = cell_name

  local world_saved, world_save_error = sdf_world.save_world_file(state.path, document)
  if not world_saved then
    os.remove(scene_path)
    document.cells[#document.cells] = nil
    return false, world_save_error
  end

  create_cell_preview(app, state, cell_document)
  return true, cell_name
end

local function save_active_cell_scene_file(app, state, active_cell, original_boxes)
  local scene_saved, scene_save_error = sdf.save_scene_file(active_cell.scene_file, active_cell.scene_path)
  if scene_saved then
    return true, nil
  end

  replace_cell_boxes(state, active_cell, original_boxes)
  rebuild_cell_preview(app, state, active_cell)
  return false, scene_save_error
end

local function add_box(app, state)
  local selected_box
  local active_cell
  selected_box, _, active_cell = get_selected_box(state)

  if active_cell == nil then
    return false, "No active cell is selected"
  end

  if active_cell.scene_file == nil then
    return false, active_cell.load_error or string.format("Active cell %s is not loaded", active_cell.name)
  end

  local original_boxes = clone_box_list(active_cell.scene_file.scene.boxes)
  local new_box = make_default_add_box(state, original_boxes, selected_box)
  local updated_boxes = build_box_list_with_appended_box(original_boxes, new_box)
  local new_box_index = #updated_boxes

  replace_cell_boxes(state, active_cell, updated_boxes)

  local saved, save_error = save_active_cell_scene_file(app, state, active_cell, original_boxes)
  if not saved then
    return false, save_error
  end

  state.selection.active_box_index = new_box_index
  reset_inspector_to_selected_box(state)
  rebuild_cell_preview(app, state, active_cell)

  return true, {
    box_name = new_box.name,
    box_index = new_box_index,
    op = new_box.op,
    translation = copy_vec3(new_box.transform.translation),
    half_size = copy_vec3(new_box.half_size)
  }
end

local function duplicate_selected_box(app, state)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_selected_box(state)

  if active_cell == nil then
    return false, "No active cell is selected"
  end

  if active_cell.scene_file == nil then
    return false, active_cell.load_error or string.format("Active cell %s is not loaded", active_cell.name)
  end

  if selected_box == nil or selected_box_index == nil then
    return false, "No box is selected"
  end

  local original_boxes = clone_box_list(active_cell.scene_file.scene.boxes)
  local duplicated_box = make_duplicate_box(original_boxes, selected_box)
  local updated_boxes = build_box_list_with_appended_box(original_boxes, duplicated_box)
  local duplicated_box_index = #updated_boxes

  replace_cell_boxes(state, active_cell, updated_boxes)

  local saved, save_error = save_active_cell_scene_file(app, state, active_cell, original_boxes)
  if not saved then
    return false, save_error
  end

  state.selection.active_box_index = duplicated_box_index
  reset_inspector_to_selected_box(state)
  rebuild_cell_preview(app, state, active_cell)

  return true, {
    source_box_name = selected_box.name,
    box_name = duplicated_box.name,
    box_index = duplicated_box_index,
    op = duplicated_box.op,
    translation = copy_vec3(duplicated_box.transform.translation),
    half_size = copy_vec3(duplicated_box.half_size)
  }
end

local function update_selected_box(app, state, box_data)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_selected_box(state)

  if active_cell == nil then
    return false, "No active cell is selected"
  end

  if active_cell.scene_file == nil then
    return false, active_cell.load_error or string.format("Active cell %s is not loaded", active_cell.name)
  end

  if selected_box == nil or selected_box_index == nil then
    return false, "No box is selected"
  end

  if box_data == nil then
    return false, "No box data was provided"
  end

  local normalized_box_data
  local normalize_error
  normalized_box_data, normalize_error = normalize_box_update_input(box_data, selected_box)
  if normalized_box_data == nil then
    return false, normalize_error
  end

  if normalized_box_data.name == "" then
    return false, "Box name cannot be empty"
  end

  if has_box_name_conflict(active_cell.scene_file.scene.boxes, selected_box_index, normalized_box_data.name) then
    return false, string.format("Box name %s already exists in %s", normalized_box_data.name, active_cell.name)
  end

  box_data.name = normalized_box_data.name
  box_data.op = normalized_box_data.op
  box_data.op_index = normalized_box_data.op_index
  box_data.translation = copy_vec3(normalized_box_data.translation)
  box_data.half_size = copy_vec3(normalized_box_data.half_size)

  if does_normalized_box_match(selected_box, normalized_box_data) then
    return true, {
      box_name = normalized_box_data.name,
      op = normalized_box_data.op,
      op_index = normalized_box_data.op_index,
      translation = copy_vec3(normalized_box_data.translation),
      half_size = copy_vec3(normalized_box_data.half_size),
      half_size_was_clamped = normalized_box_data.half_size_was_clamped,
      no_changes = true
    }
  end

  local original_boxes = clone_box_list(active_cell.scene_file.scene.boxes)
  local updated_box = original_boxes[selected_box_index]
  updated_box.name = normalized_box_data.name
  updated_box.op = normalized_box_data.op
  assign_vec3(updated_box.transform.translation, normalized_box_data.translation)
  assign_vec3(updated_box.half_size, normalized_box_data.half_size)

  local updated_boxes = build_box_list_with_replacement(original_boxes, selected_box_index, updated_box)
  replace_cell_boxes(state, active_cell, updated_boxes)

  local scene_saved, scene_save_error = sdf.save_scene_file(active_cell.scene_file, active_cell.scene_path)
  if not scene_saved then
    replace_cell_boxes(state, active_cell, original_boxes)
    rebuild_cell_preview(app, state, active_cell)
    return false, scene_save_error
  end

  rebuild_cell_preview(app, state, active_cell)

  return true, {
    box_name = normalized_box_data.name,
    op = normalized_box_data.op,
    op_index = normalized_box_data.op_index,
    translation = copy_vec3(normalized_box_data.translation),
    half_size = copy_vec3(normalized_box_data.half_size),
    half_size_was_clamped = normalized_box_data.half_size_was_clamped,
    no_changes = false
  }
end

local function delete_selected_box(app, state)
  local selected_box
  local selected_box_index
  local active_cell
  selected_box, selected_box_index, active_cell = get_selected_box(state)

  if active_cell == nil then
    return false, "No active cell is selected"
  end

  if active_cell.scene_file == nil then
    return false, active_cell.load_error or string.format("Active cell %s is not loaded", active_cell.name)
  end

  if selected_box == nil or selected_box_index == nil then
    return false, "No box is selected"
  end

  local deleted_box_name = selected_box.name
  local original_boxes = clone_box_list(active_cell.scene_file.scene.boxes)
  local filtered_boxes = sdf.SdfBoxList()

  for index = 1, #original_boxes do
    if index ~= selected_box_index then
      filtered_boxes:push_back(original_boxes[index])
    end
  end

  replace_cell_boxes(state, active_cell, filtered_boxes)

  local scene_saved, scene_save_error = sdf.save_scene_file(active_cell.scene_file, active_cell.scene_path)
  if not scene_saved then
    replace_cell_boxes(state, active_cell, original_boxes)
    rebuild_cell_preview(app, state, active_cell)
    return false, scene_save_error
  end

  clear_selected_box_index(state)
  rebuild_cell_preview(app, state, active_cell)

  return true, deleted_box_name
end

local function delete_active_cell(app, state)
  if state.active_cell_index == nil then
    return false, "No active cell is selected"
  end

  if #state.cells <= 1 then
    return false, "Cannot delete the last remaining cell"
  end

  local delete_index = state.active_cell_index
  local deleted_cell_state = state.cells[delete_index]
  local document = state.world_document
  local previous_active_cell_name = document.active_cell_name
  local replacement_active_cell_name

  if delete_index < #document.cells then
    replacement_active_cell_name = document.cells[delete_index + 1].name
  else
    replacement_active_cell_name = document.cells[delete_index - 1].name
  end

  local deleted_cell_document = table.remove(document.cells, delete_index)
  document.active_cell_name = replacement_active_cell_name

  local world_saved, world_save_error = sdf_world.save_world_file(state.path, document)
  if not world_saved then
    table.insert(document.cells, delete_index, deleted_cell_document)
    document.active_cell_name = previous_active_cell_name
    document.active_cell_index = delete_index
    return false, world_save_error
  end

  clear_selected_box_index(state)
  remove_cell_state_at_index(app, state, delete_index)
  set_active_cell_index(state, math.min(delete_index, #state.cells), app, "Active cell")
  cancel_cell_placement(state)

  local scene_deleted, scene_delete_error = os.remove(deleted_cell_state.scene_path)

  return true, {
    cell_name = deleted_cell_state.name,
    scene_path = deleted_cell_state.scene_path,
    scene_deleted = scene_deleted,
    scene_delete_error = scene_delete_error
  }
end

function sdf_scene.attach(app, world_path)
  log_panel.attach(app)
  local state = make_world_state(world_path or resolve_default_world_path())
  create_preview_nodes(app, state)
  app.sdf = state
  app.sdf_world = state
  log_loaded_world_state(app, state)
end

function sdf_scene.add_cell_at_position(app, world_position)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = add_cell_at_position(app, state, world_position)
  if not ok then
    log_panel.error(app, string.format("Add cell failed: %s", result))
    return false, result
  end

  log_panel.info(
    app,
    string.format(
      "Created cell %s at %.2f, %.2f, %.2f",
      result,
      world_position.x,
      world_position.y,
      world_position.z))
  cancel_cell_placement(state)

  return true, result
end

function sdf_scene.delete_active_cell(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  state.delete_cell_confirmation_armed = false

  local ok, result = delete_active_cell(app, state)
  if not ok then
    log_panel.error(app, string.format("Delete cell failed: %s", result))
    return false, result
  end

  if result.scene_deleted then
    log_panel.info(app, string.format("Deleted cell %s", result.cell_name))
  else
    log_panel.warn(
      app,
      string.format(
        "Deleted cell %s from the world, but could not remove %s (%s)",
        result.cell_name,
        result.scene_path,
        tostring(result.scene_delete_error)))
  end

  return true, result.cell_name
end

function sdf_scene.select_box_at_viewport_position(app, frame, screen_x, screen_y)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local active_cell = get_active_cell(state)
  if active_cell == nil then
    return false, "No active cell is selected"
  end

  if active_cell.scene_file == nil then
    return false, active_cell.load_error or string.format("Active cell %s is not loaded", active_cell.name)
  end

  local selected_box_index = sdf_selection.pick_box_index(active_cell, frame, screen_x, screen_y)
  local did_select = selected_box_index ~= nil
  set_selected_box_index(app, state, selected_box_index, did_select and "Selected box" or "Selection")

  return did_select, selected_box_index
end

function sdf_scene.clear_box_selection(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local had_selection = get_selected_box_index(state) ~= nil
  set_selected_box_index(app, state, nil, "Selection")
  return had_selection, nil
end

function sdf_scene.delete_selected_box(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = delete_selected_box(app, state)
  if not ok then
    log_panel.error(app, string.format("Delete box failed: %s", result))
    return false, result
  end

  log_panel.info(app, string.format("Deleted box %s from %s", result, state.world_document.active_cell_name))
  return true, result
end

function sdf_scene.update_selected_box(app, box_data)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = update_selected_box(app, state, box_data)
  if not ok then
    log_panel.error(app, string.format("Update box failed: %s", result))
    return false, result
  end

  reset_inspector_to_selected_box(state)

  if not result.no_changes then
    local log_prefix = result.half_size_was_clamped and "Updated box (half-size clamped)" or "Updated box"
    log_selected_box_state(app, state, log_prefix)
  end

  return true, result
end

function sdf_scene.add_box(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = add_box(app, state)
  if not ok then
    log_panel.error(app, string.format("Add box failed: %s", result))
    return false, result
  end

  log_selected_box_state(app, state, "Added box")
  return true, result
end

function sdf_scene.duplicate_selected_box(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = duplicate_selected_box(app, state)
  if not ok then
    log_panel.error(app, string.format("Duplicate box failed: %s", result))
    return false, result
  end

  log_selected_box_state(app, state, "Duplicated box")
  return true, result
end

function sdf_scene.nudge_selected_box_translation(app, axis_name, direction)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local inspector, inspector_error = get_selected_box_inspector(state)
  if inspector == nil then
    log_panel.error(app, string.format("Move box failed: %s", inspector_error))
    return false, inspector_error
  end

  local step = get_box_translation_step(state)
  local signed_delta = step * direction
  local nudged_translation = offset_translation_vec3_along_axis(inspector.translation, axis_name, signed_delta)
  if nudged_translation == nil then
    local error_message = string.format("Unsupported translation axis %s", tostring(axis_name))
    log_panel.error(app, string.format("Move box failed: %s", error_message))
    return false, error_message
  end

  inspector.translation = nudged_translation
  return sdf_scene.update_selected_box(app, inspector)
end

function sdf_scene.snap_selected_box_translation(app)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local inspector, inspector_error = get_selected_box_inspector(state)
  if inspector == nil then
    log_panel.error(app, string.format("Snap translation failed: %s", inspector_error))
    return false, inspector_error
  end

  inspector.translation = snap_translation_vec3_to_step(inspector.translation, get_box_translation_step(state))
  return sdf_scene.update_selected_box(app, inspector)
end

function sdf_scene.handle_cell_placement_confirmation(app, frame)
  local state = app.sdf_world or app.sdf
  if state == nil then
    return false
  end

  return handle_cell_placement_confirmation(app, state, frame)
end

function sdf_scene.update(app, frame)
  local state = app.sdf_world or app.sdf
  if state == nil then
    return
  end

  update_cell_placement_cursor(state, frame)

  hg.ImGuiSetNextWindowPos(hg.Vec2(frame.window_width - 344, 24))
  hg.ImGuiSetNextWindowSize(hg.Vec2(320, 0))

  if hg.ImGuiBegin(
    "SDF Scene",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    if state.load_error == nil then
      local active_cell = get_active_cell(state)
      local active_cell_name = active_cell ~= nil and active_cell.name or "<none>"

      if not state.cell_placement.active then
        if hg.ImGuiButton("Add Cell") then
          state.delete_cell_confirmation_armed = false
          state.cell_placement.active = true
          state.cell_placement.valid = false
          log_panel.info(app, "Add cell armed. Move over the ground and left-click, or use Create Cell Here.")
        end
      else
        if hg.ImGuiButton("Cancel Add Cell") then
          cancel_cell_placement(state)
          log_panel.info(app, "Add cell cancelled")
        end
      end

      hg.ImGuiSameLine()
      if not state.delete_cell_confirmation_armed then
        if hg.ImGuiButton("Delete Active Cell") then
          if #state.cells <= 1 then
            log_panel.warn(app, "Cannot delete the last remaining cell")
          else
            cancel_cell_placement(state)
            state.delete_cell_confirmation_armed = true
            log_panel.warn(
              app,
              string.format("Delete armed for %s. Confirm to remove the cell and its scene file.", active_cell_name))
          end
        end
      else
        if hg.ImGuiButton("Confirm Delete Cell") then
          sdf_scene.delete_active_cell(app)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("Cancel Delete") then
          state.delete_cell_confirmation_armed = false
          log_panel.info(app, string.format("Delete cancelled for %s", active_cell_name))
        end
      end

      if state.cell_placement.active and state.cell_placement.valid then
        if hg.ImGuiButton("Create Cell Here") then
          sdf_scene.add_cell_at_position(app, state.cell_placement.snapped_world_position)
        end
      end

      local selected_box = get_selected_box(state)
      if active_cell ~= nil and active_cell.scene_file ~= nil then
        if hg.ImGuiButton("Add Box") then
          sdf_scene.add_box(app)
        end

        if selected_box ~= nil then
          hg.ImGuiSameLine()
          if hg.ImGuiButton("Duplicate Box") then
            sdf_scene.duplicate_selected_box(app)
          end
        end
      end

      if selected_box ~= nil then
        local inspector = sync_inspector_with_selected_box(state)
        hg.ImGuiSeparator()

        local _name_changed
        _name_changed, inspector.name = hg.ImGuiInputText("Name", inspector.name, box_name_input_max_size)
        local _op_changed
        _op_changed, inspector.op_index = hg.ImGuiCombo("Op", inspector.op_index, box_op_items)
        local _translation_changed
        _translation_changed, inspector.translation = hg.ImGuiInputVec3("Local T", inspector.translation, 2)
        local _translation_step_changed
        _translation_step_changed, state.box_translation_step = hg.ImGuiInputFloat("Move Step", state.box_translation_step, 0.1, 1.0, 2)
        state.box_translation_step = get_box_translation_step(state)

        if hg.ImGuiButton("-X") then
          sdf_scene.nudge_selected_box_translation(app, "x", -1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("+X") then
          sdf_scene.nudge_selected_box_translation(app, "x", 1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("-Y") then
          sdf_scene.nudge_selected_box_translation(app, "y", -1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("+Y") then
          sdf_scene.nudge_selected_box_translation(app, "y", 1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("-Z") then
          sdf_scene.nudge_selected_box_translation(app, "z", -1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("+Z") then
          sdf_scene.nudge_selected_box_translation(app, "z", 1.0)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("Snap T") then
          sdf_scene.snap_selected_box_translation(app)
        end

        local _half_size_changed
        _half_size_changed, inspector.half_size = hg.ImGuiInputVec3("Half-Size", inspector.half_size, 2)

        if hg.ImGuiButton("Apply Box") then
          sdf_scene.update_selected_box(app, inspector)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("Revert Box") then
          reset_inspector_to_selected_box(state)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("Delete Selected Box") then
          sdf_scene.delete_selected_box(app)
        end
        hg.ImGuiSameLine()
        if hg.ImGuiButton("Clear Selection") then
          sdf_scene.clear_box_selection(app)
        end
      else
        clear_box_inspector(state)
      end

      hg.ImGuiSeparator()

      local preview_mode_changed
      preview_mode_changed, state.preview_mode = hg.ImGuiRadioButton("Flat Shaded", state.preview_mode, preview_mode_flat)
      local wireframe_changed
      wireframe_changed, state.preview_mode = hg.ImGuiRadioButton("Wireframe", state.preview_mode, preview_mode_wireframe)

      local preview_visibility_changed
      preview_visibility_changed, state.preview_visible = hg.ImGuiCheckbox("Show Boxes", state.preview_visible)
      local inactive_visibility_changed
      inactive_visibility_changed, state.show_inactive_cells = hg.ImGuiCheckbox("Show Inactive Cells", state.show_inactive_cells)

      hg.ImGuiSeparator()

      local active_cell_changed = false
      if hg.ImGuiButton("Previous Cell") then
        set_active_cell_index(state, state.active_cell_index - 1, app, "Active cell")
        active_cell_changed = true
      end
      hg.ImGuiSameLine()
      if hg.ImGuiButton("Next Cell") then
        set_active_cell_index(state, state.active_cell_index + 1, app, "Active cell")
        active_cell_changed = true
      end

      for index = 1, #state.cells do
        local cell_state = state.cells[index]
        if hg.ImGuiSelectable(cell_state.name, index == state.active_cell_index) then
          set_active_cell_index(state, index, app, "Active cell")
          active_cell_changed = true
        end
      end

      if preview_mode_changed or wireframe_changed or preview_visibility_changed or inactive_visibility_changed then
        update_preview_visibility(state)

        if preview_mode_changed or wireframe_changed then
          if state.preview_mode == preview_mode_flat then
            log_panel.info(app, "Preview mode: flat shaded")
          else
            log_panel.info(app, "Preview mode: wireframe")
          end
        end

        if preview_visibility_changed then
          if state.preview_visible then
            log_panel.info(app, "Box preview shown")
          else
            log_panel.info(app, "Box preview hidden")
          end
        end

        if inactive_visibility_changed then
          if state.show_inactive_cells then
            log_panel.info(app, "Inactive cells shown")
          else
            log_panel.info(app, "Inactive cells hidden")
          end
        end
      elseif active_cell_changed then
        update_preview_visibility(state)
      end
    end
  end

  hg.ImGuiEnd()

  local placement_handled = handle_cell_placement_confirmation(app, state, frame)
  if not placement_handled then
    handle_box_selection(app, state, frame)
  end
end

return sdf_scene
