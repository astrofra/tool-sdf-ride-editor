local hg = require("harfang")
local runtime = require("editor.runtime")
local sdf_scene = require("editor.sdf_scene")
local gizmos = require("editor.gizmos")
local camera_transport = require("editor.camera_transport")

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
  runtime.render_scene(app, frame)
  runtime.end_frame(app, frame)

  print("editor placement full-frame smoke test passed")
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
