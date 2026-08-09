local runtime = require("editor.runtime")
local sdf_scene = require("editor.sdf_scene")

local app = runtime.create()

local ok, err = xpcall(function()
  sdf_scene.attach(app)

  assert(app.sdf ~= nil, "app.sdf should be initialized")
  assert(app.sdf.scene_file ~= nil, app.sdf.load_error or "scene file should be loaded")
  assert(app.sdf.box_count > 0, "scene should contain at least one box")
  assert(#app.sdf.preview_nodes == app.sdf.box_count, "preview node count should match box count")

  print(string.format(
    "sdf scene smoke test passed (%s, %d boxes)",
    app.sdf.path,
    app.sdf.box_count))
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
