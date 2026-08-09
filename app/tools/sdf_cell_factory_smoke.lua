local sdf = require("sdf-generator")
local sdf_world = require("editor.sdf_world")
local sdf_cell_factory = require("editor.sdf_cell_factory")

local temp_output_path = "../artifacts/smoke/default_cell_template.sdfscene"

local function approx_eq(a, b)
  return math.abs(a - b) < 0.0001
end

local ok, world_document, load_error = sdf_world.load_world_file("sdf-worlds/default.sdfworld")
assert(ok, load_error)

local scene_file = sdf_cell_factory.make_default_scene_file(world_document, "default_cell_template_smoke")
local bounds = scene_file.build_settings.bounds

assert(approx_eq(bounds.min.x, -60.0), "expected min.x = -60.0")
assert(approx_eq(bounds.min.y, -10.0), "expected min.y = -10.0")
assert(approx_eq(bounds.min.z, -60.0), "expected min.z = -60.0")
assert(approx_eq(bounds.max.x, 60.0), "expected max.x = 60.0")
assert(approx_eq(bounds.max.y, 110.0), "expected max.y = 110.0")
assert(approx_eq(bounds.max.z, 60.0), "expected max.z = 60.0")
assert(scene_file.build_settings.cell_size == 1.0, "expected default build cell size = 1.0")
assert(#scene_file.scene.boxes == 1, "new template cell should contain one default socle box")

local base_box = scene_file.scene.boxes:at(0)
assert(base_box.name == "socle", string.format("expected socle box name, got %s", tostring(base_box.name)))
assert(approx_eq(base_box.transform.translation.x, 0.0), "expected socle tx = 0.0")
assert(approx_eq(base_box.transform.translation.y, -2.5), "expected socle ty = -2.5")
assert(approx_eq(base_box.transform.translation.z, 0.0), "expected socle tz = 0.0")
assert(approx_eq(base_box.half_size.x, 50.0), "expected socle hx = 50.0")
assert(approx_eq(base_box.half_size.y, 2.5), "expected socle hy = 2.5")
assert(approx_eq(base_box.half_size.z, 50.0), "expected socle hz = 50.0")

local save_ok, save_error = sdf.save_scene_file(scene_file, temp_output_path)
assert(save_ok, save_error)

local serialized_handle = io.open(temp_output_path, "rb")
assert(serialized_handle ~= nil, "expected serialized default cell scene file")
local serialized_scene = serialized_handle:read("*a")
serialized_handle:close()
assert(serialized_scene:find("box add socle", 1, true) ~= nil, "expected serialized socle box to be additive")

local reload_ok, reloaded_scene_file, reload_error = sdf.load_scene_file(temp_output_path)
assert(reload_ok, reload_error)
assert(reloaded_scene_file.scene.name == "default_cell_template_smoke")
assert(#reloaded_scene_file.scene.boxes == 1, "reloaded default cell should preserve the socle box")

os.remove(temp_output_path)

print("sdf cell factory smoke test passed")
