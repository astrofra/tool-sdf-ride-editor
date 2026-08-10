local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")

local source_scene_path = "sdf-scenes/tile_000.sdfscene"
local world_path = "../test_output/duplicate_selected_box/duplicate_selected_box_world.sdfworld"
local scene_path = "../test_output/duplicate_selected_box/tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

cleanup()

local load_source_ok, source_scene_file, source_scene_error = sdf.load_scene_file(source_scene_path)
assert(load_source_ok, source_scene_error)
assert(#source_scene_file.scene.boxes >= 1, string.format("expected source scene %s to contain at least one box", source_scene_path))

local first_source_box = source_scene_file.scene.boxes[1]
local original_box_count = #source_scene_file.scene.boxes

local save_scene_ok, save_scene_error = sdf.save_scene_file(source_scene_file, scene_path)
assert(save_scene_ok, save_scene_error)

local base_world_document = {
  name = "duplicate_selected_box_smoke_world",
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
  assert(active_cell.box_count == original_box_count, string.format("expected copied scene to preserve %d boxes, got %d", original_box_count, active_cell.box_count))

  app.sdf_world.selection.active_box_index = 1

  local duplicate_ok, duplicate_result = sdf_scene.duplicate_selected_box(app)
  assert(duplicate_ok, duplicate_result)
  assert(duplicate_result.source_box_name == first_source_box.name, string.format("expected source box name %s, got %s", first_source_box.name, tostring(duplicate_result.source_box_name)))
  assert(duplicate_result.box_name == first_source_box.name .. "_copy", string.format("expected duplicate name %s_copy, got %s", first_source_box.name, tostring(duplicate_result.box_name)))
  assert(duplicate_result.box_index == original_box_count + 1, string.format("expected duplicate box index %d, got %s", original_box_count + 1, tostring(duplicate_result.box_index)))
  assert(duplicate_result.op == first_source_box.op, string.format("expected duplicate op %s, got %s", tostring(first_source_box.op), tostring(duplicate_result.op)))

  local expected_offset = math.max(first_source_box.half_size.x * 2.0, 1.0)
  assert(approx_eq(duplicate_result.translation.x, first_source_box.transform.translation.x + expected_offset), string.format("expected duplicate tx %.4f, got %.4f", first_source_box.transform.translation.x + expected_offset, duplicate_result.translation.x))
  assert(approx_eq(duplicate_result.translation.y, first_source_box.transform.translation.y), string.format("expected duplicate ty %.4f, got %.4f", first_source_box.transform.translation.y, duplicate_result.translation.y))
  assert(approx_eq(duplicate_result.translation.z, first_source_box.transform.translation.z), string.format("expected duplicate tz %.4f, got %.4f", first_source_box.transform.translation.z, duplicate_result.translation.z))
  assert(approx_eq(duplicate_result.half_size.x, first_source_box.half_size.x), string.format("expected duplicate hx %.4f, got %.4f", first_source_box.half_size.x, duplicate_result.half_size.x))
  assert(approx_eq(duplicate_result.half_size.y, first_source_box.half_size.y), string.format("expected duplicate hy %.4f, got %.4f", first_source_box.half_size.y, duplicate_result.half_size.y))
  assert(approx_eq(duplicate_result.half_size.z, first_source_box.half_size.z), string.format("expected duplicate hz %.4f, got %.4f", first_source_box.half_size.z, duplicate_result.half_size.z))

  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell.box_count == original_box_count + 1, string.format("expected box count %d after duplicate, got %d", original_box_count + 1, active_cell.box_count))
  assert(#active_cell.scene_file.scene.boxes == original_box_count + 1, string.format("expected scene box vector count %d after duplicate, got %d", original_box_count + 1, #active_cell.scene_file.scene.boxes))
  assert(app.sdf_world.total_box_count == original_box_count + 1, string.format("expected world total box count %d after duplicate, got %d", original_box_count + 1, app.sdf_world.total_box_count))
  assert(app.sdf_world.selection.active_box_index == original_box_count + 1, string.format("expected selection to move to duplicate at %d, got %s", original_box_count + 1, tostring(app.sdf_world.selection.active_box_index)))
  assert(#active_cell.preview_nodes.flat == original_box_count + 1, string.format("expected %d flat preview nodes after duplicate, got %d", original_box_count + 1, #active_cell.preview_nodes.flat))
  assert(#active_cell.preview_nodes.wireframe == (original_box_count + 1) * 12, string.format("expected %d wireframe edges after duplicate, got %d", (original_box_count + 1) * 12, #active_cell.preview_nodes.wireframe))

  local duplicated_box = active_cell.scene_file.scene.boxes[original_box_count + 1]
  assert(duplicated_box.name == duplicate_result.box_name, string.format("expected duplicate box name %s in memory, got %s", duplicate_result.box_name, tostring(duplicated_box.name)))
  assert(duplicated_box.op == first_source_box.op, string.format("expected duplicate op %s in memory, got %s", tostring(first_source_box.op), tostring(duplicated_box.op)))
  assert(approx_eq(duplicated_box.transform.translation.x, duplicate_result.translation.x), string.format("expected duplicate box tx %.4f, got %.4f", duplicate_result.translation.x, duplicated_box.transform.translation.x))
  assert(approx_eq(duplicated_box.transform.translation.y, duplicate_result.translation.y), string.format("expected duplicate box ty %.4f, got %.4f", duplicate_result.translation.y, duplicated_box.transform.translation.y))
  assert(approx_eq(duplicated_box.transform.translation.z, duplicate_result.translation.z), string.format("expected duplicate box tz %.4f, got %.4f", duplicate_result.translation.z, duplicated_box.transform.translation.z))
  assert(approx_eq(duplicated_box.half_size.x, duplicate_result.half_size.x), string.format("expected duplicate box hx %.4f, got %.4f", duplicate_result.half_size.x, duplicated_box.half_size.x))
  assert(approx_eq(duplicated_box.half_size.y, duplicate_result.half_size.y), string.format("expected duplicate box hy %.4f, got %.4f", duplicate_result.half_size.y, duplicated_box.half_size.y))
  assert(approx_eq(duplicated_box.half_size.z, duplicate_result.half_size.z), string.format("expected duplicate box hz %.4f, got %.4f", duplicate_result.half_size.z, duplicated_box.half_size.z))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  assert(#reloaded_scene_file.scene.boxes == original_box_count + 1, string.format("expected reloaded scene to contain %d boxes, got %d", original_box_count + 1, #reloaded_scene_file.scene.boxes))

  local reloaded_duplicate_box = reloaded_scene_file.scene.boxes[original_box_count + 1]
  assert(reloaded_duplicate_box.name == duplicate_result.box_name, string.format("expected reloaded duplicate name %s, got %s", duplicate_result.box_name, tostring(reloaded_duplicate_box.name)))
  assert(reloaded_duplicate_box.op == first_source_box.op, string.format("expected reloaded duplicate op %s, got %s", tostring(first_source_box.op), tostring(reloaded_duplicate_box.op)))
  assert(approx_eq(reloaded_duplicate_box.transform.translation.x, duplicate_result.translation.x), string.format("expected reloaded duplicate tx %.4f, got %.4f", duplicate_result.translation.x, reloaded_duplicate_box.transform.translation.x))
  assert(approx_eq(reloaded_duplicate_box.transform.translation.y, duplicate_result.translation.y), string.format("expected reloaded duplicate ty %.4f, got %.4f", duplicate_result.translation.y, reloaded_duplicate_box.transform.translation.y))
  assert(approx_eq(reloaded_duplicate_box.transform.translation.z, duplicate_result.translation.z), string.format("expected reloaded duplicate tz %.4f, got %.4f", duplicate_result.translation.z, reloaded_duplicate_box.transform.translation.z))
  assert(approx_eq(reloaded_duplicate_box.half_size.x, duplicate_result.half_size.x), string.format("expected reloaded duplicate hx %.4f, got %.4f", duplicate_result.half_size.x, reloaded_duplicate_box.half_size.x))
  assert(approx_eq(reloaded_duplicate_box.half_size.y, duplicate_result.half_size.y), string.format("expected reloaded duplicate hy %.4f, got %.4f", duplicate_result.half_size.y, reloaded_duplicate_box.half_size.y))
  assert(approx_eq(reloaded_duplicate_box.half_size.z, duplicate_result.half_size.z), string.format("expected reloaded duplicate hz %.4f, got %.4f", duplicate_result.half_size.z, reloaded_duplicate_box.half_size.z))

  runtime.end_frame(app, frame)
  print("sdf duplicate selected box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
