local runtime = require("editor.runtime")
local gizmos = require("editor.gizmos")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/box_selection/gizmo_selected_box_world.sdfworld"
local scene_path = "../test_output/box_selection/gizmo_selected_box_tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

cleanup()

local base_world_document = {
  name = "gizmo_selected_box_smoke_world",
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
  gizmos.attach(app)

  app.sdf_world.selection.active_box_index = 1

  local frame = runtime.begin_frame(app)
  assert(not frame.exit_requested, "frame should not request exit during smoke")
  assert(not frame.skip_frame, "frame should not be skipped during smoke")

  runtime.prepare_camera_frame(app, frame)
  sdf_scene.update(app, frame)
  gizmos.update(app, frame)

  local translation_gizmo = app.gizmos.translation
  local active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  local selected_box = active_cell.scene_file.scene.boxes[1]

  assert(translation_gizmo.visible, "expected translation gizmo to become visible with a selected box")
  assert(translation_gizmo.axis_length >= 4.0 and translation_gizmo.axis_length <= 18.0, string.format("expected axis length in clamp range, got %.4f", translation_gizmo.axis_length))
  assert(approx_eq(translation_gizmo.pivot.x, selected_box.transform.translation.x), string.format("expected gizmo pivot.x %.4f, got %.4f", selected_box.transform.translation.x, translation_gizmo.pivot.x))
  assert(approx_eq(translation_gizmo.pivot.y, selected_box.transform.translation.y), string.format("expected gizmo pivot.y %.4f, got %.4f", selected_box.transform.translation.y, translation_gizmo.pivot.y))
  assert(approx_eq(translation_gizmo.pivot.z, selected_box.transform.translation.z), string.format("expected gizmo pivot.z %.4f, got %.4f", selected_box.transform.translation.z, translation_gizmo.pivot.z))

  runtime.end_frame(app, frame)
  print("gizmo selected box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
