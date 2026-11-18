-- server_manager.lua - "My Servers": set up and run dedicated servers from the game
-- ---------------------------------------------------------------------------
-- Opened from Multiplayer > My Servers. Each server is a folder servers/<name>/ with its
-- own server.conf, run by BlockSmithServer in its own window (it keeps running when the
-- game is closed). Everything here is editable.
--
-- Screens:  list    - your servers, online/offline, Start/Stop, Join, Edit, Delete, Create
--           create  - name a new server
--           edit    - Settings tab (name, description, game, world...) and Mods tab
--           delete  - confirm
--
-- Engine functions used:
--   engine.list_server_profiles()            -> { "Survival", ... }
--   engine.get_server_profile(name)          -> { name, motd, game, world, port, ... } (strings)
--   engine.save_server_profile(name, t)      -> true, or false + error
--   engine.create_server_profile(display)    -> folder, or nil + error
--   engine.delete_server_profile(name)       -> true, or false + error
--   engine.start_server_profile(name)        -> true, or false + error
--   engine.stop_server_profile(name)
--   engine.open_server_profile_folder(name)
--   engine.has_server_build()                -> false if BlockSmithServer isn't built
--   engine.list_games()                      -> { { folder, title, description }, ... }
--   engine.list_mods_for_game(game)          -> { { id, title, description, source, required }, ... }
--   engine.ping_server(address) / engine.get_server_status(address)
--   mp_join_address(address), mp_draw_description(...)   (from multiplayer.lua)
--
-- Called from multiplayer.lua while open:
--   render_server_manager, handle_server_manager_click/scroll/key/char
-- ---------------------------------------------------------------------------

print("[UI] server_manager.lua loaded")

local Theme = {
    colors = {
        background   = { 0.10, 0.10, 0.10, 1.00 },
        list_bg      = { 0.00, 0.00, 0.00, 0.45 },
        row_hover    = { 1.00, 1.00, 1.00, 0.08 },
        row_selected = { 1.00, 1.00, 1.00, 1.00 },
        row_sel_bg   = { 0.00, 0.00, 0.00, 1.00 },
        title        = { 1.00, 1.00, 1.00, 1.00 },
        name         = { 1.00, 1.00, 1.00, 1.00 },
        detail       = { 0.63, 0.63, 0.63, 1.00 },
        warning      = { 1.00, 0.45, 0.40, 1.00 },
        online       = { 0.45, 0.95, 0.45, 1.00 },
        starting     = { 1.00, 0.85, 0.35, 1.00 },
        outline      = { 0.00, 0.00, 0.00, 1.00 },
        outline_hov  = { 1.00, 1.00, 1.00, 1.00 },
        button       = { 0.44, 0.44, 0.44, 1.00 },
        button_hov   = { 0.50, 0.55, 0.78, 1.00 },
        button_off   = { 0.27, 0.27, 0.27, 1.00 },
        tab_active   = { 0.55, 0.55, 0.55, 1.00 },
        danger       = { 0.55, 0.20, 0.20, 1.00 },
        danger_hov   = { 0.75, 0.25, 0.25, 1.00 },
        bevel_light  = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark   = { 0.00, 0.00, 0.00, 0.35 },
        text         = { 0.88, 0.88, 0.88, 1.00 },
        text_hov     = { 1.00, 1.00, 0.63, 1.00 },
        text_off     = { 0.63, 0.63, 0.63, 1.00 },
        field_bg     = { 0.00, 0.00, 0.00, 1.00 },
        field_border = { 0.63, 0.63, 0.63, 1.00 },
        field_focus  = { 1.00, 1.00, 1.00, 1.00 },
        check_on     = { 0.35, 0.80, 0.35, 1.00 },
    },
    font = { char_w = 10, char_h = 14, base_scale = 0.5, y_offset = 1.0 },
    panel_w = 360, row_h = 36, setting_h = 22,
    button_w = 100, button_h = 20, button_gap = 4, button_pad = 12,
    field_w = 190,
}
local C = Theme.colors

