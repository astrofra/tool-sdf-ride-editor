local sdf_history = {}

local function copy_runtime_id_list(box_runtime_ids)
  local copy = {}

  if box_runtime_ids == nil then
    return copy
  end

  for index = 1, #box_runtime_ids do
    copy[index] = box_runtime_ids[index]
  end

  return copy
end

local function same_merge_target(lhs, rhs)
  return lhs.cell_name == rhs.cell_name and
    lhs.action_kind == rhs.action_kind and
    lhs.target_box_runtime_id == rhs.target_box_runtime_id
end

function sdf_history.create_state()
  return {
    undo_entries = {},
    redo_entries = {},
    next_sequence = 1
  }
end

function sdf_history.can_undo(state)
  return state ~= nil and #state.undo_entries > 0
end

function sdf_history.can_redo(state)
  return state ~= nil and #state.redo_entries > 0
end

function sdf_history.clear(state)
  if state == nil then
    return false
  end

  local had_entries = #state.undo_entries > 0 or #state.redo_entries > 0
  state.undo_entries = {}
  state.redo_entries = {}
  return had_entries
end

function sdf_history.record(state, entry)
  if state == nil then
    return "ignored", nil
  end

  entry.sequence = state.next_sequence
  state.next_sequence = state.next_sequence + 1

  local last_entry = state.undo_entries[#state.undo_entries]
  if last_entry ~= nil and same_merge_target(last_entry, entry) then
    last_entry.after_scene_text = entry.after_scene_text
    last_entry.after_box_runtime_ids = copy_runtime_id_list(entry.after_box_runtime_ids)
    last_entry.after_selected_box_index = entry.after_selected_box_index
    last_entry.description = entry.description
    last_entry.sequence = entry.sequence
    state.redo_entries = {}
    return "merged", last_entry
  end

  entry.before_box_runtime_ids = copy_runtime_id_list(entry.before_box_runtime_ids)
  entry.after_box_runtime_ids = copy_runtime_id_list(entry.after_box_runtime_ids)
  state.undo_entries[#state.undo_entries + 1] = entry
  state.redo_entries = {}

  return "pushed", entry
end

function sdf_history.undo(state)
  if not sdf_history.can_undo(state) then
    return false, nil
  end

  local entry = table.remove(state.undo_entries)
  state.redo_entries[#state.redo_entries + 1] = entry
  return true, entry
end

function sdf_history.redo(state)
  if not sdf_history.can_redo(state) then
    return false, nil
  end

  local entry = table.remove(state.redo_entries)
  state.undo_entries[#state.undo_entries + 1] = entry
  return true, entry
end

return sdf_history
