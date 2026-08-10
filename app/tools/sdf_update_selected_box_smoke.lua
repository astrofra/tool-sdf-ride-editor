local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")

local source_scene_path = "sdf-scenes/tile_000.sdfscene"
local world_path = "../test_output/update_selected_box/update_selected_box_world.sdfworld"
local scene_path = "../test_output/update_selected_box/tile_000.sdfscene"

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
assert(#source_scene_file.scene.boxes >= 2, string.format("expected source scene %s to contain at least two boxes", source_scene_path))

local first_source_box = source_scene_file.scene.boxes[1]
local second_source_box = source_scene_file.scene.boxes[2]
local original_box_count = #source_scene_file.scene.boxes

local save_scene_ok, save_scene_error = sdf.save_scene_file(source_scene_file, scene_path)
assert(save_scene_ok, save_scene_error)

local base_world_document = {
  name = "update_selected_box_smoke_world",
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

local hg = require("harfang")
local runtime = require("editor.runtime")
local sdf_scene = require("editor.sdf_scene")

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

  local duplicate_ok, duplicate_error = sdf_scene.update_selected_box(app, {
    name = second_source_box.name,
    op = first_source_box.op,
    translation = hg.Vec3(
      first_source_box.transform.translation.x,
      first_source_box.transform.translation.y,
      first_source_box.transform.translation.z),
    half_size = hg.Vec3(
      first_source_box.half_size.x,
      first_source_box.half_size.y,
      first_source_box.half_size.z)
  })
  assert(not duplicate_ok, "expected duplicate box name validation to fail")
  assert(string.find(duplicate_error, "already exists", 1, true) ~= nil, string.format("expected duplicate-name error, got %s", tostring(duplicate_error)))

  local unchanged_box = active_cell.scene_file.scene.boxes[1]
  assert(unchanged_box.name == first_source_box.name, string.format("expected first box name to stay %s after duplicate-name validation, got %s", first_source_box.name, tostring(unchanged_box.name)))
  assert(unchanged_box.op == first_source_box.op, string.format("expected first box op to stay %s after duplicate-name validation, got %s", tostring(first_source_box.op), tostring(unchanged_box.op)))

  local updated_name = first_source_box.name .. "_edited_smoke"
  local updated_translation = hg.Vec3(
    first_source_box.transform.translation.x + 1.0,
    first_source_box.transform.translation.y + 2.0,
    first_source_box.transform.translation.z - 3.0)
  local updated_half_size = hg.Vec3(
    math.max(1.0, first_source_box.half_size.x),
    0.0,
    -1.0)

  local update_ok, update_result = sdf_scene.update_selected_box(app, {
    name = updated_name,
    op = sdf.CsgOpSubtract,
    translation = updated_translation,
    half_size = updated_half_size
  })
  assert(update_ok, update_result)
  assert(update_result.box_name == updated_name, string.format("expected updated box name %s, got %s", updated_name, tostring(update_result.box_name)))
  assert(update_result.op == sdf.CsgOpSubtract, string.format("expected updated op subtract, got %s", tostring(update_result.op)))
  assert(update_result.half_size_was_clamped, "expected half-size clamp to be reported")
  assert(update_result.half_size.y > 0.0 and update_result.half_size.z > 0.0, "expected clamped half-size components to stay strictly positive")

  active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  local updated_box = active_cell.scene_file.scene.boxes[1]
  assert(updated_box.name == updated_name, string.format("expected updated box name %s, got %s", updated_name, tostring(updated_box.name)))
  assert(updated_box.op == sdf.CsgOpSubtract, string.format("expected updated op subtract, got %s", tostring(updated_box.op)))
  assert(approx_eq(updated_box.transform.translation.x, updated_translation.x), string.format("expected updated tx %.4f, got %.4f", updated_translation.x, updated_box.transform.translation.x))
  assert(approx_eq(updated_box.transform.translation.y, updated_translation.y), string.format("expected updated ty %.4f, got %.4f", updated_translation.y, updated_box.transform.translation.y))
  assert(approx_eq(updated_box.transform.translation.z, updated_translation.z), string.format("expected updated tz %.4f, got %.4f", updated_translation.z, updated_box.transform.translation.z))
  assert(approx_eq(updated_box.half_size.x, update_result.half_size.x), string.format("expected updated hx %.4f, got %.4f", update_result.half_size.x, updated_box.half_size.x))
  assert(approx_eq(updated_box.half_size.y, update_result.half_size.y), string.format("expected clamped hy %.4f, got %.4f", update_result.half_size.y, updated_box.half_size.y))
  assert(approx_eq(updated_box.half_size.z, update_result.half_size.z), string.format("expected clamped hz %.4f, got %.4f", update_result.half_size.z, updated_box.half_size.z))
  assert(app.sdf_world.selection.active_box_index == 1, string.format("expected selection to remain on box 1, got %s", tostring(app.sdf_world.selection.active_box_index)))
  assert(#active_cell.preview_nodes.flat == original_box_count, string.format("expected %d flat preview nodes after update, got %d", original_box_count, #active_cell.preview_nodes.flat))
  assert(#active_cell.preview_nodes.wireframe == original_box_count * 12, string.format("expected %d wireframe edges after update, got %d", original_box_count * 12, #active_cell.preview_nodes.wireframe))

  local load_ok, reloaded_scene_file, load_error = sdf.load_scene_file(scene_path)
  assert(load_ok, load_error)
  local reloaded_box = reloaded_scene_file.scene.boxes[1]
  assert(reloaded_box.name == updated_name, string.format("expected reloaded box name %s, got %s", updated_name, tostring(reloaded_box.name)))
  assert(reloaded_box.op == sdf.CsgOpSubtract, string.format("expected reloaded op subtract, got %s", tostring(reloaded_box.op)))
  assert(approx_eq(reloaded_box.transform.translation.x, updated_translation.x), string.format("expected reloaded tx %.4f, got %.4f", updated_translation.x, reloaded_box.transform.translation.x))
  assert(approx_eq(reloaded_box.transform.translation.y, updated_translation.y), string.format("expected reloaded ty %.4f, got %.4f", updated_translation.y, reloaded_box.transform.translation.y))
  assert(approx_eq(reloaded_box.transform.translation.z, updated_translation.z), string.format("expected reloaded tz %.4f, got %.4f", updated_translation.z, reloaded_box.transform.translation.z))
  assert(approx_eq(reloaded_box.half_size.x, update_result.half_size.x), string.format("expected reloaded hx %.4f, got %.4f", update_result.half_size.x, reloaded_box.half_size.x))
  assert(approx_eq(reloaded_box.half_size.y, update_result.half_size.y), string.format("expected reloaded hy %.4f, got %.4f", update_result.half_size.y, reloaded_box.half_size.y))
  assert(approx_eq(reloaded_box.half_size.z, update_result.half_size.z), string.format("expected reloaded hz %.4f, got %.4f", update_result.half_size.z, reloaded_box.half_size.z))

  runtime.end_frame(app, frame)
  print("sdf update selected box smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