-- ===========================================================================
-- STATE
-- ===========================================================================
local SM = {
    open = false,
    mode = "list",          -- "list", "create", "edit", "delete"
    servers = {},           -- { folder, cfg } for each server
    selected = nil,
    scroll = 0,
    message = nil,
    starting = {},          -- folder -> time Start was clicked (shows "Starting...")
    last_ping = -100,
    -- create
    create_name = "",
    -- edit
    folder = nil,
    cfg = nil,
    tab = "settings",       -- "settings" or "mods"
    focus = nil,            -- key of the text field being typed in
    games = {},
    mods = {},
    disabled = {},          -- mod id -> true
    edit_scroll = 0,
}

-- ===========================================================================
-- HELPERS (same look as multiplayer.lua)
-- ===========================================================================
local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function inside(mx, my, x, y, w, h) return mx >= x and mx < x + w and my >= y and my < y + h end
local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end
local function text_width(str, scale)
    if ui.text_width then return ui.text_width(str, scale) end
    return #str * Theme.font.char_w * scale
end
local function text_height(scale)
    if ui.text_height then return ui.text_height(scale) end
    return Theme.font.char_h * scale
end
local function text(str, x, y, scale, c, shadow, S)
    y = y + text_height(scale) * Theme.font.y_offset
    if shadow then ui.draw_text(str, x + S, y + S, scale, c[1] * 0.25, c[2] * 0.25, c[3] * 0.25, c[4] or 1) end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end
local function text_centered(str, cx, y, scale, c, S) text(str, cx - text_width(str, scale) / 2, y, scale, c, true, S) end
local function gui_scale(W, H) return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4) end
local function fit(str, width, scale)
    if text_width(str, scale) <= width then return str end
    local cut = str
    while #cut > 0 and text_width(cut .. "...", scale) > width do cut = cut:sub(1, -2) end
    return #cut > 0 and (cut .. "...") or ""
end

local function draw_button(b, mx, my, S, fs)
    local enabled = b.enabled ~= false
    local hovered = enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local fill = (not enabled) and C.button_off
              or (b.danger and (hovered and C.danger_hov or C.danger))
              or (b.selected and C.tab_active)
              or (hovered and C.button_hov or C.button)
    rect(b.x, b.y, b.w, b.h, hovered and C.outline_hov or C.outline)
    rect(b.x + S, b.y + S, b.w - 2 * S, b.h - 2 * S, fill)
    if enabled then
        rect(b.x + S, b.y + S, b.w - 2 * S, S, C.bevel_light)
        rect(b.x + S, b.y + b.h - 2 * S, b.w - 2 * S, S, C.bevel_dark)
    end
    local tc = (not enabled) and C.text_off or (hovered and C.text_hov or C.text)
    text_centered(b.label, b.x + b.w / 2, b.y + (b.h - text_height(fs)) / 2, fs, tc, S)
end

local function click_buttons(buttons, mx, my)
    for _, b in ipairs(buttons) do
        if b.enabled ~= false and inside(mx, my, b.x, b.y, b.w, b.h) then b.action() return true end
    end
    return false
end

