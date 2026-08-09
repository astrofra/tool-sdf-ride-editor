local sdf_world = {}

local function trim(text)
  return text:match("^%s*(.-)%s*$")
end

local function strip_comment(line)
  local comment_start = line:find("#", 1, true)
  if comment_start == nil then
    return trim(line)
  end

  return trim(line:sub(1, comment_start - 1))
end

local function split_tokens(line)
  local tokens = {}
  for token in line:gmatch("%S+") do
    tokens[#tokens + 1] = token
  end
  return tokens
end

local function parse_required_number(token, line_number, field_name)
  local value = tonumber(token)
  if value == nil then
    return nil, string.format("Invalid %s on line %d: '%s'", field_name, line_number, token)
  end

  return value, nil
end

local function find_cell_index(cells, cell_name)
  for index = 1, #cells do
    if cells[index].name == cell_name then
      return index
    end
  end

  return nil
end

function sdf_world.load_world_file(path)
  local handle = io.open(path, "rb")
  if handle == nil then
    return false, nil, string.format("Could not open world document: %s", path)
  end

  local document = {
    name = nil,
    cell_size = nil,
    active_cell_name = nil,
    active_cell_index = nil,
    cells = {}
  }
  local known_cell_names = {}
  local line_number = 0

  for raw_line in handle:lines() do
    line_number = line_number + 1
    local line = strip_comment(raw_line)
    if line ~= "" then
      local tokens = split_tokens(line)
      local keyword = tokens[1]

      if keyword == "world" then
        if #tokens ~= 2 then
          handle:close()
          return false, nil, string.format("Expected 'world <name>' on line %d", line_number)
        end
        document.name = tokens[2]
      elseif keyword == "cell_size" then
        if #tokens ~= 2 then
          handle:close()
          return false, nil, string.format("Expected 'cell_size <value>' on line %d", line_number)
        end

        local cell_size, cell_size_error = parse_required_number(tokens[2], line_number, "cell_size")
        if cell_size == nil then
          handle:close()
          return false, nil, cell_size_error
        end

        document.cell_size = cell_size
      elseif keyword == "active_cell" then
        if #tokens ~= 2 then
          handle:close()
          return false, nil, string.format("Expected 'active_cell <cell_name>' on line %d", line_number)
        end
        document.active_cell_name = tokens[2]
      elseif keyword == "cell" then
        if #tokens ~= 6 then
          handle:close()
          return false, nil, string.format(
            "Expected 'cell <name> <scene_path> <tx> <ty> <tz>' on line %d",
            line_number)
        end

        local cell_name = tokens[2]
        if known_cell_names[cell_name] then
          handle:close()
          return false, nil, string.format("Duplicate cell name on line %d: %s", line_number, cell_name)
        end

        local tx, tx_error = parse_required_number(tokens[4], line_number, "cell translation x")
        if tx == nil then
          handle:close()
          return false, nil, tx_error
        end

        local ty, ty_error = parse_required_number(tokens[5], line_number, "cell translation y")
        if ty == nil then
          handle:close()
          return false, nil, ty_error
        end

        local tz, tz_error = parse_required_number(tokens[6], line_number, "cell translation z")
        if tz == nil then
          handle:close()
          return false, nil, tz_error
        end

        document.cells[#document.cells + 1] = {
          name = cell_name,
          scene_path = tokens[3],
          world_translation = {
            x = tx,
            y = ty,
            z = tz
          }
        }
        known_cell_names[cell_name] = true
      else
        handle:close()
        return false, nil, string.format("Unknown keyword on line %d: %s", line_number, keyword)
      end
    end
  end

  handle:close()

  if document.name == nil or document.name == "" then
    return false, nil, string.format("World document is missing a 'world' declaration: %s", path)
  end
  if document.cell_size == nil or document.cell_size <= 0.0 then
    return false, nil, string.format("World document is missing a valid 'cell_size': %s", path)
  end
  if #document.cells == 0 then
    return false, nil, string.format("World document does not define any cells: %s", path)
  end

  if document.active_cell_name == nil then
    document.active_cell_name = document.cells[1].name
  end

  document.active_cell_index = find_cell_index(document.cells, document.active_cell_name)
  if document.active_cell_index == nil then
    return false, nil, string.format(
      "World document references unknown active cell '%s': %s",
      document.active_cell_name,
      path)
  end

  return true, document, nil
end

return sdf_world
