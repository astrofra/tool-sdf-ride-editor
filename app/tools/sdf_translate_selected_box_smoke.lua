local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/translate_selected_box/translate_selected_box_world.sdfworld"
local scene_path = "../test_output/translate_selected_box/tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

cleanup()

local base_world_document = {
  name = "translate_selected_box_smoke_world",
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

  app.sdf_world.selection.active_box_index = 1

  local original_box = active_cell.scene_file.scene.boxes[1]
  local original_translation = {
    x = original_box.transform.translation.x,
    y = original_box.transform.translation.y,
    z = original_box.transform.translation.z
  }

  app.sdf_world.box_translation_step = 2.5

  local nudge_x_ok, nudge_x_result = sdf_scene.nudge_selected_box_translation(app, "x", 1.0)
  assert(nudge_x_ok, nudge_x_result)
  assert(approx_eq(nudge_x_result.translation.x, original_translation.x + 2.5), string.format("expected x nudge result %.4f, got %.4f", original_translation.x + 2.5, nudge_x_result.translation.x))
  assert(approx_eq(nudge_x_result.translation.y, original_translation.y), string.format("expected y unchanged %.4f, got %.4f", original_translation.y, nudge_x_result.translation.y))
  assert(approx_eq(nudge_x_result.translation.z, original_translation.z), string.format("expected z unchanged %.4f, got %.4f", original_translation.z, nudge_x_result.translation.z))

  local nudge_z_ok, nudge_z_result = sdf_scene.nudge_selected_box_translation(app, "z", -1.0)
  assert(nudge_z_ok, nudge_z_result)
  assert(approx_eq(nudge_z_result.translation.x, original_translation.x + 2.5), string.format("expected x preserved after z nudge %.4f, got %.4f", original_translation.x + 2.5, nudge_z_result.translation.x))
  assert(approx_eq(nudge_z_result.translation.y, original_translation.y), string.format("expected y preserved after z nudge %.4f, got %.4f", original_translation.y, nudge_z_result.translation.y))
  assert(approx_eq(nudge_z_result.translation.z, original_translation.z - 2.5), string.format("expected z nudge result %.4f, got %.4f", original_translation.z - 2.5, nudge_z_result.translation.z))

  app.sdf_world.box_translation_step = 1.0

  local snap_ok, snap_result = sdf_scene.snap_selected_box_translation(app)
  assert(snap_ok, snap_result)
  assert(approx_eq(snap_result.translation.x, 3.0), string.format("expected snapped x 3.0, got %.4f", snap_result.translation.x))
  assert(approx_eq(snap_result.translation.y, -2.0), string.format("expected snapped y -2.0, got %.4f", snap_result.translation.y))
  assert(approx_eq(snap_result.translation.z, -2.0), string.format("expected snapped z -2.0, got %.4f", snap_result.translation.z))

  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  local translated_box = active_cell.scene_file.scene.boxes[1]
  assert(approx_eq(translated_box.transform.translation.x, snap_result.translation.x), string.format("expected in-memory snapped x %.4f, got %.4f", snap_result.translation.x, translated_box.transform.translation.x))
  assert(approx_eq(translated_box.transform.translation.y, snap_result.translation.y), string.format("expected in-memory snapped y %.4f, got %.4f", snap_result.translation.y, translated_box.transform.translation.y))
  assert(approx_eq(translated_box.transform.translation.z, snap_result.translation.z), string.format("expected in-memory snapped z %.4f, got %.4f", snap_result.translation.z, translated_box.transform.translation.z))
  assert(app.sdf_world.selection.active_box_index == 1, string.format("expected selection to remain on box 1, got %s", tostring(app.sdf_world.selection.active_box_index)))
  assert(#active_cell.preview_nodes.flat == 1, string.format("expected 1 flat preview node after translation workflow, got %d", #active_cell.preview_nodes.flat))
  assert(#active_cell.preview_nodes.wireframe == 12, string.format("expected 12 wireframe edges after translation workflow, got %d", #active_cell.preview_nodes.wireframe))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  local reloaded_box = reloaded_scene_file.scene.boxes[1]
  assert(approx_eq(reloaded_box.transform.translation.x, snap_result.translation.x), string.format("expected reloaded snapped x %.4f, got %.4f", snap_result.translation.x, reloaded_box.transform.translation.x))
  assert(approx_eq(reloaded_box.transform.translation.y, snap_result.translation.y), string.format("expected reloaded snapped y %.4f, got %.4f", snap_result.translation.y, reloaded_box.transform.translation.y))
  assert(approx_eq(reloaded_box.transform.translation.z, snap_result.translation.z), string.format("expected reloaded snapped z %.4f, got %.4f", snap_result.translation.z, reloaded_box.transform.translation.z))

  runtime.end_frame(app, frame)
  print("sdf translate selected box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