-- Rows of equal-width buttons centred at the bottom
local function bottom_buttons(W, H, S, rows)
    local fs = Theme.font.base_scale * S
    local bh, gap = Theme.button_h * S, Theme.button_gap * S
    local bw = Theme.button_w * S
    for _, row in ipairs(rows) do
        for _, b in ipairs(row) do bw = math.max(bw, text_width(b.label, fs) + Theme.button_pad * S) end
    end
    local out = {}
    local y = H - 10 * S - bh * #rows - gap * (#rows - 1)
    local top = y
    for _, row in ipairs(rows) do
        local x = math.floor((W - (bw * #row + gap * (#row - 1))) / 2)
        for _, b in ipairs(row) do
            b.x, b.y, b.w, b.h = x, y, bw, bh
            out[#out + 1] = b
            x = x + bw + gap
        end
        y = y + bh + gap
    end
    return out, top
end

local function draw_field(f, value, focused, S, fs)
    rect(f.x, f.y, f.w, f.h, focused and C.field_focus or C.field_border)
    rect(f.x + S, f.y + S, f.w - 2 * S, f.h - 2 * S, C.field_bg)
    local shown = value
    local room = f.w - 8 * S - text_width("_", fs)
    while #shown > 0 and text_width(shown, fs) > room do shown = shown:sub(2) end
    local blink = focused and (ui.get_time() % 1.0) < 0.5
    text(shown .. (blink and "_" or ""), f.x + 4 * S, f.y + (f.h - text_height(fs)) / 2, fs, C.name, false, S)
end

-- ===========================================================================
-- DATA
-- ===========================================================================
local function game_title(folder)
    for _, g in ipairs(SM.games) do if g.folder == folder then return g.title end end
    return folder or "?"
end

local function address_of(cfg) return "127.0.0.1:" .. (cfg.port or "30000") end

local function refresh()
    SM.games = engine.list_games and engine.list_games() or {}
    SM.servers = {}
    for _, folder in ipairs(engine.list_server_profiles and engine.list_server_profiles() or {}) do
        SM.servers[#SM.servers + 1] = { folder = folder, cfg = engine.get_server_profile(folder) }
    end
    if SM.selected and SM.selected > #SM.servers then SM.selected = nil end
    if not SM.selected and #SM.servers > 0 then SM.selected = 1 end
end

local function ping_all()
    for _, s in ipairs(SM.servers) do
        if engine.ping_server then engine.ping_server(address_of(s.cfg)) end
    end
    SM.last_ping = ui.get_time()
end

-- "online" (with players), "starting", or "offline"
local function server_state(s)
    local st = engine.get_server_status and engine.get_server_status(address_of(s.cfg)) or nil
    if st and st.state == "online" then
        SM.starting[s.folder] = nil
        return "online", st
    end
    local started = SM.starting[s.folder]
    if started and ui.get_time() - started < 30 then return "starting", st end
    return "offline", st
end

local function normalize_world(w)
    return (w or ""):gsub("^%s+", ""):gsub("%s+$", ""):lower()
end

-- Returns the conflicting server entry if another running/starting profile uses the same world
local function running_world_conflict(target_folder, target_world)
    local norm = normalize_world(target_world)
    if norm == "" then return nil end
    for _, other in ipairs(SM.servers) do
        if other.folder ~= target_folder
           and normalize_world(other.cfg.world) == norm
           and server_state(other) ~= "offline" then
            return other
        end
    end
    return nil
end

local function selected_server() return SM.selected and SM.servers[SM.selected] or nil end

local function load_mods()
    SM.mods = engine.list_mods_for_game and engine.list_mods_for_game(SM.cfg.game) or {}
end

local function open_editor(folder)
    SM.folder = folder
    SM.cfg = engine.get_server_profile(folder)
    SM.disabled = {}
    for id in (SM.cfg.disabled_mods or ""):gmatch("[^,]+") do
        id = id:gsub("^%s+", ""):gsub("%s+$", "")
        if id ~= "" then SM.disabled[id] = true end
    end
    load_mods()
    SM.tab = "settings"
    SM.focus = nil
    SM.edit_scroll = 0
    SM.message = nil
    SM.mode = "edit"
end

-- ===========================================================================
-- SETTINGS PAGE ROWS
-- ===========================================================================
local SETTINGS = {
    { key = "name",          label = "Server name",       kind = "text", max = 40 },
    { key = "motd",          label = "Description",       kind = "text", max = 200 },
    { key = "preview",       label = "",                  kind = "preview" },
    { key = "game",          label = "Game",              kind = "game" },
    { key = "world",         label = "World",             kind = "text", max = 32, allowed = "^[%w _%-]$" },
    { key = "seed",          label = "Seed (new world)",  kind = "text", max = 32 },
    { key = "port",          label = "Port",              kind = "text", max = 5, allowed = "^%d$" },
    { key = "max_players",   label = "Max players",       kind = "number", min = 1, max = 200 },
    { key = "view_distance", label = "View distance",     kind = "number", min = 2, max = 10 },
    { key = "welcome",       label = "Welcome message",   kind = "text", max = 120 },
    { key = "whitelist",     label = "Whitelist only",    kind = "toggle" },
}

local function setting_value(key) return SM.cfg[key] or "" end

local function cycle_game(dir)
    if #SM.games == 0 then return end
    local idx = 1
    for i, g in ipairs(SM.games) do if g.folder == SM.cfg.game then idx = i end end
    idx = ((idx - 1 + dir) % #SM.games) + 1
    SM.cfg.game = SM.games[idx].folder
    SM.disabled = {}          -- different game, different mods
    load_mods()
end

local function save_editor()
    local port = tonumber(SM.cfg.port or "")
    if not port or port < 1024 or port > 65535 then
        SM.message = "Port must be a number from 1024 to 65535"
        return false
    end
    if port == 30001 then
        SM.message = "Port 30001 is used by the LAN list - pick another"
        return false
    end
    for _, s in ipairs(SM.servers) do
        if s.folder ~= SM.folder and tonumber(s.cfg.port) == port then
            SM.message = "'" .. s.folder .. "' already uses port " .. port
            return false
        end
    end
    if (SM.cfg.name or "") == "" then SM.message = "Give the server a name" return false end
    if (SM.cfg.world or "") == "" then SM.message = "Give the world a name" return false end
    local ids = {}
    for id in pairs(SM.disabled) do ids[#ids + 1] = id end
    table.sort(ids)
    SM.cfg.disabled_mods = table.concat(ids, ", ")
    local ok, err = engine.save_server_profile(SM.folder, SM.cfg)
    if ok == false then SM.message = err return false end
    return true
end

-- ===========================================================================
-- LIST SCREEN
-- ===========================================================================
local function list_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local sel = selected_server()
    local state = sel and server_state(sel) or "offline"
    local running = state ~= "offline"
    local buttons, top = bottom_buttons(W, H, S, {
        {
            { label = running and "Stop" or "Start", enabled = sel ~= nil, action = function()
                if running then
                    engine.stop_server_profile(sel.folder)
                    SM.starting[sel.folder] = nil
                    SM.message = "Stopping '" .. sel.folder .. "' (it saves first)..."
                else
                    refresh()
                    sel = selected_server()
                    if not sel then return end
                    local conflict = running_world_conflict(sel.folder, sel.cfg.world)
                    if conflict then
                        SM.message = "World '" .. (sel.cfg.world or "?") .. "' is already running in '" .. conflict.folder .. "'"
                        return
                    end
                    local ok, err = engine.start_server_profile(sel.folder)
                    if ok == false then SM.message = err
                    else SM.starting[sel.folder] = ui.get_time(); SM.message = nil end
                end
            end },
            { label = "Join", enabled = sel ~= nil and state == "online", action = function()
                SM.open = false
                mp_join_address(address_of(sel.cfg))
            end },
            { label = "Edit", enabled = sel ~= nil, action = function() open_editor(sel.folder) end },
        },
        {
            { label = "Create Server", action = function()
                SM.mode = "create"; SM.create_name = ""; SM.message = nil end },
            { label = "Delete", enabled = sel ~= nil, action = function() SM.mode = "delete"; SM.message = nil end },
            { label = "Back", action = function() SM.open = false end },
        },
    })
    local L = { S = S, fs = fs, buttons = buttons }
    L.titleY = 10 * S
    L.infoY = L.titleY + text_height(fs) + 8 * S
    L.listW = math.min(Theme.panel_w * S, W - 20 * S)
    L.listX = math.floor((W - L.listW) / 2)
    L.listY = L.infoY + text_height(fs) + 8 * S
    L.listH = top - 8 * S - L.listY
    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))
    return L
end

local function render_list(W, H)
    if ui.get_time() - SM.last_ping > 3 then ping_all() end
    local L = list_layout(W, H)
    local S, fs = L.S, L.fs
    local small = fs * 0.85
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    text_centered("My Servers", W / 2, L.titleY, fs, C.title, S)
    local info = SM.message
    local info_color = C.warning
    if not info then
        if engine.has_server_build and not engine.has_server_build() then
            info = "Build the BlockSmithServer target to run servers from here"
        else
            info = "Servers run in their own window and keep running when the game is closed"
            info_color = C.detail
        end
    end
    text_centered(info, W / 2, L.infoY, small, info_color, S)

    rect(L.listX, L.listY, L.listW, L.listH, C.list_bg)
    if #SM.servers == 0 then
        text_centered("No servers yet - click Create Server", W / 2, L.listY + L.listH / 2 - text_height(fs) / 2, fs, C.detail, S)
    end
    SM.scroll = clamp(SM.scroll, 0, math.max(0, #SM.servers - L.visible))
    for row = 1, L.visible do
        local i = row + SM.scroll
        local s = SM.servers[i]
        if not s then break end
        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S
        if i == SM.selected then
            rect(rx, ry, rw, rh, C.row_selected)
            rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_sel_bg)
        elseif inside(mx, my, rx, ry, rw, rh) then
            rect(rx, ry, rw, rh, C.row_hover)
        end

        local state, st = server_state(s)
        local status, color = "Offline", C.detail
        if state == "online" then
            status, color = "Online  " .. (st.players or 0) .. "/" .. (st.max_players or 0), C.online
        elseif state == "starting" then
            status, color = "Starting...", C.starting
        end
        local lineY = ry + 5 * S
        local sw = text_width(status, fs)
        text(status, rx + rw - 6 * S - sw, lineY, fs, color, false, S)
        text(fit(s.cfg.name or s.folder, rw - sw - 20 * S, fs), rx + 6 * S, lineY, fs, C.name, true, S)
        local line2 = game_title(s.cfg.game) .. "  -  world '" .. (s.cfg.world or "?") .. "'  -  port " .. (s.cfg.port or "?")
        text(fit(line2, rw - 12 * S, small), rx + 6 * S, lineY + text_height(fs) + 5 * S, small, C.detail, false, S)
    end
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_list(mx, my, W, H)
    local L = list_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end
    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local i = math.floor((my - L.listY) / L.rowH) + 1 + SM.scroll
        if SM.servers[i] then
            SM.selected = i
            SM.message = nil
        end
    end
end

-- ===========================================================================
-- CREATE / DELETE
-- ===========================================================================
local function create_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = "Create", action = function()
                local folder, err = engine.create_server_profile(SM.create_name)
                if not folder then SM.message = err return end
                refresh()
                for i, s in ipairs(SM.servers) do if s.folder == folder then SM.selected = i end end
                open_editor(folder)
            end },
            { label = "Cancel", action = function() SM.mode = "list"; SM.message = nil end },
        },
    })
    local fw = Theme.field_w * S
    return { S = S, fs = fs, buttons = buttons,
             field = { x = math.floor((W - fw) / 2), y = math.floor(H / 2 - 10 * S), w = fw, h = Theme.button_h * S } }
end

local function render_create(W, H)
    local L = create_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    rect(0, 0, W, H, C.background)
    text_centered("Create Server", W / 2, 12 * S, fs, C.title, S)
    text("Server name", L.field.x, L.field.y - text_height(fs) - 6 * S, fs, C.detail, false, S)
    draw_field(L.field, SM.create_name, true, S, fs)
    local hint = SM.message or "You can change the game, mods and everything else next"
    text_centered(hint, W / 2, L.field.y + L.field.h + 8 * S, fs * 0.85, SM.message and C.warning or C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function delete_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = "Delete", danger = true, action = function()
                local s = selected_server()
                if s then
                    engine.stop_server_profile(s.folder)
                    local ok, err = engine.delete_server_profile(s.folder)
                    if ok == false then SM.message = err end
                end
                SM.selected = nil
                SM.mode = "list"
                refresh()
            end },
            { label = "Cancel", action = function() SM.mode = "list" end },
        },
    })
    return { S = S, fs = fs, buttons = buttons }
