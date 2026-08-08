local hg = require("harfang")

local window_width = 1280
local window_height = 720
local window_title = "SDF Ride Editor"
local imgui_view_id = 255

local grid_half_extent = 25
local grid_spacing = 1
local grid_line_thickness = 0.025
local camera_drive = 0.0
local camera_z_velocity = 0.0
local camera_translation_speed = 16.0
local camera_velocity_response = 7.0
local slider_return_speed = 8.0

local function smooth_towards(current, target, speed, dt)
  local blend = math.min(speed * dt, 1.0)
  return current + (target - current) * blend
end

local function create_material(shader_ref, r, g, b)
  return hg.CreateMaterial(
    shader_ref,
    "uDiffuseColor", hg.Vec4I(r, g, b),
    "uSpecularColor", hg.Vec4I(r, g, b))
end

local function create_grid(scene, resources, shader_ref)
  local vtx_layout = hg.VertexLayoutPosFloatNormUInt8()
  local line_length = grid_half_extent * 2
  local line_count = math.floor(line_length / grid_spacing)

  local x_line_ref = resources:AddModel(
    "grid_line_x",
    hg.CreateCubeModel(vtx_layout, line_length, grid_line_thickness, grid_line_thickness))
  local z_line_ref = resources:AddModel(
    "grid_line_z",
    hg.CreateCubeModel(vtx_layout, grid_line_thickness, grid_line_thickness, line_length))

  local grid_material = create_material(shader_ref, 92, 98, 108)
  local x_axis_material = create_material(shader_ref, 176, 72, 72)
  local z_axis_material = create_material(shader_ref, 72, 120, 176)

  for index = 0, line_count do
    local coordinate = -grid_half_extent + index * grid_spacing
    local x_line_material = grid_material
    local z_line_material = grid_material

    if coordinate == 0 then
      x_line_material = x_axis_material
      z_line_material = z_axis_material
    end

    hg.CreateObject(
      scene,
      hg.TranslationMat4(hg.Vec3(0, 0, coordinate)),
      x_line_ref,
      {x_line_material})

    hg.CreateObject(
      scene,
      hg.TranslationMat4(hg.Vec3(coordinate, 0, 0)),
      z_line_ref,
      {z_line_material})
  end
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

local scene = hg.Scene()
scene.canvas.color = hg.ColorI(24, 28, 34)
scene.environment.ambient = hg.Color(0.3, 0.3, 0.32, 1.0)

create_grid(scene, resources, shader_ref)

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

  scene:Update(dt_clock)
  hg.SubmitSceneToPipeline(0, scene, hg.IntRect(0, 0, window_width, window_height), true, pipeline, resources)
  hg.ImGuiEndFrame(imgui_view_id)

  hg.Frame()
  hg.UpdateWindow(window)
end

hg.DestroyForwardPipeline(pipeline)
hg.RenderShutdown()
hg.DestroyWindow(window)
hg.WindowSystemShutdown()
hg.InputShutdown()
