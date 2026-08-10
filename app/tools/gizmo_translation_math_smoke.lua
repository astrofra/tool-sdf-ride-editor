local hg = require("harfang")
local gizmos = require("editor.gizmos")

local function approx_eq(lhs, rhs)
  return math.abs(lhs - rhs) < 0.0001
end

local axis_origin = hg.Vec3(10.0, 2.0, 7.0)
local camera_origin = hg.Vec3(16.0, 9.0, -20.0)
local camera_forward = hg.Normalize(axis_origin - camera_origin)
local start_local_translation = hg.Vec3(1.0, 4.0, -2.0)

local function ray_toward(point)
  return camera_origin, hg.Normalize(point - camera_origin)
end

local start_ray_origin, start_ray_direction = ray_toward(axis_origin)

local x_target = axis_origin + hg.Vec3(3.2, 0.0, 0.0)
local x_ray_origin, x_ray_direction = ray_toward(x_target)
local x_ok, x_translation = gizmos.debug_solve_axis_translation(
  start_local_translation,
  "x",
  axis_origin,
  camera_forward,
  start_ray_origin,
  start_ray_direction,
  x_ray_origin,
  x_ray_direction,
  0.5)
assert(x_ok, "expected X drag solve to succeed")
assert(approx_eq(x_translation.x, 4.0), string.format("expected snapped x 4.0, got %.4f", x_translation.x))
assert(approx_eq(x_translation.y, start_local_translation.y), string.format("expected y unchanged %.4f, got %.4f", start_local_translation.y, x_translation.y))
assert(approx_eq(x_translation.z, start_local_translation.z), string.format("expected z unchanged %.4f, got %.4f", start_local_translation.z, x_translation.z))

local y_target = axis_origin + hg.Vec3(0.0, 5.4, 0.0)
local y_ray_origin, y_ray_direction = ray_toward(y_target)
local y_ok, y_translation = gizmos.debug_solve_axis_translation(
  start_local_translation,
  "y",
  axis_origin,
  camera_forward,
  start_ray_origin,
  start_ray_direction,
  y_ray_origin,
  y_ray_direction,
  1.0)
assert(y_ok, "expected Y drag solve to succeed")
assert(approx_eq(y_translation.x, start_local_translation.x), string.format("expected x unchanged %.4f, got %.4f", start_local_translation.x, y_translation.x))
assert(approx_eq(y_translation.y, 9.0), string.format("expected snapped y 9.0, got %.4f", y_translation.y))
assert(approx_eq(y_translation.z, start_local_translation.z), string.format("expected z unchanged %.4f, got %.4f", start_local_translation.z, y_translation.z))

local z_target = axis_origin + hg.Vec3(0.0, 0.0, -2.6)
local z_ray_origin, z_ray_direction = ray_toward(z_target)
local z_ok, z_translation = gizmos.debug_solve_axis_translation(
  start_local_translation,
  "z",
  axis_origin,
  camera_forward,
  start_ray_origin,
  start_ray_direction,
  z_ray_origin,
  z_ray_direction,
  0.5)
assert(z_ok, "expected Z drag solve to succeed")
assert(approx_eq(z_translation.x, start_local_translation.x), string.format("expected x unchanged %.4f, got %.4f", start_local_translation.x, z_translation.x))
assert(approx_eq(z_translation.y, start_local_translation.y), string.format("expected y unchanged %.4f, got %.4f", start_local_translation.y, z_translation.y))
assert(approx_eq(z_translation.z, -4.5), string.format("expected snapped z -4.5, got %.4f", z_translation.z))

print("gizmo translation math smoke test passed")