end

local function render_delete(W, H)
    local L = delete_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local s = selected_server()
    rect(0, 0, W, H, C.background)
    text_centered("Delete the server '" .. (s and s.folder or "?") .. "'?", W / 2, H / 2 - 30 * S, fs, C.title, S)
    text_centered("Its settings are deleted. Its world stays in saves/" .. (s and s.cfg.world or "") .. ".",
                  W / 2, H / 2 - 10 * S, fs * 0.85, C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

-- ===========================================================================
-- EDIT SCREEN (Settings and Mods tabs)
-- ===========================================================================
local function edit_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons, top = bottom_buttons(W, H, S, {
        {
            { label = "Save", action = function()
                if save_editor() then
                    refresh()
                    local s = selected_server()
                    local is_running = s and server_state(s) ~= "offline"
                    local conflict = s and running_world_conflict(s.folder, s.cfg.world) or nil
                    SM.mode = "list"
                    if is_running then
                        SM.message = "Saved - restart the server (Stop, then Start) to use the changes"
                    elseif conflict then
                        SM.message = "Saved - stop '" .. conflict.folder .. "' before starting (same world '" .. (s.cfg.world or "?") .. "')"
                    else
                        SM.message = nil
                    end
                end
            end },
            { label = "Icon Folder", action = function() engine.open_server_profile_folder(SM.folder) end },
            { label = "Cancel", action = function() SM.mode = "list"; SM.message = nil; SM.focus = nil end },
        },
    })
    local L = { S = S, fs = fs, buttons = buttons }
    L.titleY = 8 * S
    local tw, gap, bh = 100 * S, Theme.button_gap * S, Theme.button_h * S
    local tx = math.floor((W - (tw * 2 + gap)) / 2)
    L.tabsY = L.titleY + text_height(fs) + 8 * S
    L.tabs = {
        { id = "settings", label = "Settings", x = tx, y = L.tabsY, w = tw, h = bh },
        { id = "mods", label = "Mods", x = tx + tw + gap, y = L.tabsY, w = tw, h = bh },
    }
    L.panelW = math.min(Theme.panel_w * S, W - 20 * S)
    L.panelX = math.floor((W - L.panelW) / 2)
    L.panelY = L.tabsY + bh + 6 * S
    L.msgY = top - 6 * S - text_height(fs * 0.85)
    L.panelH = L.msgY - 6 * S - L.panelY
    L.rowH = Theme.setting_h * S
    L.visible = math.max(1, math.floor((L.panelH - 4 * S) / L.rowH))
    return L
end

local function current_rows()
    if SM.tab == "settings" then return SETTINGS end
    return SM.mods
end

-- Widgets of one settings row
local function setting_widgets(L, item, rx, ry, rw, rh)
    local S = L.S
    local bh = Theme.button_h * S
    local by = ry + (rh - bh) / 2
    local fw = math.min(Theme.field_w * S, rw * 0.62)
    if item.kind == "text" then return { field = { x = rx + rw - fw, y = by, w = fw, h = bh } } end
    if item.kind == "toggle" or item.kind == "game" then
        local w = (item.kind == "game") and fw or 60 * S
        return { button = { x = rx + rw - w, y = by, w = w, h = bh } }
    end
    if item.kind == "number" then
        local sw = 22 * S
        return { plus = { x = rx + rw - sw, y = by, w = sw, h = bh },
                 minus = { x = rx + rw - fw, y = by, w = sw, h = bh } }
    end
    return {}
end

local function render_edit(W, H)
    local L = edit_layout(W, H)
    local S, fs = L.S, L.fs
    local small = fs * 0.85
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    text_centered("Server: " .. SM.folder, W / 2, L.titleY, fs, C.title, S)
    for _, t in ipairs(L.tabs) do
        draw_button({ x = t.x, y = t.y, w = t.w, h = t.h, label = t.label, selected = (SM.tab == t.id) }, mx, my, S, fs)
    end
    rect(L.panelX, L.panelY, L.panelW, L.panelH, C.list_bg)

    local rows = current_rows()
    SM.edit_scroll = clamp(SM.edit_scroll, 0, math.max(0, #rows - L.visible))
    for r = 1, L.visible do
        local item = rows[r + SM.edit_scroll]
        if not item then break end
        local rx, ry = L.panelX + 4 * S, L.panelY + 2 * S + (r - 1) * L.rowH
        local rw, rh = L.panelW - 8 * S, L.rowH - 2 * S
        local ty = ry + (rh - text_height(fs)) / 2

        if SM.tab == "settings" then
            if item.kind == "preview" then
                -- What players will see under the server's name
                if mp_draw_description then
                    mp_draw_description(SM.cfg.motd or "", rx + rw / 2, ry, small, rw, S)
                end
            else
                text(item.label, rx + 4 * S, ty, fs, C.name, true, S)
                local w = setting_widgets(L, item, rx, ry, rw, rh)
                if w.field then
                    draw_field(w.field, setting_value(item.key), SM.focus == item.key, S, fs)
                elseif w.button then
                    local label = (item.kind == "game") and ("< " .. game_title(SM.cfg.game) .. " >")
                               or ((SM.cfg.whitelist == "true") and "On" or "Off")
                    draw_button({ x = w.button.x, y = w.button.y, w = w.button.w, h = w.button.h, label = fit(label, w.button.w - 6 * S, fs) }, mx, my, S, fs)
                elseif w.plus then
                    local v = tonumber(setting_value(item.key)) or item.min
                    draw_button({ x = w.minus.x, y = w.minus.y, w = w.minus.w, h = w.minus.h, label = "-", enabled = v > item.min }, mx, my, S, fs)
                    draw_button({ x = w.plus.x, y = w.plus.y, w = w.plus.w, h = w.plus.h, label = "+", enabled = v < item.max }, mx, my, S, fs)
                    local vs = tostring(v) .. ((item.key == "view_distance") and " chunks" or "")
                    local mid = (w.minus.x + w.minus.w + w.plus.x) / 2
                    text(vs, mid - text_width(vs, fs) / 2, ty, fs, C.text, false, S)
                end
            end
        else
            -- Mods tab: a checkbox per mod
            local m = item
            local on = not SM.disabled[m.id] or m.required
            if inside(mx, my, rx, ry, rw, rh) and not m.required then rect(rx, ry, rw, rh, C.row_hover) end
            local box = rh - 6 * S
            local bx, byy = rx + 4 * S, ry + 3 * S
            rect(bx, byy, box, box, C.field_border)
            rect(bx + S, byy + S, box - 2 * S, box - 2 * S, C.field_bg)
            if on then rect(bx + 3 * S, byy + 3 * S, box - 6 * S, box - 6 * S, m.required and C.detail or C.check_on) end
            local tag = m.source .. (m.required and ", required" or "")
            local tagW = text_width(tag, small)
            text(tag, rx + rw - 4 * S - tagW, ty, small, C.detail, false, S)
            text(fit(m.title, rw - box - tagW - 24 * S, fs), bx + box + 6 * S, ty, fs, on and C.name or C.text_off, true, S)
        end
    end

    local msg, color = SM.message, C.warning
    if not msg then
        color = C.detail
        if SM.tab == "mods" then
            local on = 0
            for _, m in ipairs(SM.mods) do if not SM.disabled[m.id] or m.required then on = on + 1 end end
            msg = on .. " of " .. #SM.mods .. " mods on - players need the same mods installed"
        else
            msg = "Icon: put server_icon.png (64x64) in this server's folder (Icon Folder)"
        end
    end
    text_centered(msg, W / 2, L.msgY, small, color, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_edit(mx, my, W, H)
    local L = edit_layout(W, H)
    SM.focus = nil
    if click_buttons(L.buttons, mx, my) then return end
    for _, t in ipairs(L.tabs) do
        if inside(mx, my, t.x, t.y, t.w, t.h) then SM.tab = t.id; SM.edit_scroll = 0; SM.message = nil return end
    end
    local rows = current_rows()
    for r = 1, L.visible do
        local item = rows[r + SM.edit_scroll]
        if not item then break end
        local rx, ry = L.panelX + 4 * L.S, L.panelY + 2 * L.S + (r - 1) * L.rowH
        local rw, rh = L.panelW - 8 * L.S, L.rowH - 2 * L.S
        if SM.tab == "mods" then
            if inside(mx, my, rx, ry, rw, rh) and not item.required then
                SM.disabled[item.id] = not SM.disabled[item.id] or nil
                SM.message = nil
                return
            end
        else
            local w = setting_widgets(L, item, rx, ry, rw, rh)
            if w.field and inside(mx, my, w.field.x, w.field.y, w.field.w, w.field.h) then
                SM.focus = item.key
                return
            elseif w.button and inside(mx, my, w.button.x, w.button.y, w.button.w, w.button.h) then
                if item.kind == "game" then
                    cycle_game((mx < w.button.x + w.button.w / 2) and -1 or 1)
                else
                    SM.cfg.whitelist = (SM.cfg.whitelist == "true") and "false" or "true"
                end
                return
            elseif w.plus then
                local v = tonumber(setting_value(item.key)) or item.min
                if inside(mx, my, w.minus.x, w.minus.y, w.minus.w, w.minus.h) then
                    SM.cfg[item.key] = tostring(math.max(item.min, v - 1)) return
                elseif inside(mx, my, w.plus.x, w.plus.y, w.plus.w, w.plus.h) then
                    SM.cfg[item.key] = tostring(math.min(item.max, v + 1)) return
                end
            end
        end
    end
end

-- ===========================================================================
-- ENTRY POINTS
-- ===========================================================================
function server_manager_open()
    SM.open = true
    SM.mode = "list"
    SM.message = nil
    SM.scroll = 0
    refresh()
    ping_all()
end

function server_manager_close() SM.open = false end
function server_manager_is_open() return SM.open end

function render_server_manager(W, H)
    if SM.mode == "create" then render_create(W, H)
    elseif SM.mode == "edit" then render_edit(W, H)
    elseif SM.mode == "delete" then render_delete(W, H)
    else render_list(W, H) end
end

function handle_server_manager_click(mx, my, W, H)
    if SM.mode == "create" then click_buttons(create_layout(W, H).buttons, mx, my)
    elseif SM.mode == "edit" then click_edit(mx, my, W, H)
    elseif SM.mode == "delete" then click_buttons(delete_layout(W, H).buttons, mx, my)
    else click_list(mx, my, W, H) end
    return true
end

function handle_server_manager_scroll(dy)
    local step = (dy > 0) and -1 or 1
    if SM.mode == "edit" then SM.edit_scroll = math.max(0, SM.edit_scroll + step)
    elseif SM.mode == "list" then SM.scroll = math.max(0, SM.scroll + step) end
end

function handle_server_manager_key(key, action, mods)
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT
    if not press then return true end
    local enter = key == ui.keys.ENTER or key == ui.keys.KP_ENTER

    if SM.mode == "create" then
        if key == ui.keys.ESCAPE then SM.mode = "list"; SM.message = nil
        elseif key == ui.keys.BACKSPACE then SM.create_name = SM.create_name:sub(1, -2)
        elseif enter then
            local folder, err = engine.create_server_profile(SM.create_name)
            if not folder then SM.message = err else refresh(); open_editor(folder) end
        end
        return true
    end
    if SM.mode == "edit" then
        if SM.focus then
            if key == ui.keys.BACKSPACE then
                SM.cfg[SM.focus] = (SM.cfg[SM.focus] or ""):sub(1, -2)
            elseif enter or key == ui.keys.ESCAPE or key == 258 then   -- 258 = Tab
                SM.focus = nil
            end
        elseif key == ui.keys.ESCAPE then
            SM.mode = "list"; SM.message = nil
        end
        return true
    end
    if SM.mode == "delete" then
        if key == ui.keys.ESCAPE then SM.mode = "list" end
        return true
    end
    -- list
    if key == ui.keys.ESCAPE then SM.open = false
    elseif key == ui.keys.UP and #SM.servers > 0 then SM.selected = clamp((SM.selected or 1) - 1, 1, #SM.servers)
    elseif key == ui.keys.DOWN and #SM.servers > 0 then SM.selected = clamp((SM.selected or 0) + 1, 1, #SM.servers)
    end
    return true
end

function handle_server_manager_char(codepoint, ch)
    if not ch or ch == "" or codepoint < 32 or codepoint >= 127 then return true end
    if SM.mode == "create" then
        if #SM.create_name < 32 and ch:match("^[%w _%-]$") then SM.create_name = SM.create_name .. ch end
        return true
    end
    if SM.mode == "edit" and SM.focus then
        local item
        for _, it in ipairs(SETTINGS) do if it.key == SM.focus then item = it end end
        local cur = SM.cfg[SM.focus] or ""
        if item and #cur < (item.max or 64) and ch:match(item.allowed or "^[ -~]$") then
            SM.cfg[SM.focus] = cur .. ch
        end
    end
    return true
end
