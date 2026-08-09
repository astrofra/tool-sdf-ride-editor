local hg = require("harfang")

local camera_transport = {}

local orbit_sensitivity = 0.008
local zoom_step = 2.5
local min_distance = 6.0
local max_distance = 220.0
local min_pitch = math.rad(10.0)
local max_pitch = math.rad(85.0)

local function get_active_cell_ground_pivot(app)
  local world_state = app.sdf_world or app.sdf
  if world_state ~= nil and world_state.active_cell_index ~= nil then
    local active_cell = world_state.cells[world_state.active_cell_index]
    if active_cell ~= nil then
      return hg.Vec3(active_cell.world_translation.x, 0.0, active_cell.world_translation.z)
    end
  end

  return hg.Vec3(app.scene.origin.x, 0.0, app.scene.origin.z)
end

local function clamp_pitch(pitch)
  return hg.Clamp(pitch, min_pitch, max_pitch)
end

local function compute_orbit_state_from_camera(position, pivot)
  local offset = position - pivot
  local distance = math.max(hg.Len(offset), min_distance)
  local yaw = math.atan(offset.x, offset.z)
  local pitch = clamp_pitch(math.asin(hg.Clamp(offset.y / distance, -1.0, 1.0)))

  return yaw, pitch, distance
end

local function compute_camera_position(pivot, yaw, pitch, distance)
  local cos_pitch = math.cos(pitch)

  return hg.Vec3(
    pivot.x + math.sin(yaw) * cos_pitch * distance,
    pivot.y + math.sin(pitch) * distance,
    pivot.z + math.cos(yaw) * cos_pitch * distance)
end

local function sync_camera_transform(app, state)
  state.position = compute_camera_position(state.pivot, state.yaw, state.pitch, state.distance)
  app.scene.camera_position = state.position
  app.scene.camera_transform:SetPos(state.position)
  app.scene.camera_transform:SetRot(hg.ToEuler(hg.Mat3LookAt(hg.Normalize(state.pivot - state.position))))
end

local function center_on_pivot(app, state, pivot)
  state.pivot = hg.Vec3(pivot.x, pivot.y, pivot.z)
  sync_camera_transform(app, state)
end

local function update_mouse_deltas(state, frame)
  local mouse_x = frame.mouse:X()
  local mouse_y = frame.mouse:Y()

  local dt_x = 0
  local dt_y = 0

  if state.last_mouse_x ~= nil then
    dt_x = mouse_x - state.last_mouse_x
    dt_y = mouse_y - state.last_mouse_y
  end

  state.last_mouse_x = mouse_x
  state.last_mouse_y = mouse_y

  return dt_x, dt_y, frame.mouse:Wheel()
end

function camera_transport.attach(app)
  local pivot = get_active_cell_ground_pivot(app)
  local yaw, pitch, distance = compute_orbit_state_from_camera(app.scene.camera_position, pivot)

  app.camera_transport = {
    pivot = pivot,
    yaw = yaw,
    pitch = pitch,
    distance = distance,
    position = app.scene.camera_position,
    last_mouse_x = nil,
    last_mouse_y = nil
  }

  sync_camera_transform(app, app.camera_transport)
end

function camera_transport.update(app, frame)
  local state = app.camera_transport

  hg.ImGuiSetNextWindowPos(hg.Vec2(24, frame.window_height - 136))
  hg.ImGuiSetNextWindowSize(hg.Vec2(380, 0))

  local center_on_active_cell_requested = false
  local center_on_origin_requested = false

  if hg.ImGuiBegin(
    "Camera Transport",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    hg.ImGuiTextWrapped("Right-drag orbits around the ground pivot at the screen center. Mouse wheel zooms along the camera local Z axis.")
    hg.ImGuiText(string.format("Pivot: %.2f, %.2f, %.2f", state.pivot.x, state.pivot.y, state.pivot.z))
    hg.ImGuiText(string.format("Distance: %.2f", state.distance))

    if hg.ImGuiButton("Center On Active Cell") then
      center_on_active_cell_requested = true
    end
    hg.ImGuiSameLine()
    if hg.ImGuiButton("Center On Origin") then
      center_on_origin_requested = true
    end
  end
  hg.ImGuiEnd()

  if center_on_active_cell_requested then
    center_on_pivot(app, state, get_active_cell_ground_pivot(app))
  elseif center_on_origin_requested then
    center_on_pivot(app, state, hg.Vec3(app.scene.origin.x, 0.0, app.scene.origin.z))
  end

  local dt_x, dt_y, dt_wheel = update_mouse_deltas(state, frame)
  local mouse_captured_by_imgui = hg.ImGuiWantCaptureMouse()

  if not mouse_captured_by_imgui then
    if frame.mouse:Button(hg.MB_1) then
      state.yaw = state.yaw - dt_x * orbit_sensitivity
      state.pitch = clamp_pitch(state.pitch - dt_y * orbit_sensitivity)
    end

    if dt_wheel ~= 0 then
      state.distance = hg.Clamp(state.distance - dt_wheel * zoom_step, min_distance, max_distance)
    end
  end

  sync_camera_transform(app, state)
end

return camera_transport
