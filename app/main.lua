local hg = require("harfang")

local window_width = 1280
local window_height = 720
local window_title = "SDF Ride Editor"
local label_view_id = 254
local imgui_view_id = 255

local grid_half_extent = 25
local grid_spacing = 1
local grid_line_thickness = 0.025
local grid_height = 0.0
local measurement_height = 0.06
local measurement_arrow_length = 1.25
local measurement_arrow_angle = math.rad(28)
local measurement_label_offset = 0.65
local measurement_label_lift = 0.02
local measurement_label_scale = 0.02
local camera_drive = 0.0
local camera_z_velocity = 0.0
local camera_translation_speed = 16.0
local camera_velocity_response = 7.0
local slider_return_speed = 8.0
local grid_center = hg.Vec3(0, 0, 0)
local scene_origin = hg.Vec3(0, 0, 0)

local function smooth_towards(current, target, speed, dt)
  local blend = math.min(speed * dt, 1.0)
  return current + (target - current) * blend
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

local function create_material(shader_ref, r, g, b)
  return hg.CreateMaterial(
    shader_ref,
    "uDiffuseColor", hg.Vec4I(r, g, b),
    "uSpecularColor", hg.Vec4I(r, g, b))
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

local function compute_measurement_label_transform(start_pos, end_pos)
  local delta = end_pos - start_pos
  local distance = hg.Len(delta)
  local line_direction = distance > 0.0001 and delta / distance or hg.Vec3(1, 0, 0)
  local line_yaw = yaw_from_xz(line_direction)
  local line_normal = hg.Vec3(line_direction.z, 0, -line_direction.x)
  local mid = (start_pos + end_pos) * 0.5
  local label_position = hg.Vec3(
    mid.x + line_normal.x * measurement_label_offset,
    measurement_height + measurement_label_lift,
    mid.z + line_normal.z * measurement_label_offset)
  local label_matrix = hg.TransformationMat4(
    hg.Vec3(0, 0, 0),
    hg.Vec3(hg.Deg(-90), -line_yaw, 0),
    hg.Vec3(measurement_label_scale, measurement_label_scale, measurement_label_scale))

  return label_position, label_matrix
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

local function create_measurement_nodes(scene, line_ref, measurement_material)
  return {
    shaft = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    start_a = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    start_b = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    end_a = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material}),
    end_b = hg.CreateObject(scene, hg.Mat4.Identity, line_ref, {measurement_material})
  }
end

local function update_measurement_nodes(nodes, start_pos, end_pos)
  local delta = end_pos - start_pos
  local distance = hg.Len(delta)
  local line_direction = distance > 0.0001 and delta / distance or hg.Vec3(1, 0, 0)
  local line_yaw = yaw_from_xz(line_direction)
  local mid = (start_pos + end_pos) * 0.5

  set_line_transform(
    nodes.shaft:GetTransform(),
    hg.Vec3(mid.x, measurement_height, mid.z),
    line_yaw,
    math.max(distance, 0.001),
    grid_line_thickness * 1.35)

  local arrow_length = math.min(measurement_arrow_length, math.max(distance * 0.25, 0.35))
  local start_dir_a = rotate_xz(line_direction, measurement_arrow_angle)
  local start_dir_b = rotate_xz(line_direction, -measurement_arrow_angle)
  local end_base_dir = hg.Vec3(-line_direction.x, -line_direction.y, -line_direction.z)
  local end_dir_a = rotate_xz(end_base_dir, measurement_arrow_angle)
  local end_dir_b = rotate_xz(end_base_dir, -measurement_arrow_angle)

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

  return distance
end

hg.InputInit()
hg.WindowSystemInit()

local render_flags = hg.RF_VSync | hg.RF_MSAA4X
local window = hg.RenderInit(window_title, window_width, window_height, render_flags)

hg.AddAssetsFolder("assets_compiled")
hg.ImGuiInit(
  10,
  hg.LoadProgramFromAssets("core/shader/imgui"),
  hg.LoadProgramFromAssets("core/shader/imgui_image"))

local pipeline = hg.CreateForwardPipeline()
local resources = hg.PipelineResources()
local shader_ref = hg.LoadPipelineProgramRefFromAssets(
  "core/shader/default.hps",
  resources,
  hg.GetForwardPipelineInfo())
local font = hg.LoadFontFromAssets("fonts/spacemono-regular.ttf", 40)
local font_program = hg.LoadProgramFromAssets("core/shader/font")
local text_uniform_values = {hg.MakeUniformSetValue("u_color", hg.Vec4(0.92, 0.92, 0.95, 1.0))}
local text_render_state = hg.ComputeRenderState(hg.BM_Alpha, hg.DT_LessEqual, hg.FC_Disabled, false)
local line_model = hg.CreateCubeModel(hg.VertexLayoutPosFloatNormUInt8(), 1, 1, 1)
local line_ref = resources:AddModel("editor_line_unit", line_model)
local grid_material = create_material(shader_ref, 92, 98, 108)
local x_axis_material = create_material(shader_ref, 176, 72, 72)
local z_axis_material = create_material(shader_ref, 72, 120, 176)
local measurement_material = create_material(shader_ref, 232, 232, 240)

