local runtime = require("editor.runtime")
local sdf_scene = require("editor.sdf_scene")

local app = runtime.create()

local ok, err = xpcall(function()
  sdf_scene.attach(app)

  assert(app.sdf_world ~= nil, "app.sdf_world should be initialized")
  assert(app.sdf_world.world_document ~= nil, app.sdf_world.load_error or "world document should be loaded")
  assert(math.abs(app.sdf_world.world_document.cell_bounds_padding - 10.0) < 0.0001, "world should expose 10m bounds padding")
  assert(math.abs(app.sdf_world.world_document.effective_cell_span - 120.0) < 0.0001, "world should expose 120m effective span")
  assert(app.sdf_world.cell_placement ~= nil, "world should expose cell placement state")
  assert(app.sdf_world.cell_placement.active == false, "cell placement should be idle by default")
  assert(#app.sdf_world.cells > 0, "world should contain at least one cell")
  assert(app.sdf_world.active_cell_index ~= nil, "world should define an active cell")

  local active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell ~= nil, "active cell should exist")
  assert(active_cell.scene_file ~= nil, active_cell.load_error or "active cell scene file should be loaded")
  assert(active_cell.bounds_span ~= nil, "active cell should expose bounds span diagnostics")
  assert(#active_cell.preview_nodes.flat == active_cell.box_count, "flat preview node count should match active cell box count")
  assert(#active_cell.preview_nodes.wireframe == active_cell.box_count * 12, "wireframe preview node count should expose 12 edges per box")

  print(string.format(
    "sdf scene smoke test passed (%s, %d cells, active=%s)",
    app.sdf_world.path,
    #app.sdf_world.cells,
    active_cell.name))
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
