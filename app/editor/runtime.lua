local hg = require("harfang")
local ground_plane = require("editor.ground_plane")

local runtime = {}

local window_defaults = {
  width = 1280,
  height = 720,
  title = "SDF Ride Editor"
}

local background_clear = {24, 28, 34}
local imgui_view_id = 255
local follow_spotlight_height = 200.0
local follow_spotlight_radius = 450.0
local follow_spotlight_inner_angle = hg.Deg(30)
local follow_spotlight_outer_angle = hg.Deg(45)
local follow_spotlight_priority = 1.0
local follow_spotlight_shadow_bias = 0.00005
local follow_spotlight_shadow_near = 1.0
local follow_spotlight_shadow_far = 450.0
local follow_spotlight_diffuse = hg.Color(1.0, 0.98, 0.94, 1.0)
local follow_spotlight_specular = hg.Color.White

local function make_follow_spotlight_position(target)
  return hg.Vec3(target.x, target.y + follow_spotlight_height, target.z)
end

local function compute_follow_spotlight_target(app, frame)
  if frame == nil or frame.window_width == nil or frame.window_height == nil then
    return hg.Vec3(app.scene.origin.x, 0.0, app.scene.origin.z)
  end

  local hit_ok, hit_position = ground_plane.screen_to_ground(
    frame,
    frame.window_width * 0.5,
    frame.window_height * 0.5,
    0.0)
  if hit_ok then
    return hit_position
  end

  return hg.Vec3(app.scene.origin.x, 0.0, app.scene.origin.z)
end

local function update_follow_spotlight(app, frame)
  local spotlight = app.scene.follow_spotlight
  if spotlight == nil then
    return
  end

  local target = compute_follow_spotlight_target(app, frame)
  local position = make_follow_spotlight_position(target)
  local direction = hg.Normalize(target - position)

  spotlight.target = target
  spotlight.transform:SetPos(position)
  spotlight.transform:SetRot(hg.ToEuler(hg.Mat3LookAt(direction)))
end

function runtime.create()
  hg.InputInit()
  hg.WindowSystemInit()

  local render_flags = hg.RF_VSync | hg.RF_MSAA4X
  local window = hg.RenderInit(
    window_defaults.title,
    window_defaults.width,
    window_defaults.height,
    render_flags)

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
  local line_model = hg.CreateCubeModel(hg.VertexLayoutPosFloatNormUInt8(), 1, 1, 1)
  local line_ref = resources:AddModel("editor_line_unit", line_model)

  local app = {
    window = {
      handle = window,
      width = window_defaults.width,
      height = window_defaults.height,
      title = window_defaults.title
    },
    theme = {
      background_clear = background_clear,
      default_shader_gamma = 2.2
    },
    render = {
      flags = render_flags,
      pipeline = pipeline,
      resources = resources,
      shader_ref = shader_ref,
      line_ref = line_ref,
      view_ids = {
        imgui = imgui_view_id
      }
    },
    scene = {
      origin = hg.Vec3(0, 0, 0),
      camera_position = hg.Vec3(18, 16, -18)
    }
  }

  local scene = hg.Scene()
  scene.canvas.color = hg.ColorI(background_clear[1], background_clear[2], background_clear[3])
  scene.environment.ambient = hg.Color(0.3, 0.3, 0.32, 1.0)
  app.scene.handle = scene

  app.scene.camera = hg.CreateCamera(
    scene,
    hg.Mat4LookAt(app.scene.camera_position, hg.Vec3(0, 0, 0)),
    0.1,
    500.0,
    hg.Deg(45))
  scene:SetCurrentCamera(app.scene.camera)
  app.scene.camera_transform = app.scene.camera:GetTransform()

  local follow_spotlight_node = hg.CreateSpotLight(
    scene,
    hg.Mat4LookAt(
      hg.Vec3(0.0, follow_spotlight_height, 0.0),
      hg.Vec3(0.0, 0.0, 0.0)),
    follow_spotlight_radius,
    follow_spotlight_inner_angle,
    follow_spotlight_outer_angle,
    follow_spotlight_diffuse,
    follow_spotlight_specular,
    follow_spotlight_priority,
    hg.LST_Map,
    follow_spotlight_shadow_bias,
    follow_spotlight_shadow_near,
    follow_spotlight_shadow_far)

  app.scene.follow_spotlight = {
    node = follow_spotlight_node,
    transform = follow_spotlight_node:GetTransform(),
    target = hg.Vec3(0.0, 0.0, 0.0)
  }

  return app
