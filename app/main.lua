local runtime = require("editor.runtime")
local camera_transport = require("editor.camera_transport")
local gizmos = require("editor.gizmos")
local sdf_scene = require("editor.sdf_scene")

local function run_app(app)
  while runtime.is_running(app) do
    local frame = runtime.begin_frame(app)
    if frame.exit_requested then
      break
    end

    if not frame.skip_frame then
      camera_transport.update(app, frame)
      runtime.prepare_camera_frame(app, frame)
      sdf_scene.update(app, frame)
      gizmos.update(app, frame)

      runtime.render_scene(app, frame)
      gizmos.draw(app, frame)
    end

    runtime.end_frame(app, frame)
  end
end

local app = runtime.create()
sdf_scene.attach(app)
camera_transport.attach(app)
gizmos.attach(app)

local ok, err = xpcall(function()
  run_app(app)
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
