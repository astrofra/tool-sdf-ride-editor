local hg = require("harfang")

local runtime = {}

local window_defaults = {
  width = 1280,
  height = 720,
  title = "SDF Ride Editor"
}

local background_clear = {24, 28, 34}
local label_view_id = 254
local imgui_view_id = 255

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
  local font = hg.LoadFontFromAssets("fonts/spacemono-regular.ttf", 40)
  local font_program = hg.LoadProgramFromAssets("core/shader/font")
  local text_uniform_values = {hg.MakeUniformSetValue("u_color", hg.Vec4(0.92, 0.92, 0.95, 1.0))}
  local text_render_state = hg.ComputeRenderState(hg.BM_Alpha, hg.DT_LessEqual, hg.FC_Disabled, false)
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
      font = font,
      font_program = font_program,
      text_uniform_values = text_uniform_values,
      text_render_state = text_render_state,
      line_ref = line_ref,
      view_ids = {
        label = label_view_id,
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
  local camera_world = app.scene.camera_transform:GetWorld()
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
  app.scene.handle:Update(frame.dt_clock)
  hg.SubmitSceneToPipeline(
    0,
    app.scene.handle,
    hg.IntRect(0, 0, frame.window_width, frame.window_height),
    true,
    app.render.pipeline,
    app.render.resources)
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
