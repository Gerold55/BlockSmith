-- Minimap: a client mod (it runs only in your game, so it works on any server that allows
-- client mods). Shows the land around you from above, with waypoints.
--
--   M                 show / hide the map
--   .minimap zoom N   1 = close up ... 3 = far out
--   .wp add <name>    a waypoint where you stand (saved per world / server)
--   .wp del <name>    .wp list    .wp clear
--
-- How it works: client.get_surface(x, z) gives the top block of each column and
-- client.get_block_color(name) its colour. A few hundred columns are read per frame, so the map
-- fills in smoothly and keeps up with changes. Blocks further than the server allows show as dark.
--
-- Client mods are off until you switch them on: Mods screen > Minimap > Enable, then restart.

local storage = client.get_mod_storage()

local SIZE = 150                    -- map size on screen, in pixels (scaled with the window)
local CELLS = 50                    -- map cells across
local PER_FRAME = 350               -- columns read per frame

local shown = storage:get("shown") ~= "false"
local zoom = storage:get_int("zoom")                          -- 1..3, saved
if zoom < 1 or zoom > 3 then zoom = 2 end
local step = ({ 1, 2, 4 })[zoom]    -- blocks per map cell

-- Column cache: [x][z] = { y, r, g, b }
local columns = {}
local cached = 0
local sweep = 0

local function column(x, z) local c = columns[x] return c and c[z] end

local function read_column(x, z)
    local y, name = client.get_surface(x, z)
    local col = columns[x]
    if not col then col = {} columns[x] = col end
    if not y then
        if col[z] == nil then cached = cached + 1 end
        col[z] = false                                   -- unknown (not loaded, or too far)
        return
    end
    local r, g, b = client.get_block_color(name)
    if col[z] == nil then cached = cached + 1 end
    col[z] = { y, r, g, b }
end

-- ---------------------------------------------------------------------------
-- Waypoints, per world / server
-- ---------------------------------------------------------------------------
local place = "?"
local waypoints = {}                -- { { name, x, y, z, r, g, b } }

local COLORS = { { 1, 0.35, 0.35 }, { 0.4, 0.8, 1 }, { 1, 0.85, 0.3 }, { 0.6, 1, 0.5 }, { 0.9, 0.5, 1 } }

