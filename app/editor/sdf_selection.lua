local hg = require("harfang")
local ground_plane = require("editor.ground_plane")

local sdf_selection = {}

local ray_epsilon = 0.0001

local function get_box_world_bounds(cell_state, box)
  local center = hg.Vec3(
    cell_state.world_translation.x + box.transform.translation.x,
    cell_state.world_translation.y + box.transform.translation.y,
    cell_state.world_translation.z + box.transform.translation.z)

  return hg.Vec3(
      center.x - box.half_size.x,
      center.y - box.half_size.y,
      center.z - box.half_size.z),
    hg.Vec3(
      center.x + box.half_size.x,
      center.y + box.half_size.y,
      center.z + box.half_size.z)
end

local function component_by_axis(vector, axis)
  if axis == 1 then
    return vector.x
  elseif axis == 2 then
    return vector.y
  end

  return vector.z
end

local function intersect_aabb(origin, direction, bounds_min, bounds_max)
  local t_min = 0.0
  local t_max = math.huge

  for axis = 1, 3 do
    local axis_origin = component_by_axis(origin, axis)
    local axis_direction = component_by_axis(direction, axis)
    local axis_min = component_by_axis(bounds_min, axis)
    local axis_max = component_by_axis(bounds_max, axis)

    if math.abs(axis_direction) < ray_epsilon then
      if axis_origin < axis_min or axis_origin > axis_max then
        return false, nil
      end
    else
      local inverse_direction = 1.0 / axis_direction
      local axis_t0 = (axis_min - axis_origin) * inverse_direction
      local axis_t1 = (axis_max - axis_origin) * inverse_direction

      if axis_t0 > axis_t1 then
        axis_t0, axis_t1 = axis_t1, axis_t0
      end

      t_min = math.max(t_min, axis_t0)
      t_max = math.min(t_max, axis_t1)

      if t_min > t_max then
        return false, nil
      end
    end
  end

  if t_max < 0.0 then
    return false, nil
  end

  if t_min >= 0.0 then
    return true, t_min
  end

  return true, t_max
end

function sdf_selection.pick_box_index(cell_state, frame, screen_x, screen_y)
  if cell_state == nil or cell_state.scene_file == nil then
    return nil, nil
  end

  local ray_ok
  local ray_origin
  local ray_direction
  ray_ok, ray_origin, ray_direction = ground_plane.screen_to_world_ray(frame, screen_x, screen_y)
  if not ray_ok then
    return nil, nil
  end

  local nearest_box_index = nil
  local nearest_hit_distance = nil
  local boxes = cell_state.scene_file.scene.boxes

  for index = 1, #boxes do
    local bounds_min
    local bounds_max
    bounds_min, bounds_max = get_box_world_bounds(cell_state, boxes[index])

    local hit_ok
    local hit_distance
    hit_ok, hit_distance = intersect_aabb(ray_origin, ray_direction, bounds_min, bounds_max)
    if hit_ok and (nearest_hit_distance == nil or hit_distance < nearest_hit_distance) then
      nearest_box_index = index
      nearest_hit_distance = hit_distance
    end
  end

  return nearest_box_index, nearest_hit_distance
end

return sdf_selection
