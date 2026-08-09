local hg = require("harfang")

local log_panel = {}

local default_max_entries = 256
local default_window_height = 196

local level_colors = {
  info = hg.Color(0.78, 0.82, 0.88, 1.0),
  warn = hg.Color(0.95, 0.78, 0.35, 1.0),
  error = hg.Color(0.95, 0.45, 0.45, 1.0)
}

local function ensure_state(app)
  if app.log_panel ~= nil then
    return app.log_panel
  end

  local state = {
    entries = {},
    next_sequence = 1,
    max_entries = default_max_entries,
    auto_scroll = true,
    scroll_to_bottom = false
  }

  app.log_panel = state
  return state
end

local function trim_entries(state)
  while #state.entries > state.max_entries do
    table.remove(state.entries, 1)
  end
end

local function append_entry(app, level, message)
  local state = ensure_state(app)
  local entry = {
    level = level,
    text = string.format("%03d | %s", state.next_sequence, message)
  }

  state.next_sequence = state.next_sequence + 1
  state.entries[#state.entries + 1] = entry
  trim_entries(state)
  state.scroll_to_bottom = true

  return entry
end

function log_panel.attach(app)
  return ensure_state(app)
end

function log_panel.info(app, message)
  return append_entry(app, "info", message)
end

function log_panel.warn(app, message)
  return append_entry(app, "warn", message)
end

function log_panel.error(app, message)
  return append_entry(app, "error", message)
end

function log_panel.update(app, frame)
  local state = ensure_state(app)

  hg.ImGuiSetNextWindowPos(hg.Vec2(24, frame.window_height - default_window_height - 24))
  hg.ImGuiSetNextWindowSize(hg.Vec2(frame.window_width - 48, default_window_height))

  if hg.ImGuiBegin(
    "Log",
    true,
    hg.ImGuiWindowFlags_NoMove | hg.ImGuiWindowFlags_NoResize | hg.ImGuiWindowFlags_NoCollapse) then
    if hg.ImGuiSmallButton("Clear") then
      state.entries = {}
      state.scroll_to_bottom = false
    end
    hg.ImGuiSameLine()
    local _auto_scroll_changed
    _auto_scroll_changed, state.auto_scroll = hg.ImGuiCheckbox("Auto-scroll", state.auto_scroll)

    if hg.ImGuiBeginChild("LogEntries", hg.Vec2(0, 0), true) then
      for index = 1, #state.entries do
        local entry = state.entries[index]
        hg.ImGuiTextColored(level_colors[entry.level] or level_colors.info, entry.text)
      end

      if state.auto_scroll and state.scroll_to_bottom then
        hg.ImGuiSetScrollHereY(1.0)
      end
    end
    hg.ImGuiEndChild()
  end

  hg.ImGuiEnd()
  state.scroll_to_bottom = false
end

return log_panel
