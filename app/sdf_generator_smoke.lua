local sdf = require("sdf-generator")

local function file_exists(path)
  local handle = io.open(path, "rb")
  if handle == nil then
    return false
  end

  handle:close()
  return true
end

local scene_path = "sdf-scenes/frame_006_blockout.sdfscene"
if not file_exists(scene_path) then
  scene_path = "sdf-scenes/tile_000.sdfscene"
end

local ok, scene_file, err = sdf.load_scene_file(scene_path)
assert(ok, err)
assert(scene_file.scene.name == "frame_006_blockout")

scene_file.build_settings.cell_size = 16.0

local request = sdf.BuildRequest()
request.export_obj = false
request.unwrap_uvs = false

local build_ok, build_result, build_err = sdf.build_scene_file(scene_file, request)
assert(build_ok, build_err)
assert(build_result.sampled_cells > 0)
assert(build_result.generated_triangle_count > 0)

print("sdf-generator smoke test passed")
