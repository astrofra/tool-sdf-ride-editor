local runtime = require("editor.runtime")
local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_scene = require("editor.sdf_scene")
local sdf_cell_factory = require("editor.sdf_cell_factory")
local sdf_selection = require("editor.sdf_selection")

local world_path = "../test_output/box_selection/box_selection_world.sdfworld"
local scene_path = "../test_output/box_selection/tile_000.sdfscene"

local function cleanup()
  os.remove(world_path)
  os.remove(scene_path)
end

local function set_vec3(vec3, x, y, z)
  vec3.x = x
  vec3.y = y
  vec3.z = z
end

local function find_empty_pick_position(active_cell, frame)
  local candidate_positions = {
    {x = 0.0, y = 0.0},
    {x = frame.window_width, y = 0.0},
    {x = 0.0, y = frame.window_height},
    {x = frame.window_width, y = frame.window_height},
    {x = -frame.window_width, y = -frame.window_height},
    {x = frame.window_width * 2.0, y = -frame.window_height},
    {x = -frame.window_width, y = frame.window_height * 2.0},
    {x = frame.window_width * 2.0, y = frame.window_height * 2.0}
  }

  for index = 1, #candidate_positions do
    local candidate = candidate_positions[index]
    local pick_index = sdf_selection.pick_box_index(active_cell, frame, candidate.x, candidate.y)
    if pick_index == nil then
      return candidate
    end
  end

  return nil
end

cleanup()

local base_world_document = {
  name = "box_selection_smoke_world",
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
local center_box = sdf.SdfBox()
center_box.name = "center_box"
set_vec3(center_box.transform.translation, 0.0, 5.0, 0.0)
set_vec3(center_box.half_size, 5.0, 5.0, 5.0)
base_scene_file.scene.boxes = sdf.SdfBoxList({center_box})

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

  local active_cell = app.sdf_world.cells[app.sdf_world.active_cell_index]
  assert(active_cell ~= nil, "expected active cell")

  local pick_index, pick_distance = sdf_selection.pick_box_index(
    active_cell,
    frame,
    frame.window_width * 0.5,
    frame.window_height * 0.5)
  assert(pick_index == 1, string.format("expected center pick to hit box 1, got %s", tostring(pick_index)))
  assert(pick_distance ~= nil and pick_distance >= 0.0, string.format("expected non-negative pick distance, got %s", tostring(pick_distance)))

  local select_ok, selected_index = sdf_scene.select_box_at_viewport_position(
    app,
    frame,
    frame.window_width * 0.5,
    frame.window_height * 0.5)
  assert(select_ok, "expected center selection to succeed")
  assert(selected_index == 1, string.format("expected selected box index 1, got %s", tostring(selected_index)))
  assert(app.sdf_world.selection.active_box_index == 1, string.format("expected world selection index 1, got %s", tostring(app.sdf_world.selection.active_box_index)))

  local empty_pick_position = find_empty_pick_position(active_cell, frame)
  assert(empty_pick_position ~= nil, "expected at least one tested screen position to miss all boxes")

  local clear_ok, cleared_index = sdf_scene.select_box_at_viewport_position(
    app,
    frame,
    empty_pick_position.x,
    empty_pick_position.y)
  assert(not clear_ok, "expected empty-space selection to miss boxes")
  assert(cleared_index == nil, string.format("expected nil cleared selection index, got %s", tostring(cleared_index)))
  assert(app.sdf_world.selection.active_box_index == nil, "expected selection to clear after empty click")

  runtime.end_frame(app, frame)
  print("sdf box selection smoke test passed")
end, debug.traceback)

runtime.shutdown(app)
cleanup()

if not ok then
  error(err)
end
