local hg = require("harfang")
local runtime = require("editor.runtime")
local camera_transport = require("editor.camera_transport")

local app = runtime.create()

local ok, err = xpcall(function()
  camera_transport.attach(app)

  local applied_drag_delta = camera_transport.apply_pan_delta(
    app,
    app.camera_transport,
    hg.Vec3(999.0, 0.0, 0.0))
  assert(applied_drag_delta.x <= 10.0001, string.format("expected clamped pan step <= 10.0, got %.4f", applied_drag_delta.x))
  assert(app.camera_transport.pivot.x <= 10.0001, string.format("expected pivot.x <= 10.0 after step clamp, got %.4f", app.camera_transport.pivot.x))

  app.camera_transport.pivot = hg.Vec3(999.0, 0.0, 999.0)

  local frame = runtime.begin_frame(app)
  assert(not frame.exit_requested, "frame should not request exit during smoke")
  assert(not frame.skip_frame, "frame should not be skipped during smoke")

  runtime.prepare_camera_frame(app, frame)
  camera_transport.update(app, frame)
  runtime.end_frame(app, frame)

  local pivot = app.camera_transport.pivot
  local distance = math.sqrt(pivot.x * pivot.x + pivot.z * pivot.z)
  assert(distance <= 200.0001, string.format("expected clamped pivot distance <= 200.0, got %.4f", distance))

  print("camera transport clamp smoke test passed")
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
