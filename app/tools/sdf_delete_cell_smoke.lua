local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/delete_cell_world.sdfworld"
local first_scene_path = "../test_output/tile_000.sdfscene"
local second_scene_path = "../test_output/tile_001.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(first_scene_path)
  os.remove(second_scene_path)
end

local function file_exists(path)
  local handle = io.open(path, "rb")
  if handle == nil then
    return false
  end

  handle:close()
  return true
end

cleanup()

local base_world_document = {
  name = "delete_cell_smoke_world",
  cell_size = 100.0,
  cell_bounds_padding = 10.0,
  active_cell_name = "tile_001",
  cells = {
    {
      name = "tile_000",
      scene_path = first_scene_path,
      world_translation = {
        x = 0.0,
        y = 0.0,
        z = 0.0
      }
    },
    {
      name = "tile_001",
      scene_path = second_scene_path,
      world_translation = {
        x = 100.0,
        y = 0.0,
        z = 0.0
      }
    }
  }
}

local first_scene_file = sdf_cell_factory.make_default_scene_file(base_world_document, "tile_000")
local second_scene_file = sdf_cell_factory.make_default_scene_file(base_world_document, "tile_001")

local save_first_scene_ok, save_first_scene_error = sdf.save_scene_file(first_scene_file, first_scene_path)
assert(save_first_scene_ok, save_first_scene_error)

local save_second_scene_ok, save_second_scene_error = sdf.save_scene_file(second_scene_file, second_scene_path)
assert(save_second_scene_ok, save_second_scene_error)

local save_world_ok, save_world_error = sdf_world.save_world_file(world_path, base_world_document)
assert(save_world_ok, save_world_error)

local app = runtime.create()

local ok, err = xpcall(function()
  sdf_scene.attach(app, world_path)

  assert(#app.sdf_world.cells == 2, string.format("expected 2 cells before delete, got %d", #app.sdf_world.cells))
  assert(app.sdf_world.active_cell_index == 2, string.format("expected active cell index 2 before delete, got %s", tostring(app.sdf_world.active_cell_index)))
  assert(file_exists(second_scene_path), "expected second scene file to exist before delete")

  local delete_ok, deleted_cell_name = sdf_scene.delete_active_cell(app)
  assert(delete_ok, deleted_cell_name)
  assert(deleted_cell_name == "tile_001", string.format("expected tile_001 to be deleted, got %s", tostring(deleted_cell_name)))
  assert(#app.sdf_world.cells == 1, string.format("expected 1 cell after delete, got %d", #app.sdf_world.cells))
  assert(app.sdf_world.active_cell_index == 1, string.format("expected active cell index 1 after delete, got %s", tostring(app.sdf_world.active_cell_index)))
  assert(app.sdf_world.world_document.active_cell_name == "tile_000", string.format("expected tile_000 active after delete, got %s", tostring(app.sdf_world.world_document.active_cell_name)))
  assert(app.sdf_world.cells[1].name == "tile_000", string.format("expected remaining cell tile_000, got %s", tostring(app.sdf_world.cells[1].name)))
  assert(app.sdf_world.total_box_count == app.sdf_world.cells[1].box_count, "expected total box count to match remaining cell")
  assert(not file_exists(second_scene_path), "expected deleted scene file to be removed from disk")

  local load_ok, loaded_world_document, load_error = sdf_world.load_world_file(world_path)
  assert(load_ok, load_error)
  assert(#loaded_world_document.cells == 1, string.format("expected persisted world to contain 1 cell, got %d", #loaded_world_document.cells))
  assert(loaded_world_document.active_cell_name == "tile_000", string.format("expected persisted active cell tile_000, got %s", tostring(loaded_world_document.active_cell_name)))

  local second_delete_ok, second_delete_error = sdf_scene.delete_active_cell(app)
  assert(not second_delete_ok, "expected deleting the last remaining cell to fail")
  assert(second_delete_error == "Cannot delete the last remaining cell", string.format("unexpected last-cell delete error: %s", tostring(second_delete_error)))

  print("sdf delete cell smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
