local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/delete_selected_box/delete_selected_box_world.sdfworld"
local scene_path = "../test_output/delete_selected_box/tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

cleanup()

local base_world_document = {
  name = "delete_selected_box_smoke_world",
  cell_size = 100.0,
  cell_bounds_padding = 10.0,
  active_cell_name = "tile_000",
  cells = {
    {
      name = "tile_000",
      scene_path = scene_path,
      world_translation = {
        x = 0.0,
        y = 0.0,
        z = 0.0
      }
    }
  }
}

local base_scene_file = sdf_cell_factory.make_default_scene_file(base_world_document, "tile_000")

local save_scene_ok, save_scene_error = sdf.save_scene_file(base_scene_file, scene_path)
assert(save_scene_ok, save_scene_error)

local save_world_ok, save_world_error = sdf_world.save_world_file(world_path, base_world_document)
assert(save_world_ok, save_world_error)

local app = runtime.create()

local ok, err = xpcall(function()
  sdf_scene.attach(app, world_path)

  local frame = runtime.begin_frame(app)
  assert(not frame.exit_requested, "frame should not request exit during smoke")
  assert(not frame.skip_frame, "frame should not be skipped during smoke")
  runtime.prepare_camera_frame(app, frame)

  local select_ok, selected_index = sdf_scene.select_box_at_viewport_position(
    app,
    frame,
    frame.window_width * 0.5,
    frame.window_height * 0.5)
  assert(select_ok, "expected selected-box delete smoke to select the default box first")
  assert(selected_index == 1, string.format("expected selected index 1 before delete, got %s", tostring(selected_index)))

  local delete_ok, deleted_box_name = sdf_scene.delete_selected_box(app)
  assert(delete_ok, deleted_box_name)
  assert(deleted_box_name == "socle", string.format("expected deleted box socle, got %s", tostring(deleted_box_name)))

  local active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell ~= nil, "expected active cell after delete")
  assert(active_cell.box_count == 0, string.format("expected active cell box count 0 after delete, got %d", active_cell.box_count))
  assert(#active_cell.scene_file.scene.boxes == 0, string.format("expected scene file box vector to be empty, got %d", #active_cell.scene_file.scene.boxes))
  assert(app.sdf_world.total_box_count == 0, string.format("expected world total box count 0 after delete, got %d", app.sdf_world.total_box_count))
  assert(app.sdf_world.selection.active_box_index == nil, "expected selection to clear after delete")
  assert(#active_cell.preview_nodes.flat == 0, string.format("expected no flat preview nodes after delete, got %d", #active_cell.preview_nodes.flat))
  assert(#active_cell.preview_nodes.wireframe == 0, string.format("expected no wireframe preview nodes after delete, got %d", #active_cell.preview_nodes.wireframe))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  assert(#reloaded_scene_file.scene.boxes == 0, string.format("expected reloaded scene file to stay empty after delete, got %d", #reloaded_scene_file.scene.boxes))

  runtime.end_frame(app, frame)
  print("sdf delete selected box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