local function save_waypoints()
    local lines = {}
    for _, w in ipairs(waypoints) do
        lines[#lines + 1] = string.format("%s|%d|%d|%d", w.name, w.x, w.y, w.z)
    end
    storage:set("wp:" .. place, table.concat(lines, ";"))
end

local function load_waypoints()
    waypoints = {}
    for entry in (storage:get("wp:" .. place) or ""):gmatch("[^;]+") do
        local name, x, y, z = entry:match("^(.-)|(-?%d+)|(-?%d+)|(-?%d+)$")
        if name then
            local c = COLORS[#waypoints % #COLORS + 1]
            waypoints[#waypoints + 1] = { name = name, x = tonumber(x), y = tonumber(y), z = tonumber(z),
                                          r = c[1], g = c[2], b = c[3] }
        end
    end
end

client.register_on_join(function(info)
    place = info.mode == "server" and ("server:" .. (info.address or "?")) or ("world:" .. (info.world or "?"))
    load_waypoints()
    columns, cached = {}, 0
end)

client.register_chat_command("wp", {
    params = "add <name> | del <name> | list | clear",
    description = "Minimap waypoints",
    func = function(param)
        local cmd, name = param:match("^(%S*)%s*(.-)$")
        if cmd == "add" and name ~= "" then
            local x, y, z = client.get_player_position()
            for i = #waypoints, 1, -1 do if waypoints[i].name == name then table.remove(waypoints, i) end end
            local c = COLORS[#waypoints % #COLORS + 1]
            waypoints[#waypoints + 1] = { name = name, x = math.floor(x), y = math.floor(y), z = math.floor(z),
                                          r = c[1], g = c[2], b = c[3] }
            save_waypoints()
            return true, "Waypoint '" .. name .. "' saved."
        elseif cmd == "del" and name ~= "" then
            for i = #waypoints, 1, -1 do
                if waypoints[i].name == name then table.remove(waypoints, i) save_waypoints() return true, "Removed." end
            end
            return false, "No waypoint called '" .. name .. "'."
        elseif cmd == "list" then
            if #waypoints == 0 then return true, "No waypoints yet: .wp add <name>" end
            local x, _, z = client.get_player_position()
            for _, w in ipairs(waypoints) do
                local d = math.floor(math.sqrt((w.x - x) ^ 2 + (w.z - z) ^ 2))
                client.display_chat_message(string.format("  %s: %d %d %d (%d blocks away)", w.name, w.x, w.y, w.z, d),
                                            w.r, w.g, w.b)
            end
            return true
        elseif cmd == "clear" then
            waypoints = {}
            save_waypoints()
            return true, "All waypoints removed."
        end
        return false, "Usage: .wp add <name> | del <name> | list | clear"
    end,
})

client.register_chat_command("minimap", {
    params = "[zoom 1-3]",
    description = "Show or hide the minimap, or zoom it",
    func = function(param)
        local n = tonumber(param:match("^zoom%s+(%d)$"))
        if n then
            zoom = math.max(1, math.min(3, n))
            step = ({ 1, 2, 4 })[zoom]
            storage:set("zoom", zoom)
            return true, "Minimap zoom " .. zoom
        end
        shown = not shown
        storage:set("shown", tostring(shown))
        return true, shown and "Minimap on" or "Minimap off"
    end,
})

client.register_on_key(function(key, action)
    if key == ui.keys.M and action == ui.actions.PRESS then
        shown = not shown
        storage:set("shown", tostring(shown))
    end
end)

-- ---------------------------------------------------------------------------
-- Reading the land: a sweep over the map area, a few hundred columns per frame
-- ---------------------------------------------------------------------------
client.register_on_update(function()
    if not shown then return end
    local px, _, pz = client.get_player_position()
    local half = CELLS // 2
    local cx, cz = math.floor(px / step), math.floor(pz / step)
    local total = CELLS * CELLS
    for _ = 1, PER_FRAME do
        sweep = (sweep + 1) % total
        local i, j = sweep % CELLS - half, sweep // CELLS - half
        read_column((cx + i) * step, (cz + j) * step)
    end
    if cached > 60000 then columns, cached = {}, 0 end   -- wandered far: start a fresh cache
end)

-- ---------------------------------------------------------------------------
-- Drawing
-- ---------------------------------------------------------------------------
local DIRECTIONS = { "E", "SE", "S", "SW", "W", "NW", "N", "NE" }

client.register_on_hud(function(W, H)
    if not shown then return end
    local scale = math.max(0.75, H / 900)
    local size = math.floor(SIZE * scale)
    local cell = size / CELLS
    local x0, y0 = W - size - 12, 12

    local p = client.get_player_state()
    if not p then return end
    local half = CELLS // 2
    local cx, cz = math.floor(p.x / step), math.floor(p.z / step)

    ui.draw_rect(x0 - 3, y0 - 3, size + 6, size + 6, 0, 0, 0, 0.55)       -- frame

    -- One row at a time; neighbouring cells of the same colour are drawn as one rectangle
    for j = -half, CELLS - half - 1 do
        local run_x, run_r, run_g, run_b, run_n = nil, 0, 0, 0, 0
        local py = y0 + (j + half) * cell
        local function flush()
            if run_x and run_n > 0 then
                ui.draw_rect(x0 + run_x * cell, py, run_n * cell + 0.5, cell + 0.5, run_r, run_g, run_b, 1)
            end
        end
        for i = -half, CELLS - half - 1 do
            local wx, wz = (cx + i) * step, (cz + j) * step
            local c = column(wx, wz)
            local r, g, b = 0.08, 0.08, 0.1                               -- unknown: dark
            if c then
                r, g, b = c[2], c[3], c[4]
                local north = column(wx, wz - step)                       -- light from the north-west, like
                if north then                                             -- Minecraft's maps: slopes show
                    local d = c[1] - north[1]
                    local k = d > 0 and 1.15 or d < 0 and 0.8 or 1
                    r, g, b = math.min(1, r * k), math.min(1, g * k), math.min(1, b * k)
                end
                -- fewer distinct colours = longer runs = fewer rectangles
                r, g, b = math.floor(r * 24) / 24, math.floor(g * 24) / 24, math.floor(b * 24) / 24
            end
            local col = i + half
            if run_x and r == run_r and g == run_g and b == run_b then
                run_n = run_n + 1
            else
                flush()
                run_x, run_r, run_g, run_b, run_n = col, r, g, b, 1
            end
        end
        flush()
    end

    -- Waypoints (on the edge when they're off the map)
    local mid = x0 + size / 2
    local midy = y0 + size / 2
    for _, w in ipairs(waypoints) do
        local dx, dz = (w.x - p.x) / step * cell, (w.z - p.z) / step * cell
        local lim = size / 2 - 4
        local m = math.max(math.abs(dx), math.abs(dz))
        if m > lim then dx, dz = dx / m * lim, dz / m * lim end
        ui.draw_rect(mid + dx - 3, midy + dz - 3, 7, 7, 0, 0, 0, 0.8)
        ui.draw_rect(mid + dx - 2, midy + dz - 2, 5, 5, w.r, w.g, w.b, 1)
    end

    -- You: a white dot with a pointer showing where you face (yaw 0 = east, 90 = south)
    local a = math.rad(p.yaw or 0)
    ui.draw_rect(mid - 3, midy - 3, 7, 7, 0, 0, 0, 0.8)
    ui.draw_rect(mid - 2, midy - 2, 5, 5, 1, 1, 1, 1)
    for k = 1, 3 do
        local fx, fy = mid + math.cos(a) * (4 + k * 2.5), midy + math.sin(a) * (4 + k * 2.5)
        ui.draw_rect(fx - 1, fy - 1, 3, 3, 1, 1, 1, 1)
    end

    -- North marker and a line of text under the map
    local ts = 0.3 * scale
    ui.draw_text("N", mid - ui.text_width("N", ts) / 2, y0 + 2, ts, 1, 1, 1)
    local dir = DIRECTIONS[math.floor((((p.yaw or 0) % 360) + 22.5) / 45) % 8 + 1]
    local info = string.format("%d, %d, %d  %s", math.floor(p.x), math.floor(p.y), math.floor(p.z), dir)
    ui.draw_rect(x0 - 3, y0 + size + 3, size + 6, ui.text_height(ts) + 8, 0, 0, 0, 0.55)
    ui.draw_text(info, x0 + (size - ui.text_width(info, ts)) / 2, y0 + size + 7, ts, 1, 1, 1)
end)

print("minimap loaded: M to show/hide, .wp add <name> for waypoints")
