local hg = require("harfang")

local camera_transport = {}

local camera_translation_speed = 16.0
local camera_velocity_response = 7.0
local slider_return_speed = 8.0

local function smooth_towards(current, target, speed, dt)
  local blend = math.min(speed * dt, 1.0)
  return current + (target - current) * blend
end

function camera_transport.attach(app)
  app.camera_transport = {
    drive = 0.0,
    z_velocity = 0.0,
    position = app.scene.camera_position,
    translation_speed = camera_translation_speed,
    velocity_response = camera_velocity_response,
    slider_return_speed = slider_return_speed
  }
end

function camera_transport.update(app, frame)
  local state = app.camera_transport

  hg.ImGuiSetNextWindowPos(hg.Vec2(24, frame.window_height - 104))
  hg.ImGuiSetNextWindowSize(hg.Vec2(360, 0))

  local slider_is_active = false
  if hg.ImGuiBegin(
    "Camera Transport",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    hg.ImGuiTextWrapped("Push left to move backward on world Z, right to move forward. Releasing recenters the control.")
    _, state.drive = hg.ImGuiSliderFloat("Z Drive", state.drive, -1.0, 1.0, "%.2f")
    slider_is_active = hg.ImGuiIsItemActive()
  end
  hg.ImGuiEnd()

  if not slider_is_active then
    state.drive = smooth_towards(state.drive, 0.0, state.slider_return_speed, frame.dt)
    if math.abs(state.drive) < 0.001 then
      state.drive = 0.0
    end
  end

  local target_z_velocity = state.drive * state.translation_speed
  state.z_velocity = smooth_towards(state.z_velocity, target_z_velocity, state.velocity_response, frame.dt)
  if math.abs(state.z_velocity) < 0.001 and math.abs(state.drive) < 0.001 then
    state.z_velocity = 0.0
  end

  state.position.z = state.position.z + state.z_velocity * frame.dt
  app.scene.camera_transform:SetPos(state.position)
end

return camera_transport
