local hg = require("harfang")
local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local world_path = "../test_output/add_cell/add_cell_world.sdfworld"
local scene_path = "../test_output/add_cell/tile_000.sdfscene"
local added_scene_path = "../test_output/add_cell/tile_001.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
  os.remove(added_scene_path)
end

cleanup()

local base_world_document = {
  name = "add_cell_smoke_world",
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

  app.sdf_world.cell_placement.active = true
  app.sdf_world.cell_placement.valid = true
  app.sdf_world.cell_placement.snapped_world_position = hg.Vec3(100.0, 0.0, 0.0)
  frame.mouse = {
    Button = function(_, button)
      return button == hg.MB_0
    end
  }

  local add_ok = sdf_scene.handle_cell_placement_confirmation(app, frame)
  assert(add_ok, "expected placement confirmation to add a cell")
  runtime.end_frame(app, frame)

  local new_cell_name = app.sdf_world.cells[2].name
  assert(new_cell_name == "tile_001", string.format("expected tile_001, got %s", tostring(new_cell_name)))
  assert(#app.sdf_world.cells == 2, string.format("expected 2 cells, got %d", #app.sdf_world.cells))
  assert(app.sdf_world.active_cell_index == 2, string.format("expected active cell index 2, got %s", tostring(app.sdf_world.active_cell_index)))

  local added_cell = app.sdf_world.cells[2]
  assert(added_cell ~= nil, "expected added cell state")
  assert(added_cell.name == "tile_001", string.format("expected added cell tile_001, got %s", tostring(added_cell.name)))
  assert(added_cell.world_translation.x == 100.0 and added_cell.world_translation.z == 0.0, "expected added cell translation to match placement")
  assert(added_cell.scene_file ~= nil, added_cell.load_error or "expected added cell scene file")
  assert(#added_cell.scene_file.scene.boxes == 1, string.format("expected added cell to contain 1 default box, got %d", #added_cell.scene_file.scene.boxes))

  local added_base_box = added_cell.scene_file.scene.boxes:at(0)
  assert(added_base_box.name == "socle", string.format("expected added default box name socle, got %s", tostring(added_base_box.name)))

  local load_ok, loaded_world_document, load_error = sdf_world.load_world_file(world_path)
  assert(load_ok, load_error)
  assert(#loaded_world_document.cells == 2, string.format("expected persisted world to contain 2 cells, got %d", #loaded_world_document.cells))
  assert(loaded_world_document.active_cell_name == "tile_001", string.format("expected persisted active cell tile_001, got %s", tostring(loaded_world_document.active_cell_name)))

  print("sdf add cell smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
