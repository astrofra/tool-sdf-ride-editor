local hg = require("harfang")

local ground_plane = {}

local ray_epsilon = 0.0001

local function is_finite_number(value)
  return value == value and value ~= math.huge and value ~= -math.huge
end

local function is_finite_vec3(value)
  return is_finite_number(value.x) and is_finite_number(value.y) and is_finite_number(value.z)
end

function ground_plane.intersect_ray(origin, direction, plane_y)
  local target_plane_y = plane_y or 0.0
  if math.abs(direction.y) < ray_epsilon then
    return false, nil
  end

  local distance_to_plane = (target_plane_y - origin.y) / direction.y
  if not is_finite_number(distance_to_plane) or distance_to_plane < 0.0 then
    return false, nil
  end

  local point = origin + direction * distance_to_plane
  if not is_finite_vec3(point) then
    return false, nil
  end

  return true, point
end

function ground_plane.screen_to_world_ray(frame, screen_x, screen_y)
  if not frame.inverse_projection_ok then
    return false, nil, nil
  end

  local ok, view_position = hg.UnprojectFromScreenSpace(
    frame.inverse_projection_matrix,
    hg.Vec3(screen_x, screen_y, 1.0),
    frame.resolution)
  if not ok or not is_finite_vec3(view_position) then
    return false, nil, nil
  end

  local direction = hg.GetX(frame.camera_world) * view_position.x +
    hg.GetY(frame.camera_world) * view_position.y +
    hg.GetZ(frame.camera_world) * view_position.z
  if not is_finite_vec3(direction) or hg.Len(direction) <= ray_epsilon then
    return false, nil, nil
  end

  return true, hg.GetT(frame.camera_world), hg.Normalize(direction)
end

function ground_plane.screen_to_ground(frame, screen_x, screen_y, plane_y)
  local ok, origin, direction = ground_plane.screen_to_world_ray(frame, screen_x, screen_y)
  if not ok then
    return false, nil
  end

  return ground_plane.intersect_ray(origin, direction, plane_y)
end

return ground_plane
