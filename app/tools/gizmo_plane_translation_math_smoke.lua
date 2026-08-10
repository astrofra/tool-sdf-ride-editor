local hg = require("harfang")
local gizmos = require("editor.gizmos")

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

local axis_origin = hg.Vec3(10.0, 2.0, 7.0)
local camera_origin = hg.Vec3(16.0, 9.0, -20.0)
local start_local_translation = hg.Vec3(1.0, 4.0, -2.0)

local function ray_toward(point)
  return camera_origin, hg.Normalize(point - camera_origin)
end

local function assert_translation(ok, translation, expected_x, expected_y, expected_z, label)
  assert(ok, string.format("expected %s drag solve to succeed", label))
  assert(approx_eq(translation.x, expected_x), string.format("expected %s x %.4f, got %.4f", label, expected_x, translation.x))
  assert(approx_eq(translation.y, expected_y), string.format("expected %s y %.4f, got %.4f", label, expected_y, translation.y))
  assert(approx_eq(translation.z, expected_z), string.format("expected %s z %.4f, got %.4f", label, expected_z, translation.z))
end

local xy_start_ray_origin, xy_start_ray_direction = ray_toward(axis_origin + hg.Vec3(0.6, 0.6, 0.0))
local xy_current_ray_origin, xy_current_ray_direction = ray_toward(axis_origin + hg.Vec3(3.2, 5.4, 0.0))
local xy_ok, xy_translation = gizmos.debug_solve_plane_translation(
  start_local_translation,
  "xy",
  axis_origin,
  xy_start_ray_origin,
  xy_start_ray_direction,
  xy_current_ray_origin,
  xy_current_ray_direction,
  0.5)
assert_translation(xy_ok, xy_translation, 3.5, 9.0, start_local_translation.z, "XY")

local yz_start_ray_origin, yz_start_ray_direction = ray_toward(axis_origin + hg.Vec3(0.0, 0.6, 0.6))
local yz_current_ray_origin, yz_current_ray_direction = ray_toward(axis_origin + hg.Vec3(0.0, 5.4, -2.6))
local yz_ok, yz_translation = gizmos.debug_solve_plane_translation(
  start_local_translation,
  "yz",
  axis_origin,
  yz_start_ray_origin,
  yz_start_ray_direction,
  yz_current_ray_origin,
  yz_current_ray_direction,
  0.5)
assert_translation(yz_ok, yz_translation, start_local_translation.x, 9.0, -5.0, "YZ")

local zx_start_ray_origin, zx_start_ray_direction = ray_toward(axis_origin + hg.Vec3(0.6, 0.0, 0.6))
local zx_current_ray_origin, zx_current_ray_direction = ray_toward(axis_origin + hg.Vec3(3.2, 0.0, -2.6))
local zx_ok, zx_translation = gizmos.debug_solve_plane_translation(
  start_local_translation,
  "zx",
  axis_origin,
  zx_start_ray_origin,
  zx_start_ray_direction,
  zx_current_ray_origin,
  zx_current_ray_direction,
  0.5)
assert_translation(zx_ok, zx_translation, 3.5, start_local_translation.y, -5.0, "ZX")

print("gizmo plane translation math smoke test passed")
