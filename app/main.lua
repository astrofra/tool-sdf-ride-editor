local runtime = require("editor.runtime")
local camera_transport = require("editor.camera_transport")
local gizmos = require("editor.gizmos")

local function run_app(app)
  while runtime.is_running(app) do
    local frame = runtime.begin_frame(app)
    if frame.exit_requested then
      break
    end
    if frame.skip_frame then
      runtime.end_frame(app, frame)
      goto continue
    end

    camera_transport.update(app, frame)
    runtime.prepare_camera_frame(app, frame)
    gizmos.update(app, frame)

    runtime.render_scene(app, frame)
    gizmos.draw(app, frame)
    runtime.end_frame(app, frame)

    ::continue::
  end
end

local app = runtime.create()
camera_transport.attach(app)
gizmos.attach(app)

local ok, err = xpcall(function()
  run_app(app)
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
