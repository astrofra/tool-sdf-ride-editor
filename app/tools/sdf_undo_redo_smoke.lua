local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/update_selected_box/undo_redo_world.sdfworld"
local scene_path = "../test_output/update_selected_box/undo_redo_tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

cleanup()

local base_world_document = {
  name = "undo_redo_smoke_world",
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

  local active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell ~= nil, "expected active cell")
  assert(active_cell.box_count == 1, string.format("expected initial box count 1, got %d", active_cell.box_count))

  local add_ok, add_result = sdf_scene.add_box(app)
  assert(add_ok, add_result)
  assert(active_cell.box_count == 2, string.format("expected box count 2 after add, got %d", active_cell.box_count))
  assert(app.sdf_world.selection.active_box_index == 2, string.format("expected added box selection 2, got %s", tostring(app.sdf_world.selection.active_box_index)))
  assert(#app.sdf_world.history.undo_entries == 1, string.format("expected one undo entry after add, got %d", #app.sdf_world.history.undo_entries))

  local undo_add_ok, undo_add_result = sdf_scene.undo_last_edit(app)
  assert(undo_add_ok, undo_add_result)
  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell.box_count == 1, string.format("expected box count 1 after undo add, got %d", active_cell.box_count))
  assert(app.sdf_world.selection.active_box_index == nil, string.format("expected selection cleared after undo add, got %s", tostring(app.sdf_world.selection.active_box_index)))

  local redo_add_ok, redo_add_result = sdf_scene.redo_last_edit(app)
  assert(redo_add_ok, redo_add_result)
  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell.box_count == 2, string.format("expected box count 2 after redo add, got %d", active_cell.box_count))
  assert(app.sdf_world.selection.active_box_index == 2, string.format("expected selection restored to added box after redo, got %s", tostring(app.sdf_world.selection.active_box_index)))

  local added_box = active_cell.scene_file.scene.boxes[2]
  local original_translation = {
    x = added_box.transform.translation.x,
    y = added_box.transform.translation.y,
    z = added_box.transform.translation.z
  }

  app.sdf_world.box_translation_step = 1.5

  local nudge_x_ok, nudge_x_result = sdf_scene.nudge_selected_box_translation(app, "x", 1.0)
  assert(nudge_x_ok, nudge_x_result)
  local nudge_z_ok, nudge_z_result = sdf_scene.nudge_selected_box_translation(app, "z", 1.0)
  assert(nudge_z_ok, nudge_z_result)

  assert(approx_eq(nudge_z_result.translation.x, original_translation.x + 1.5), string.format("expected translated x %.4f, got %.4f", original_translation.x + 1.5, nudge_z_result.translation.x))
  assert(approx_eq(nudge_z_result.translation.y, original_translation.y), string.format("expected translated y %.4f, got %.4f", original_translation.y, nudge_z_result.translation.y))
  assert(approx_eq(nudge_z_result.translation.z, original_translation.z + 1.5), string.format("expected translated z %.4f, got %.4f", original_translation.z + 1.5, nudge_z_result.translation.z))
  assert(#app.sdf_world.history.undo_entries == 2, string.format("expected merged translation history to keep two undo entries total, got %d", #app.sdf_world.history.undo_entries))

  local undo_move_ok, undo_move_result = sdf_scene.undo_last_edit(app)
  assert(undo_move_ok, undo_move_result)
  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  added_box = active_cell.scene_file.scene.boxes[2]
  assert(approx_eq(added_box.transform.translation.x, original_translation.x), string.format("expected undo merged translation x %.4f, got %.4f", original_translation.x, added_box.transform.translation.x))
  assert(approx_eq(added_box.transform.translation.y, original_translation.y), string.format("expected undo merged translation y %.4f, got %.4f", original_translation.y, added_box.transform.translation.y))
  assert(approx_eq(added_box.transform.translation.z, original_translation.z), string.format("expected undo merged translation z %.4f, got %.4f", original_translation.z, added_box.transform.translation.z))
  assert(app.sdf_world.selection.active_box_index == 2, string.format("expected selection to remain on added box after undo move, got %s", tostring(app.sdf_world.selection.active_box_index)))

  local redo_move_ok, redo_move_result = sdf_scene.redo_last_edit(app)
  assert(redo_move_ok, redo_move_result)
  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  added_box = active_cell.scene_file.scene.boxes[2]
  assert(approx_eq(added_box.transform.translation.x, nudge_z_result.translation.x), string.format("expected redo translated x %.4f, got %.4f", nudge_z_result.translation.x, added_box.transform.translation.x))
  assert(approx_eq(added_box.transform.translation.y, nudge_z_result.translation.y), string.format("expected redo translated y %.4f, got %.4f", nudge_z_result.translation.y, added_box.transform.translation.y))
  assert(approx_eq(added_box.transform.translation.z, nudge_z_result.translation.z), string.format("expected redo translated z %.4f, got %.4f", nudge_z_result.translation.z, added_box.transform.translation.z))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  local reloaded_box = reloaded_scene_file.scene.boxes[2]
  assert(approx_eq(reloaded_box.transform.translation.x, nudge_z_result.translation.x), string.format("expected reloaded translated x %.4f, got %.4f", nudge_z_result.translation.x, reloaded_box.transform.translation.x))
  assert(approx_eq(reloaded_box.transform.translation.y, nudge_z_result.translation.y), string.format("expected reloaded translated y %.4f, got %.4f", nudge_z_result.translation.y, reloaded_box.transform.translation.y))
  assert(approx_eq(reloaded_box.transform.translation.z, nudge_z_result.translation.z), string.format("expected reloaded translated z %.4f, got %.4f", nudge_z_result.translation.z, reloaded_box.transform.translation.z))

  runtime.end_frame(app, frame)
  print("sdf undo redo smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
