local hg = require("harfang")
local sdf = require("sdf-generator")

local sdf_scene = {}

local default_scene_path_candidates = {
  "sdf-scenes/frame_006_blockout.sdfscene",
  "sdf-scenes/tile_000.sdfscene"
}
local preview_mode_flat = 0
local preview_mode_wireframe = 1
local add_box_color = {166, 174, 186}
local subtract_box_color = {208, 112, 112}
local wireframe_line_thickness = 0.08

local function file_exists(path)
  local handle = io.open(path, "rb")
  if handle == nil then
    return false
  end

  handle:close()
  return true
end

local function resolve_default_scene_path()
  for index = 1, #default_scene_path_candidates do
    local candidate = default_scene_path_candidates[index]
    if file_exists(candidate) then
      return candidate
    end
  end

  return default_scene_path_candidates[1]
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

local function make_scene_file_state(path)
  local state = {
    path = path,
    scene_file = nil,
    load_error = nil,
    preview_visible = true,
    preview_mode = preview_mode_flat,
    preview_nodes = {
      flat = {},
      wireframe = {}
    },
    materials = {},
    box_count = 0
  }

  local ok, scene_file, error_message = sdf.load_scene_file(path)
  if not ok then
    state.load_error = error_message
    return state
  end

  state.scene_file = scene_file
  state.box_count = #scene_file.scene.boxes
  return state
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

local function update_preview_visibility(state)
  if not state.preview_visible then
    set_preview_visibility(state.preview_nodes.flat, false)
    set_preview_visibility(state.preview_nodes.wireframe, false)
    return
  end

  local show_flat = state.preview_mode == preview_mode_flat
  set_preview_visibility(state.preview_nodes.flat, show_flat)
  set_preview_visibility(state.preview_nodes.wireframe, not show_flat)
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

local function create_preview_nodes(app, state)
  if state.scene_file == nil then
    return
  end

  state.materials.flat_add_box = create_material(app, add_box_color)
  state.materials.flat_subtract_box = create_material(app, subtract_box_color)
  state.materials.wireframe_add_box = create_wireframe_material(app, add_box_color)
  state.materials.wireframe_subtract_box = create_wireframe_material(app, subtract_box_color)

  local scene = app.scene.handle
  local boxes = state.scene_file.scene.boxes
  for index = 1, #boxes do
    local box = boxes[index]
    local is_subtractive = box.op == sdf.CsgOpSubtract
    local flat_material = is_subtractive and state.materials.flat_subtract_box or state.materials.flat_add_box
    local wireframe_material = is_subtractive and state.materials.wireframe_subtract_box or state.materials.wireframe_add_box
    local node = hg.CreateObject(scene, hg.Mat4.Identity, app.render.line_ref, {flat_material})
    local transform = node:GetTransform()
    local translation = hg.Vec3(
      box.transform.translation.x,
      box.transform.translation.y,
      box.transform.translation.z)
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

    state.preview_nodes.flat[#state.preview_nodes.flat + 1] = node
    create_wireframe_box_nodes(
      scene,
      app.render.line_ref,
      wireframe_material,
      translation,
      half_size,
      state.preview_nodes.wireframe)
  end

  update_preview_visibility(state)
end

function sdf_scene.attach(app)
  local state = make_scene_file_state(resolve_default_scene_path())
  create_preview_nodes(app, state)
  app.sdf = state
end

function sdf_scene.update(app, frame)
  local state = app.sdf
  if state == nil then
    return
  end

  hg.ImGuiSetNextWindowPos(hg.Vec2(frame.window_width - 344, 24))
  hg.ImGuiSetNextWindowSize(hg.Vec2(320, 0))

  if hg.ImGuiBegin(
    "SDF Scene",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    hg.ImGuiTextWrapped("Loaded procedural authoring state from app/sdf-scenes/. This preview only displays the source boxes.")

    if state.load_error ~= nil then
      hg.ImGuiTextWrapped("Load error:")
      hg.ImGuiTextWrapped(state.load_error)
    else
      hg.ImGuiText(string.format("Scene: %s", state.scene_file.scene.name))
      hg.ImGuiText(string.format("Boxes: %d", state.box_count))
      hg.ImGuiText(string.format("Meshing mode: %s", sdf.meshing_mode_name(state.scene_file.build_settings.meshing_mode)))
      hg.ImGuiTextWrapped(string.format("Path: %s", state.path))
      hg.ImGuiText("Preview Mode:")

      local preview_mode_changed
      preview_mode_changed, state.preview_mode = hg.ImGuiRadioButton("Flat Shaded", state.preview_mode, preview_mode_flat)
      local wireframe_changed
      wireframe_changed, state.preview_mode = hg.ImGuiRadioButton("Wireframe", state.preview_mode, preview_mode_wireframe)

      local _changed
      _changed, state.preview_visible = hg.ImGuiCheckbox("Show Boxes", state.preview_visible)
      if preview_mode_changed or wireframe_changed or _changed then
        update_preview_visibility(state)
      end
    end
  end

  hg.ImGuiEnd()
end

return sdf_scene
