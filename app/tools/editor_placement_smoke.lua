local hg = require("harfang")
local runtime = require("editor.runtime")
local sdf_scene = require("editor.sdf_scene")
local gizmos = require("editor.gizmos")
local camera_transport = require("editor.camera_transport")
local ground_plane = require("editor.ground_plane")
local log_panel = require("editor.log_panel")

local app = runtime.create()

local ok, err = xpcall(function()
  sdf_scene.attach(app)
  camera_transport.attach(app)
  gizmos.attach(app)

  assert(app.sdf_world ~= nil, "app.sdf_world should be initialized")
  app.sdf_world.cell_placement.active = true
  app.sdf_world.cell_placement.valid = true
  app.sdf_world.cell_placement.snapped_world_position = hg.Vec3(0.0, 0.0, 0.0)

  local frame = runtime.begin_frame(app)
  assert(not frame.exit_requested, "frame should not request exit during smoke")
  assert(not frame.skip_frame, "frame should not be skipped during smoke")

  runtime.prepare_camera_frame(app, frame)
  camera_transport.update(app, frame)
  runtime.prepare_camera_frame(app, frame)
  sdf_scene.update(app, frame)
  gizmos.update(app, frame)
  log_panel.update(app, frame)
  runtime.update_scene_lighting(app, frame)

  assert(app.scene.follow_spotlight ~= nil, "follow spotlight should be initialized")
  local spotlight_position = app.scene.follow_spotlight.transform:GetPos()
  local spotlight_target = app.scene.follow_spotlight.target
  local hit_ok, hit_position = ground_plane.screen_to_ground(frame, frame.window_width * 0.5, frame.window_height * 0.5, 0.0)
  if hit_ok then
    assert(math.abs(spotlight_target.x - hit_position.x) < 0.0001, string.format("expected spotlight target x %.4f, got %.4f", hit_position.x, spotlight_target.x))
    assert(math.abs(spotlight_target.z - hit_position.z) < 0.0001, string.format("expected spotlight target z %.4f, got %.4f", hit_position.z, spotlight_target.z))
  else
    assert(math.abs(spotlight_target.x - app.scene.origin.x) < 0.0001, string.format("expected fallback spotlight target x %.4f, got %.4f", app.scene.origin.x, spotlight_target.x))
    assert(math.abs(spotlight_target.z - app.scene.origin.z) < 0.0001, string.format("expected fallback spotlight target z %.4f, got %.4f", app.scene.origin.z, spotlight_target.z))
  end

  assert(math.abs(spotlight_position.x - spotlight_target.x) < 0.0001, string.format("expected spotlight x %.4f, got %.4f", spotlight_target.x, spotlight_position.x))
  assert(math.abs(spotlight_position.y - 200.0) < 0.0001, string.format("expected spotlight y 200.0, got %.4f", spotlight_position.y))
  assert(math.abs(spotlight_position.z - spotlight_target.z) < 0.0001, string.format("expected spotlight z %.4f, got %.4f", spotlight_target.z, spotlight_position.z))

  runtime.render_scene(app, frame)
  runtime.end_frame(app, frame)

  print("editor placement full-frame smoke test passed")
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