end

function runtime.is_running(app)
  return hg.IsWindowOpen(app.window.handle)
end

function runtime.begin_frame(app)
  local _render_was_reset
  _render_was_reset, app.window.width, app.window.height = hg.RenderResetToWindow(
    app.window.handle,
    app.window.width,
    app.window.height,
    app.render.flags)

  local frame = {
    window_width = app.window.width,
    window_height = app.window.height,
    mouse = hg.ReadMouse(),
    keyboard = hg.ReadKeyboard(),
    exit_requested = false,
    skip_frame = app.window.width <= 0 or app.window.height <= 0
  }

  if frame.keyboard:Key(hg.K_Escape) then
    frame.exit_requested = true
    return frame
  end

  if frame.skip_frame then
    return frame
  end

  frame.dt_clock = hg.TickClock()
  frame.dt = hg.time_to_sec_f(frame.dt_clock)
  frame.resolution = hg.Vec2(frame.window_width, frame.window_height)

  hg.ImGuiBeginFrame(
    frame.window_width,
    frame.window_height,
    frame.dt_clock,
    frame.mouse,
    frame.keyboard)

  return frame
end

function runtime.prepare_camera_frame(app, frame)
  local camera_world = hg.TransformationMat4(
    app.scene.camera_transform:GetPos(),
    app.scene.camera_transform:GetRot())
  local camera_component = app.scene.camera:GetCamera()
  local view_matrix = hg.InverseFast(camera_world)
  local projection_matrix = hg.ComputePerspectiveProjectionMatrix(
    camera_component:GetZNear(),
    camera_component:GetZFar(),
    hg.FovToZoomFactor(camera_component:GetFov()),
    hg.ComputeAspectRatioX(frame.window_width, frame.window_height))
  local inverse_projection_matrix
  local inverse_projection_ok
  inverse_projection_matrix, inverse_projection_ok = hg.Inverse(projection_matrix)

  frame.camera_world = camera_world
  frame.camera_component = camera_component
  frame.camera_forward = hg.Normalize(hg.GetZ(camera_world))
  frame.view_matrix = view_matrix
  frame.projection_matrix = projection_matrix
  frame.inverse_projection_matrix = inverse_projection_matrix
  frame.inverse_projection_ok = inverse_projection_ok

  return frame
end

function runtime.render_scene(app, frame)
  update_follow_spotlight(app, frame)
  app.scene.handle:Update(frame.dt_clock)
  hg.SubmitSceneToPipeline(
    0,
    app.scene.handle,
    hg.IntRect(0, 0, frame.window_width, frame.window_height),
    true,
    app.render.pipeline,
    app.render.resources)
end

function runtime.update_scene_lighting(app, frame)
  update_follow_spotlight(app, frame)
end

function runtime.end_frame(app, frame)
  if frame ~= nil and not frame.skip_frame then
    hg.ImGuiEndFrame(app.render.view_ids.imgui)
  end
  hg.Frame()
  hg.UpdateWindow(app.window.handle)
end

function runtime.shutdown(app)
  if app ~= nil and app.render ~= nil and app.render.pipeline ~= nil then
    hg.DestroyForwardPipeline(app.render.pipeline)
  end

  hg.RenderShutdown()

  if app ~= nil and app.window ~= nil and app.window.handle ~= nil then
    hg.DestroyWindow(app.window.handle)
  end

  hg.WindowSystemShutdown()
  hg.InputShutdown()
end

return runtime