local scene = hg.Scene()
scene.canvas.color = hg.ColorI(24, 28, 34)
scene.environment.ambient = hg.Color(0.3, 0.3, 0.32, 1.0)

local grid_x_nodes, grid_z_nodes = create_grid_nodes(
  scene,
  line_ref,
  grid_material,
  x_axis_material,
  z_axis_material)
local measurement_nodes = create_measurement_nodes(scene, line_ref, measurement_material)

local camera_position = hg.Vec3(18, 16, -18)
local camera = hg.CreateCamera(
  scene,
  hg.Mat4LookAt(camera_position, hg.Vec3(0, 0, 0)),
  0.1,
  500.0,
  hg.Deg(45))
scene:SetCurrentCamera(camera)
local camera_transform = camera:GetTransform()

hg.CreatePointLight(
  scene,
  hg.TranslationMat4(hg.Vec3(0, 18, 0)),
  80,
  hg.ColorI(255, 244, 228),
  hg.Color.Black,
  0)

hg.CreatePointLight(
  scene,
  hg.TranslationMat4(hg.Vec3(-16, 10, -16)),
  60,
  hg.ColorI(128, 164, 255),
  hg.Color.Black,
  0)

while hg.IsWindowOpen(window) do
  local render_was_reset
  render_was_reset, window_width, window_height = hg.RenderResetToWindow(window, window_width, window_height, render_flags)

  local mouse_state = hg.ReadMouse()
  local keyboard_state = hg.ReadKeyboard()
  if keyboard_state:Key(hg.K_Escape) then
    break
  end

  local dt_clock = hg.TickClock()
  local dt = hg.time_to_sec_f(dt_clock)

  hg.ImGuiBeginFrame(window_width, window_height, dt_clock, mouse_state, keyboard_state)

  hg.ImGuiSetNextWindowPos(hg.Vec2(24, window_height - 104))
  hg.ImGuiSetNextWindowSize(hg.Vec2(360, 0))

  local slider_is_active = false
  if hg.ImGuiBegin(
    "Camera Transport",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    hg.ImGuiTextWrapped("Push left to move backward on world Z, right to move forward. Releasing recenters the control.")
    _, camera_drive = hg.ImGuiSliderFloat("Z Drive", camera_drive, -1.0, 1.0, "%.2f")
    slider_is_active = hg.ImGuiIsItemActive()
  end
  hg.ImGuiEnd()

  if not slider_is_active then
    camera_drive = smooth_towards(camera_drive, 0.0, slider_return_speed, dt)
    if math.abs(camera_drive) < 0.001 then
      camera_drive = 0.0
    end
  end

  local target_z_velocity = camera_drive * camera_translation_speed
  camera_z_velocity = smooth_towards(camera_z_velocity, target_z_velocity, camera_velocity_response, dt)
  if math.abs(camera_z_velocity) < 0.001 and math.abs(camera_drive) < 0.001 then
    camera_z_velocity = 0.0
  end

  camera_position.z = camera_position.z + camera_z_velocity * dt
  camera_transform:SetPos(camera_position)

  local camera_world = camera_transform:GetWorld()
  local camera_forward = hg.Normalize(hg.GetZ(camera_world))
  grid_center = intersect_ground_plane(hg.GetT(camera_world), camera_forward, grid_center)
  grid_center.y = 0
  update_grid_nodes(grid_x_nodes, grid_z_nodes, grid_center)
  local measurement_distance = update_measurement_nodes(
    measurement_nodes,
    grid_center,
    scene_origin)

  local camera_component = camera:GetCamera()
  local view_matrix = hg.InverseFast(camera_world)
  local projection_matrix = hg.ComputePerspectiveProjectionMatrix(
    camera_component:GetZNear(),
    camera_component:GetZFar(),
    hg.FovToZoomFactor(camera_component:GetFov()),
    hg.ComputeAspectRatioX(window_width, window_height))
  local measurement_start_world = hg.Vec3(grid_center.x, measurement_height, grid_center.z)
  local measurement_end_world = hg.Vec3(scene_origin.x, measurement_height, scene_origin.z)
  local label_position, label_matrix = compute_measurement_label_transform(
    measurement_start_world,
    measurement_end_world)
  local label_text = string.format("%.0fm", measurement_distance)

  scene:Update(dt_clock)
  hg.SubmitSceneToPipeline(0, scene, hg.IntRect(0, 0, window_width, window_height), true, pipeline, resources)
  hg.SetViewRect(label_view_id, 0, 0, window_width, window_height)
  hg.SetViewClear(label_view_id, 0)
  hg.SetViewTransform(label_view_id, view_matrix, projection_matrix)
  hg.DrawText(
    label_view_id,
    font,
    label_text,
    font_program,
    "u_tex",
    0,
    label_matrix,
    label_position,
    hg.DTHA_Center,
    hg.DTVA_Center,
    text_uniform_values,
    {},
    text_render_state)
  hg.ImGuiEndFrame(imgui_view_id)

  hg.Frame()
  hg.UpdateWindow(window)
end

hg.DestroyForwardPipeline(pipeline)
hg.RenderShutdown()
hg.DestroyWindow(window)
hg.WindowSystemShutdown()
hg.InputShutdown()
