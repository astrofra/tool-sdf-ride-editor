local hg = require("harfang")
local sdf = require("sdf-generator")
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
local wireframe_line_thickness = 0.08
local create_preview_nodes_for_cell

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

local function create_material(app, rgb)
  local shader_color = make_default_shader_color(app, rgb)
  return hg.CreateMaterial(
    app.render.shader_ref,
    "uDiffuseColor", shader_color,
    "uSpecularColor", shader_color)
end

local function create_wireframe_material(app, rgb)
  local material = hg.CreateMaterial(
    app.render.shader_ref,
    "uDiffuseColor", hg.Vec4(0.0, 0.0, 0.0, 1.0),
    "uSpecularColor", hg.Vec4(0.0, 0.0, 0.0, 1.0))
  hg.SetMaterialValue(material, "uSelfColor", make_default_shader_color(app, rgb))
  return material
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

local function set_active_cell_index(state, new_index)
  local cell_count = #state.cells
  if cell_count == 0 then
    state.active_cell_index = nil
    return
  end

  if new_index < 1 then
    new_index = cell_count
  elseif new_index > cell_count then
    new_index = 1
  end

  state.active_cell_index = new_index
  state.world_document.active_cell_index = new_index
  state.world_document.active_cell_name = state.cells[new_index].name
  update_preview_visibility(state)
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
  set_active_cell_index(state, #state.cells)
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
    materials = {},
    action_error = nil,
    status_message = nil
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
  for index = 1, #boxes do
    local box = boxes[index]
    local is_subtractive = box.op == sdf.CsgOpSubtract
    local flat_material = is_subtractive and state.materials.flat_subtract_box or state.materials.flat_add_box
    local wireframe_material = is_subtractive and state.materials.wireframe_subtract_box or state.materials.wireframe_add_box
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

local function create_preview_nodes(app, state)
  if state.world_document == nil then
    return
  end

  state.materials.flat_add_box = create_material(app, add_box_color)
  state.materials.flat_subtract_box = create_material(app, subtract_box_color)
  state.materials.wireframe_add_box = create_wireframe_material(app, add_box_color)
  state.materials.wireframe_subtract_box = create_wireframe_material(app, subtract_box_color)

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

function sdf_scene.attach(app, world_path)
  local state = make_world_state(world_path or resolve_default_world_path())
  create_preview_nodes(app, state)
  app.sdf = state
  app.sdf_world = state
end

function sdf_scene.add_cell_at_position(app, world_position)
  local state = app.sdf_world or app.sdf
  if state == nil or state.world_document == nil then
    return false, "World state is not initialized"
  end

  local ok, result = add_cell_at_position(app, state, world_position)
  if not ok then
    state.action_error = result
    state.status_message = nil
    return false, result
  end

  state.action_error = nil
  state.status_message = string.format(
    "Created cell %s at %.2f, %.2f, %.2f",
    result,
    world_position.x,
    world_position.y,
    world_position.z)
  state.cell_placement.active = false
  state.cell_placement.valid = false

  return true, result
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
    hg.ImGuiTextWrapped(
      "Cells keep their objects in local space. The world document owns cell placement and shared envelope metadata.")

    if state.load_error ~= nil then
      hg.ImGuiTextWrapped("Load error:")
      hg.ImGuiTextWrapped(state.load_error)
    else
      if state.action_error ~= nil then
        hg.ImGuiTextWrapped(string.format("Action Error: %s", state.action_error))
      elseif state.status_message ~= nil then
        hg.ImGuiTextWrapped(state.status_message)
      end

      local active_cell = get_active_cell(state)
      local active_cell_name = active_cell ~= nil and active_cell.name or "<none>"

      hg.ImGuiText(string.format("World: %s", state.world_document.name))
      hg.ImGuiText(string.format("Cells: %d (%d loaded)", #state.cells, state.loaded_cell_count))
      hg.ImGuiText(string.format("Total Boxes: %d", state.total_box_count))
      hg.ImGuiText(string.format("Cell Size: %.2f", state.world_document.cell_size))
      hg.ImGuiText(string.format("Cell Bounds Padding: %.2f", state.world_document.cell_bounds_padding))
      hg.ImGuiText(string.format("Effective Bounds Span: %.2f", state.world_document.effective_cell_span))
      hg.ImGuiText(string.format("Active Cell: %s", active_cell_name))
      hg.ImGuiTextWrapped(string.format("World Path: %s", state.path))
      hg.ImGuiSeparator()
      if not state.cell_placement.active then
        if hg.ImGuiButton("Add Cell") then
          state.cell_placement.active = true
          state.cell_placement.valid = false
        end
      else
        if hg.ImGuiButton("Cancel Add Cell") then
          state.cell_placement.active = false
          state.cell_placement.valid = false
        end
      end

      if state.cell_placement.active then
        hg.ImGuiTextWrapped("Placement mode is armed. Move the mouse over the ground in the 3D viewport. Left-click in the viewport to create the cell directly. The red square shows the next cell footprint snapped on the cell grid.")
        if state.cell_placement.valid then
          hg.ImGuiText(string.format(
            "Next Cell Center: %.2f, %.2f, %.2f",
            state.cell_placement.snapped_world_position.x,
            state.cell_placement.snapped_world_position.y,
            state.cell_placement.snapped_world_position.z))
          if hg.ImGuiButton("Create Cell Here") then
            sdf_scene.add_cell_at_position(app, state.cell_placement.snapped_world_position)
          end
        else
          hg.ImGuiTextWrapped("Placement cursor is waiting for a valid ground hit in the 3D viewport.")
        end
        hg.ImGuiSeparator()
      end

      hg.ImGuiText("Preview Mode:")

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
        set_active_cell_index(state, state.active_cell_index - 1)
        active_cell_changed = true
      end
      hg.ImGuiSameLine()
      if hg.ImGuiButton("Next Cell") then
        set_active_cell_index(state, state.active_cell_index + 1)
        active_cell_changed = true
      end

      for index = 1, #state.cells do
        local cell_state = state.cells[index]
        local label
        if cell_state.load_error ~= nil then
          label = string.format("%s [load error]", cell_state.name)
        else
          label = string.format("%s (%d boxes)", cell_state.name, cell_state.box_count)
          if cell_state.exceeds_world_bounds_policy then
            label = label .. " [bounds > policy]"
          end
        end

        if hg.ImGuiSelectable(label, index == state.active_cell_index) then
          set_active_cell_index(state, index)
          active_cell_changed = true
        end
      end

      active_cell = get_active_cell(state)
      if active_cell ~= nil then
        hg.ImGuiSeparator()
        hg.ImGuiTextWrapped(string.format("Active Scene Path: %s", active_cell.scene_path))
        hg.ImGuiText(string.format(
          "Active Cell World T: %.2f, %.2f, %.2f",
          active_cell.world_translation.x,
          active_cell.world_translation.y,
          active_cell.world_translation.z))

        if active_cell.load_error ~= nil then
          hg.ImGuiTextWrapped("Active cell load error:")
          hg.ImGuiTextWrapped(active_cell.load_error)
        elseif active_cell.scene_file ~= nil then
          hg.ImGuiText(string.format("Active Scene: %s", active_cell.scene_file.scene.name))
          hg.ImGuiText(string.format(
            "Meshing Mode: %s",
            sdf.meshing_mode_name(active_cell.scene_file.build_settings.meshing_mode)))
          if active_cell.bounds_span ~= nil then
            hg.ImGuiText(string.format(
              "Scene Bounds Span: %.2f x %.2f x %.2f",
              active_cell.bounds_span.x,
              active_cell.bounds_span.y,
              active_cell.bounds_span.z))
            if active_cell.exceeds_world_bounds_policy then
              hg.ImGuiTextWrapped(string.format(
                "Bounds policy warning: scene bounds exceed the current world envelope (max span %.2f m). Overflow: x %.2f, y %.2f, z %.2f. Current clipping still follows this scene file's bounds.",
                state.world_document.effective_cell_span,
                active_cell.bounds_policy_overflow.x,
                active_cell.bounds_policy_overflow.y,
                active_cell.bounds_policy_overflow.z))
            else
              hg.ImGuiText(string.format(
                "Bounds Policy: within %.2f m envelope",
                state.world_document.effective_cell_span))
            end
          end
        end
      end

      if preview_mode_changed or wireframe_changed or preview_visibility_changed or inactive_visibility_changed then
        update_preview_visibility(state)
      elseif active_cell_changed then
        update_preview_visibility(state)
      end
    end
  end

  hg.ImGuiEnd()

  handle_cell_placement_confirmation(app, state, frame)
end

return sdf_scene
