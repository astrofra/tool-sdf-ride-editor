local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/add_box/add_box_world.sdfworld"
local scene_path = "../test_output/add_box/tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

cleanup()

local base_world_document = {
  name = "add_box_smoke_world",
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
  assert(app.sdf_world.selection.active_box_index == nil, "expected no initial box selection")

  local add_ok, add_result = sdf_scene.add_box(app)
  assert(add_ok, add_result)
  assert(add_result.box_name == "box", string.format("expected added box name box, got %s", tostring(add_result.box_name)))
  assert(add_result.box_index == 2, string.format("expected added box index 2, got %s", tostring(add_result.box_index)))
  assert(add_result.op == sdf.CsgOpAdd, string.format("expected added box op add, got %s", tostring(add_result.op)))
  assert(approx_eq(add_result.translation.x, 0.0), string.format("expected added tx 0, got %.4f", add_result.translation.x))
  assert(approx_eq(add_result.translation.y, 5.0), string.format("expected added ty 5, got %.4f", add_result.translation.y))
  assert(approx_eq(add_result.translation.z, 0.0), string.format("expected added tz 0, got %.4f", add_result.translation.z))
  assert(approx_eq(add_result.half_size.x, 5.0), string.format("expected added hx 5, got %.4f", add_result.half_size.x))
  assert(approx_eq(add_result.half_size.y, 5.0), string.format("expected added hy 5, got %.4f", add_result.half_size.y))
  assert(approx_eq(add_result.half_size.z, 5.0), string.format("expected added hz 5, got %.4f", add_result.half_size.z))

  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell.box_count == 2, string.format("expected active cell box count 2 after add, got %d", active_cell.box_count))
  assert(#active_cell.scene_file.scene.boxes == 2, string.format("expected scene file box vector count 2 after add, got %d", #active_cell.scene_file.scene.boxes))
  assert(app.sdf_world.total_box_count == 2, string.format("expected world total box count 2 after add, got %d", app.sdf_world.total_box_count))
  assert(app.sdf_world.selection.active_box_index == 2, string.format("expected selection to move to box 2, got %s", tostring(app.sdf_world.selection.active_box_index)))
  assert(#active_cell.preview_nodes.flat == 2, string.format("expected 2 flat preview nodes after add, got %d", #active_cell.preview_nodes.flat))
  assert(#active_cell.preview_nodes.wireframe == 24, string.format("expected 24 wireframe edges after add, got %d", #active_cell.preview_nodes.wireframe))

  local added_box = active_cell.scene_file.scene.boxes[2]
  assert(added_box.name == "box", string.format("expected added box name box in memory, got %s", tostring(added_box.name)))
  assert(added_box.op == sdf.CsgOpAdd, string.format("expected added box op add in memory, got %s", tostring(added_box.op)))
  assert(approx_eq(added_box.transform.translation.x, add_result.translation.x), string.format("expected added box tx %.4f, got %.4f", add_result.translation.x, added_box.transform.translation.x))
  assert(approx_eq(added_box.transform.translation.y, add_result.translation.y), string.format("expected added box ty %.4f, got %.4f", add_result.translation.y, added_box.transform.translation.y))
  assert(approx_eq(added_box.transform.translation.z, add_result.translation.z), string.format("expected added box tz %.4f, got %.4f", add_result.translation.z, added_box.transform.translation.z))
  assert(approx_eq(added_box.half_size.x, add_result.half_size.x), string.format("expected added box hx %.4f, got %.4f", add_result.half_size.x, added_box.half_size.x))
  assert(approx_eq(added_box.half_size.y, add_result.half_size.y), string.format("expected added box hy %.4f, got %.4f", add_result.half_size.y, added_box.half_size.y))
  assert(approx_eq(added_box.half_size.z, add_result.half_size.z), string.format("expected added box hz %.4f, got %.4f", add_result.half_size.z, added_box.half_size.z))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  assert(#reloaded_scene_file.scene.boxes == 2, string.format("expected reloaded scene to contain 2 boxes, got %d", #reloaded_scene_file.scene.boxes))

  local reloaded_box = reloaded_scene_file.scene.boxes[2]
  assert(reloaded_box.name == "box", string.format("expected reloaded added box name box, got %s", tostring(reloaded_box.name)))
  assert(reloaded_box.op == sdf.CsgOpAdd, string.format("expected reloaded added box op add, got %s", tostring(reloaded_box.op)))
  assert(approx_eq(reloaded_box.transform.translation.x, add_result.translation.x), string.format("expected reloaded tx %.4f, got %.4f", add_result.translation.x, reloaded_box.transform.translation.x))
  assert(approx_eq(reloaded_box.transform.translation.y, add_result.translation.y), string.format("expected reloaded ty %.4f, got %.4f", add_result.translation.y, reloaded_box.transform.translation.y))
  assert(approx_eq(reloaded_box.transform.translation.z, add_result.translation.z), string.format("expected reloaded tz %.4f, got %.4f", add_result.translation.z, reloaded_box.transform.translation.z))
  assert(approx_eq(reloaded_box.half_size.x, add_result.half_size.x), string.format("expected reloaded hx %.4f, got %.4f", add_result.half_size.x, reloaded_box.half_size.x))
  assert(approx_eq(reloaded_box.half_size.y, add_result.half_size.y), string.format("expected reloaded hy %.4f, got %.4f", add_result.half_size.y, reloaded_box.half_size.y))
  assert(approx_eq(reloaded_box.half_size.z, add_result.half_size.z), string.format("expected reloaded hz %.4f, got %.4f", add_result.half_size.z, reloaded_box.half_size.z))

  runtime.end_frame(app, frame)
  print("sdf add box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
