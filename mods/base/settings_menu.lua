-- settings_menu.lua - Settings screen (title screen "Settings", or pause menu "Options...")
-- ---------------------------------------------------------------------------
-- A card in the middle of the screen: categories down the left, their settings on the right.
--   Controls - click a key, then press the new one (a key or a mouse button). Escape cancels.
--              Keys used by more than one control are outlined in red.
--   Video    - Field of View, Render Distance, Brightness (sliders), VSync (a switch),
--              Main Hand (Left / Right), lighting options which also control BlockSmith Shaders.
--              Applied straight away.
--   Sound    - the volumes (sliders).
--   Player   - your name (used in multiplayer and chat) and the password that protects it on
--              servers. Click the box, type, press Enter.
--   Server   - what other players see in their server list when you host: the description
--              (MOTD), max players and port.
-- Everything is saved to settings.txt automatically. Everything here is editable.
--
-- Engine functions used:
--   engine.get_keybinds()              -> { { action, label, key, key_name, default_key }, ... }
--   engine.set_keybind(action, key)    -> true, or false + reason
--   engine.reset_keybinds()
--   engine.get_video_settings()        -> { { id, value, min, max, step }, ... }
--   engine.set_video_setting(id, v)    -> true, or false + reason
--   engine.get_player_name()           -> "Steve"
--   engine.set_player_name(name)       -> true, or false + reason
--   engine.has_player_password(), engine.set_player_password(text)
--   engine.get_server_settings()       -> { motd, max_players, port }
--   engine.set_server_setting(id, v)   -> true, or false + reason   (id: motd, max_players, port)
--   engine.has_server_icon()
--   engine.close_settings()
--   engine.is_in_game()
--
-- C++ calls:
--   render_settings(W, H)
--   handle_settings_click(mx, my, W, H)
--   handle_settings_scroll(dy)
--   handle_settings_key(key, action, mods)   -> true = handled
--   handle_settings_char(codepoint, char)    -> true = handled
--   handle_settings_mouse(code)              -> true = handled (a mouse button as a key)
--   on_settings_opened()
-- For other mods: settings_set_tab("video") opens a category.
-- ---------------------------------------------------------------------------

print("[UI] settings_menu.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local te = (function()   -- the caret in text boxes (lualib/textedit.lua)
    local ok, m = pcall(require, "textedit")
    if ok then return m end
    print("[UI] lualib/textedit.lua is missing: text boxes can only type at the end")
    local function view(_, t, w, sc)
        while #t > 0 and ui.text_width(t, sc) > w do t = t:sub(2) end
        return t, ui.text_width(t, sc)
    end
    return { reset = function() end, focus = function() end, view = view,
             key = function(_, t, k) if k == ui.keys.BACKSPACE then return t:sub(1, -2), true end return t, false end,
             insert = function(_, t, s, max) if max and #t >= max then return t end return t .. s end,
             draw_caret = function(_, x, y, h, r, g, b, a) if ui.get_time() % 1 < 0.5 then ui.draw_rect(x, y, 1, h, r, g, b, a) end end }
end)()

local Theme = {
    colors = {
        backdrop        = { 0.02, 0.03, 0.05, 0.62 },   -- over the title screen's panorama
        backdrop_game   = { 0.00, 0.00, 0.00, 0.55 },   -- over the world, opened from the pause menu
        sidebar         = { 0.05, 0.06, 0.08, 0.94 },
        content         = { 0.09, 0.10, 0.13, 0.92 },
        card_edge       = { 1.00, 1.00, 1.00, 0.10 },
        divider         = { 1.00, 1.00, 1.00, 0.07 },
        accent          = { 1.00, 0.78, 0.28, 1.00 },   -- gold, like the title screen's
        accent_text     = { 0.10, 0.08, 0.04, 1.00 },   -- text on the accent
        title           = { 1.00, 1.00, 1.00, 1.00 },
        text            = { 0.90, 0.91, 0.93, 1.00 },
        detail          = { 0.60, 0.62, 0.67, 1.00 },
        section         = { 0.55, 0.58, 0.65, 1.00 },
        nav_hover       = { 1.00, 1.00, 1.00, 0.05 },
        nav_active      = { 1.00, 1.00, 1.00, 0.09 },
        row_hover       = { 1.00, 1.00, 1.00, 0.035 },
        control         = { 0.13, 0.14, 0.18, 1.00 },   -- key caps, fields, switch tracks
        control_edge    = { 1.00, 1.00, 1.00, 0.16 },
        control_hover   = { 1.00, 1.00, 1.00, 0.42 },
        track           = { 1.00, 1.00, 1.00, 0.12 },
        knob            = { 0.96, 0.97, 0.98, 1.00 },
        conflict        = { 1.00, 0.38, 0.33, 1.00 },
        waiting         = { 1.00, 0.85, 0.35, 1.00 },
        error           = { 1.00, 0.45, 0.40, 1.00 },
        disabled        = { 1.00, 1.00, 1.00, 0.25 },
    },

    -- Font metrics at scale 1.0 (PixeloidSans is about 20px tall at scale 1)
    font = { char_w = 10, char_h = 14, base_scale = 0.5, y_offset = 1.0 },

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    card_w = 560, card_h = 340,
    sidebar_w = 132,
    nav_h = 22,
    row_h = 24, section_h = 20,
    control_h = 17,
    key_w = 96,
    slider_w = 150,
    field_w = 150, wide_field_w = 210,
}

local C = Theme.colors

local TABS = {
    { id = "controls", label = "Controls", title = "Controls",
      desc = "Click a key, then press the new one. Red keys are used more than once." },
    { id = "video",    label = "Video",    title = "Video",
      desc = "How the world looks. Changes apply straight away." },
    { id = "sound",    label = "Sound",    title = "Sound",
      desc = "How loud each kind of sound is." },
    { id = "player",   label = "Player",   title = "Player",
      desc = "Your name in chat and multiplayer, and the password that protects it." },
    { id = "server",   label = "Network",  title = "Network",
      desc = "Hosting a game, and network input for client mods." },
}

local LABELS = {
    fov             = "Field of View",
    render_distance = "Render Distance",
    vsync           = "VSync",
    main_hand       = "Main Hand",
    brightness      = "Brightness",
    light_quality   = "Lighting",
    bloom           = "Bloom",
    filmic          = "Filmic Look",
    mirrors         = "Mirror Reflections",
    udp_input       = "Network input (client mods)",
    volume_master   = "Master Volume",
    volume_music    = "Music",
    volume_effects  = "Blocks & Effects",
    volume_ambient  = "Weather & Ambience",
}

-- Settings most players never need, shown only with "Advanced" on (the switch in the sidebar,
-- remembered in settings.txt). The rest is what everyone uses.
local ADVANCED = {
    vsync = true, bloom = true, filmic = true, mirrors = true,     -- Video
    volume_ambient = true,                                         -- Sound
    port = true, udp_input = true,                                 -- Network
}
local ADVANCED_CONTROL_SECTIONS = { Hotbar = true, Other = true }

-- Which section each control is listed under (in this order)
local CONTROL_SECTIONS = {
    { "Movement", { forward = 1, back = 2, left = 3, right = 4, jump = 5, sneak = 6, sprint = 7 } },
    { "Actions", { pick_block = 1, drop = 2, inventory = 3 } },
    { "Chat & Multiplayer", { chat = 1, command = 2, player_list = 3 } },
    { "View", { third_person = 1, hide_hud = 2, debug_info = 3 } },
    { "Hotbar", nil },          -- hotbar1..9
    { "Other", false },         -- anything else (controls mods add)
}

local function value_text(s)
    if s.id == "vsync" then return (s.value ~= 0) and "On" or "Off" end
    if s.id == "main_hand" then return (s.value ~= 0) and "Left" or "Right" end
    if s.id == "render_distance" then return s.value .. " chunks" end
    if s.id == "brightness" then          -- Minecraft's words for the ends; the middle is the game's own
        if s.value <= 0 then return "Moody" elseif s.value >= 100 then return "Bright"
        elseif s.value == 50 then return "Default" end
        return s.value .. "%"
    end
    if s.id:find("^volume_") then return (s.value == 0) and "Off" or (s.value .. "%") end
    return tostring(s.value)
end

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    tab = "controls",
    advanced = false,     -- showing advanced settings too
    binds = {},
    video = {},
    sound = {},
    player = {},
    server = {},
    editing = nil,        -- the text item being typed in (nil = none)
    edit_text = "",       -- what's in the box while typing
    scroll = 0,           -- first entry shown (entries are rows and section headings)
    waiting = nil,        -- action waiting for a key press
    dragging = nil,       -- the slider being dragged
    message = nil,
}

-- ===========================================================================
-- DRAWING HELPERS
-- ===========================================================================

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function inside(mx, my, x, y, w, h) return mx >= x and mx < x + w and my >= y and my < y + h end

local function rect(x, y, w, h, c, a)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], (c[4] or 1) * (a or 1))
end
-- A box with its corners clipped by `r` (a pixel-art rounded box), and its outline
local function box(x, y, w, h, r, c)
    rect(x + r, y, w - 2 * r, h, c)
    rect(x, y + r, r, h - 2 * r, c)
    rect(x + w - r, y + r, r, h - 2 * r, c)
end
local function outline(x, y, w, h, r, c, t)
    t = t or 1
    rect(x + r, y, w - 2 * r, t, c)
    rect(x + r, y + h - t, w - 2 * r, t, c)
    rect(x, y + r, t, h - 2 * r, c)
    rect(x + w - t, y + r, t, h - 2 * r, c)
end

local function text_width(str, scale)
    if ui.text_width then return ui.text_width(str, scale) end
    return #str * Theme.font.char_w * scale
end
local function text_height(scale)
    if ui.text_height then return ui.text_height(scale) end
    return Theme.font.char_h * scale
end
-- `y` is the TOP of the text
local function text(str, x, y, scale, c, shadow, S)
    y = y + text_height(scale) * Theme.font.y_offset
    if shadow then ui.draw_text(str, x + S, y + S, scale, 0, 0, 0, (c[4] or 1) * 0.45) end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end
-- Text centred vertically in a band from y to y + h
local function text_mid(str, x, y, h, scale, c, shadow, S)
    text(str, x, y + (h - text_height(scale)) / 2, scale, c, shadow, S)
end
-- Shortened with "..." to fit a width
local function fit(str, w, scale)
    if text_width(str, scale) <= w then return str end
    while #str > 1 and text_width(str .. "...", scale) > w do str = str:sub(1, -2) end
    return str .. "..."
end

local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

-- The categories' little pictures, drawn with rectangles (s = their size)
local function icon(id, x, y, s, c)
    local u = math.max(1, math.floor(s / 8))
    if id == "controls" then          -- a key cap
        outline(x, y + u, s, s - 2 * u, u, c, u)
        rect(x + 2 * u, y + s - 4 * u, s - 4 * u, u, c)
        rect(x + 2 * u, y + 3 * u, u, u, c); rect(x + 4 * u, y + 3 * u, u, u, c); rect(x + 6 * u, y + 3 * u, u, u, c)
    elseif id == "video" then         -- a screen on a stand
        outline(x, y, s, s - 3 * u, u, c, u)
        rect(x + 3 * u, y + s - 2 * u, s - 6 * u, u, c)
        rect(x + s / 2 - u / 2, y + s - 3 * u, u, u, c)
    elseif id == "sound" then         -- a speaker and its waves
        rect(x, y + 3 * u, 2 * u, 2 * u, c)
        rect(x + 2 * u, y + 2 * u, u, 4 * u, c)
        rect(x + 3 * u, y + u, u, 6 * u, c)
        rect(x + 5 * u, y + 3 * u, u, 2 * u, c)
        rect(x + 7 * u, y + 2 * u, u, 4 * u, c)
    elseif id == "player" then        -- a head and shoulders
        box(x + 2 * u, y, s - 4 * u, 3 * u, 0, c)
        box(x + u, y + 4 * u, s - 2 * u, s - 4 * u, u, c)
    elseif id == "server" then        -- stacked server units with lights
        for k = 0, 2 do
            outline(x, y + k * 3 * u, s, 2 * u + 1, 0, c, u)
            rect(x + s - 3 * u, y + k * 3 * u + u, u, u, c)
        end
    end
end

-- ===========================================================================
-- DATA
-- ===========================================================================

local function refresh()
    state.advanced = engine.get_ui_setting ~= nil and engine.get_ui_setting("settings_advanced") == "1"
    state.binds = (engine and engine.get_keybinds and engine.get_keybinds()) or {}
    state.video, state.sound = {}, {}
    for _, s in ipairs((engine and engine.get_video_settings and engine.get_video_settings()) or {}) do
        if s.id:find("^volume_") then state.sound[#state.sound + 1] = s
        elseif s.id == "udp_input" then state.udp_input = s           -- (listed under Network)
        else state.video[#state.video + 1] = s end
    end
    -- Text items: kind = "text", value, max (length), allowed (pattern for one character)
    state.player = {
        { id = "name", kind = "text", label = "Player Name", max = 16, allowed = "^[%w_%-]$",
          hint = "Letters, numbers, _ and -", value = (engine.get_player_name and engine.get_player_name()) or "" },
        -- Protects your name on servers. Never shown or stored - only a hash of it is kept.
        { id = "password", kind = "text", label = "Server Password", max = 64, allowed = "^[ -~]$",
          secret = true, value = "", hint = "Protects your name on servers",
          is_set = engine.has_player_password and engine.has_player_password() or false },
    }
    local sv = (engine.get_server_settings and engine.get_server_settings()) or {}
    state.server = {
        { id = "motd", kind = "text", label = "Description", max = 200, allowed = "^[ -~]$", wide = true,
          value = sv.motd or "" },
        { id = "max_players", kind = "number", label = "Max Players",
          value = sv.max_players or 50, min = 2, max = 200, step = 1 },
        { id = "port", kind = "text", label = "Port", max = 5, allowed = "^%d$",
          value = tostring(sv.port or 30000) },
    }
end

-- The list for the open category: section headings and rows, in order
local function entries()
    local out = {}
    -- (a heading is only shown once something under it is: a section of advanced settings
    -- disappears whole when they're hidden)
    local pending, hidden, sectionAdvanced = nil, 0, false
    local function section(title) pending = title; sectionAdvanced = ADVANCED_CONTROL_SECTIONS[title] == true and state.tab == "controls" end
    local function row(item)
        if not state.advanced and (sectionAdvanced or ADVANCED[item.id]) then hidden = hidden + 1; return end
        if pending then out[#out + 1] = { section = pending }; pending = nil end
        out[#out + 1] = { item = item }
    end
    if state.tab == "controls" then
        local used = {}
        for _, sec in ipairs(CONTROL_SECTIONS) do
            local list = {}
            for _, b in ipairs(state.binds) do
                if not used[b.action] then
                    local order
                    if sec[2] == nil then order = b.action:match("^hotbar(%d+)$") and tonumber(b.action:match("%d+"))
                    elseif sec[2] == false then order = 1000 + #list
                    else order = sec[2][b.action] end
                    if order then list[#list + 1] = { b, order } end
                end
            end
            table.sort(list, function(a, b) return a[2] < b[2] end)
            if #list > 0 then
                section(sec[1])
                for _, e in ipairs(list) do used[e[1].action] = true; row(e[1]) end
            end
        end
    elseif state.tab == "video" then
        section("Display")
        local lighting = {
            light_quality = true, bloom = true, filmic = true, mirrors = true,
        }
        for _, s in ipairs(state.video) do if s.id ~= "main_hand" and not lighting[s.id] then row(s) end end
        local any = false
        for _, s in ipairs(state.video) do
            if lighting[s.id] then
                if not any then section("Lighting"); any = true end
                row(s)
            end
        end
        for _, s in ipairs(state.video) do
            if s.id == "main_hand" then section("Hand"); row(s) end
        end
    elseif state.tab == "sound" then
        section("Volume")
        for _, s in ipairs(state.sound) do row(s) end
    elseif state.tab == "player" then
        section("Profile"); row(state.player[1])
        section("Security"); row(state.player[2])
    elseif state.tab == "server" then
        section("Hosting")
        for _, s in ipairs(state.server) do row(s) end
        if state.udp_input then section("Client mods"); row(state.udp_input) end
    end
    -- what's hidden: one line that shows it
    if hidden > 0 then out[#out + 1] = { more = hidden } end
    return out
end

local function set_advanced(on)
    state.advanced = on
    if not on then state.waiting = nil end
    if engine.set_ui_setting then engine.set_ui_setting("settings_advanced", on and "1" or "0") end
end

-- How many controls use each key (to highlight duplicates)
local function key_counts()
    local counts = {}
    for _, b in ipairs(state.binds) do counts[b.key] = (counts[b.key] or 0) + 1 end
    return counts
end

-- Saves a setting through the right engine function
local function set_value(item, value)
    if item.id == "name" then return engine.set_player_name(value) end
    if item.id == "password" then
        if value == "" then return true end   -- empty = keep the current password
        return engine.set_player_password(value)
    end
    if state.tab == "server" and item.id ~= "udp_input" then return engine.set_server_setting(item.id, tostring(value)) end
    return engine.set_video_setting(item.id, value)
end

local function commit_edit()
    if not state.editing then return end
    local ok, err = set_value(state.editing, state.edit_text)
    if ok == false then state.message = err; return end   -- not allowed: stay in the box
    state.editing = nil
    state.message = nil
    refresh()
end
local function cancel_edit()
    state.editing = nil
    state.message = nil
end

-- ===========================================================================
-- LAYOUT
-- ===========================================================================

local function layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local L = { S = S, fs = fs }
    local m = 10 * S
    L.cardW = math.floor(math.min(Theme.card_w * S, W - 2 * m))
    L.cardH = math.floor(math.min(Theme.card_h * S, H - 2 * m))
    L.cardX = math.floor((W - L.cardW) / 2)
    L.cardY = math.floor((H - L.cardH) / 2)
    L.sideW = math.floor(clamp(L.cardW * 0.25, 104 * S, Theme.sidebar_w * S))

    -- Sidebar: title, the categories, Done at the bottom
    L.nav = {}
    local ny = L.cardY + 46 * S
    for i, t in ipairs(TABS) do
        L.nav[i] = { id = t.id, label = t.label, x = L.cardX + 6 * S, y = ny, w = L.sideW - 12 * S, h = Theme.nav_h * S }
        ny = ny + (Theme.nav_h + 2) * S
    end
    L.done = { x = L.cardX + 10 * S, y = L.cardY + L.cardH - 30 * S, w = L.sideW - 20 * S, h = 20 * S, label = "Done" }
    -- "Advanced": the whole row is the switch
    L.adv = { x = L.cardX + 6 * S, y = L.done.y - 26 * S, w = L.sideW - 12 * S, h = 20 * S }

    -- Content: a header, the list, a status line
    L.cx = L.cardX + L.sideW
    L.cw = L.cardW - L.sideW
    L.pad = 16 * S
    L.headerY = L.cardY + 12 * S
    L.dividerY = L.cardY + 44 * S
    L.reset = nil
    if state.tab == "controls" then
        local w = 78 * S
        L.reset = { x = L.cx + L.cw - L.pad - w, y = L.cardY + 14 * S, w = w, h = 18 * S, label = "Reset All" }
    end
    L.listX = L.cx + L.pad - 6 * S
    L.listW = L.cw - 2 * L.pad + 12 * S
    L.listY = L.dividerY + 6 * S
    L.statusY = L.cardY + L.cardH - 18 * S
    L.listH = L.statusY - 6 * S - L.listY
    return L
end

local function entry_h(L, e) return ((e.section and Theme.section_h) or Theme.row_h) * L.S end

-- The entries shown, from the scroll position, and where each one is
local function visible(L)
    local list = entries()
    -- the furthest it can scroll: the last entries just fill the list
    local maxScroll, used = #list, 0
    for i = #list, 1, -1 do
        used = used + entry_h(L, list[i])
        if used > L.listH then break end
        maxScroll = i - 1
    end
    state.scroll = clamp(state.scroll, 0, math.max(0, maxScroll))
    local shown, y = {}, L.listY
    for i = state.scroll + 1, #list do
        local h = entry_h(L, list[i])
        if y + h > L.listY + L.listH + 0.5 then break end
        shown[#shown + 1] = { e = list[i], y = y, h = h, index = i }
        y = y + h
    end
    return shown, list, maxScroll
end

-- The control on the right of a row (by the kind of setting)
local function widget(L, item, ry, rh)
    local S = L.S
    local right = L.listX + L.listW - 8 * S
    local ch = Theme.control_h * S
    local cy = ry + math.floor((rh - ch) / 2)
    if state.tab == "controls" then
        local w = Theme.key_w * S
        return { kind = "key", x = right - w, y = cy, w = w, h = ch }
    end
    if item.kind == "text" then
        local w = math.min((item.wide and Theme.wide_field_w or Theme.field_w) * S, L.listW * 0.6)
        return { kind = "field", x = right - w, y = cy, w = w, h = ch }
    end
    if item.id == "vsync" then
        local w = 26 * S
        return { kind = "switch", x = right - w, y = ry + math.floor((rh - 13 * S) / 2), w = w, h = 13 * S }
    end
    if item.id == "main_hand" then
        local w = 46 * S
        return { kind = "segments", x = right - 2 * w, y = cy, w = 2 * w, h = ch, seg = w,
                 options = { { "Left", 1 }, { "Right", 0 } } }
    end
    -- Choices the engine lists (lighting, bloom, visual effects): buttons; unsupported ones are
    -- greyed out (opt[3]) and say why when clicked
    if item.options then
        -- every choice as wide as the longest label needs (so "This computer" fits), at least 46
        local w = 46 * S
        local opts = {}
        for i, o in ipairs(item.options) do
            opts[i] = { o.label, o.value, o.disabled }
            w = math.max(w, text_width(o.label, L.fs * 0.85) + 24 * S)
        end
        -- (never wider than 60% of the row: the setting's name needs the rest)
        w = math.min(w, math.floor(L.listW * 0.6 / #opts))
        return { kind = "segments", x = right - #opts * w, y = cy, w = #opts * w, h = ch, seg = w, options = opts }
    end
    local valueW = 46 * S
    local w = Theme.slider_w * S
    return { kind = "slider", x = right - valueW - w, y = cy, w = w, h = ch, valueX = right - valueW + 8 * S, valueW = valueW - 8 * S }
end

local function slider_value(item, wdg, mx)
    local t = clamp((mx - wdg.x) / wdg.w, 0, 1)
    local steps = math.floor((item.max - item.min) / item.step + 0.5)
    return item.min + math.floor(t * steps + 0.5) * item.step
end

-- ===========================================================================
-- DRAWING
-- ===========================================================================

local function draw_primary(b, mx, my, S, fs)
    local hov = inside(mx, my, b.x, b.y, b.w, b.h)
    local c = hov and { 1.00, 0.85, 0.42, 1 } or C.accent
    box(b.x, b.y, b.w, b.h, 2 * S, c)
    rect(b.x + 2 * S, b.y + b.h - S, b.w - 4 * S, S, { 0, 0, 0, 0.25 })
    text_mid(b.label, b.x + (b.w - text_width(b.label, fs)) / 2, b.y, b.h, fs, C.accent_text, false, S)
end

local function draw_secondary(b, mx, my, S, fs)
    local hov = inside(mx, my, b.x, b.y, b.w, b.h)
    box(b.x, b.y, b.w, b.h, S, { 1, 1, 1, hov and 0.14 or 0.06 })
    outline(b.x, b.y, b.w, b.h, S, hov and C.control_hover or C.control_edge)
    text_mid(b.label, b.x + (b.w - text_width(b.label, fs * 0.85)) / 2, b.y, b.h, fs * 0.85, C.text, false, S)
end

local function draw_field(w, item, S, fs)
    local focused = state.editing ~= nil and state.editing.id == item.id
    box(w.x, w.y, w.w, w.h, S, C.control)
    outline(w.x, w.y, w.w, w.h, S, focused and C.accent or C.control_edge)
    local tx, tw = w.x + 5 * S, w.w - 10 * S
    if focused then
        local shown, caret_x = te.view("settings", state.edit_text, tw, fs, item.secret and "*" or nil)
        text_mid(shown, tx, w.y, w.h, fs, C.title, false, S)
        local th = text_height(fs)
        te.draw_caret("settings", tx + caret_x, w.y + (w.h - th) / 2 - S, th + 2 * S, C.accent[1], C.accent[2], C.accent[3], 1)
        return
    end
    local value, color = tostring(item.value), C.text
    if item.secret then value, color = item.is_set and "********" or "Not set", item.is_set and C.text or C.detail end
    if value == "" then value, color = "Empty", C.detail end
    text_mid(fit(value, tw, fs), tx, w.y, w.h, fs, color, false, S)
end

local function draw_list(L, mx, my)
    local S, fs = L.S, L.fs
    local shown, list, maxScroll = visible(L)
    local counts = key_counts()
    state.hover_note = nil            -- (set again below while over a row with a note)
    local _, _, left_down = ui.get_mouse()

    for _, v in ipairs(shown) do
        local e, ry, rh = v.e, v.y, v.h
        if e.more then
            local hov = inside(mx, my, L.listX, ry, L.listW, rh)
            if hov then box(L.listX, ry, L.listW, rh, S, C.row_hover) end
            local t = e.more .. (e.more == 1 and " more setting" or " more settings") .. " - Show advanced"
            text_mid(fit(t, L.listW - 16 * S, fs * 0.85), L.listX + 8 * S, ry, rh, fs * 0.85, hov and C.text or C.detail, false, S)
        elseif e.section then
            local sfs = fs * 0.72
            text(string.upper(e.section), L.listX + 8 * S, ry + rh - text_height(sfs) - 4 * S, sfs, C.section, false, S)
            rect(L.listX + 8 * S + text_width(string.upper(e.section), sfs) + 6 * S, ry + rh - 4 * S - text_height(sfs) / 2,
                 L.listW - 16 * S - text_width(string.upper(e.section), sfs) - 6 * S, 1, C.divider)
        else
            local item = e.item
            if inside(mx, my, L.listX, ry, L.listW, rh) then box(L.listX, ry, L.listW, rh, S, C.row_hover) end
            local label = item.label or LABELS[item.id] or item.id
            local wdg = widget(L, item, ry, rh)
            text_mid(fit(label, wdg.x - L.listX - 20 * S, fs), L.listX + 8 * S, ry, rh, fs, C.text, false, S)

            local hov = inside(mx, my, wdg.x, wdg.y, wdg.w, wdg.h)
            if wdg.kind == "key" then
                local waiting = state.waiting == item.action
                local conflict = (counts[item.key] or 0) > 1
                box(wdg.x, wdg.y, wdg.w, wdg.h, S, C.control)
                rect(wdg.x + S, wdg.y + wdg.h - S, wdg.w - 2 * S, S, { 0, 0, 0, 0.35 })      -- the cap's lower edge
                local edge = waiting and C.waiting or (conflict and C.conflict or (hov and C.control_hover or C.control_edge))
                if waiting then edge = { C.waiting[1], C.waiting[2], C.waiting[3], 0.55 + 0.45 * math.abs(math.sin(ui.get_time() * 4)) } end
                outline(wdg.x, wdg.y, wdg.w, wdg.h, S, edge)
                local label2 = waiting and "Press a key..." or (item.key_name or "?")
                local col = waiting and C.waiting or (conflict and C.conflict or C.title)
                text_mid(fit(label2, wdg.w - 8 * S, fs * 0.9), wdg.x + (wdg.w - text_width(fit(label2, wdg.w - 8 * S, fs * 0.9), fs * 0.9)) / 2,
                         wdg.y, wdg.h, fs * 0.9, col, false, S)
            elseif wdg.kind == "field" then
                draw_field(wdg, item, S, fs)
            elseif wdg.kind == "switch" then
                local on = item.value ~= 0
                local track = on and C.accent or { 1, 1, 1, hov and 0.22 or 0.14 }
                box(wdg.x, wdg.y, wdg.w, wdg.h, 3 * S, track)
                local k = wdg.h - 4 * S
                local kx = on and (wdg.x + wdg.w - 2 * S - k) or (wdg.x + 2 * S)
                box(kx, wdg.y + 2 * S, k, k, 2 * S, C.knob)
                local vt = on and "On" or "Off"
                text_mid(vt, wdg.x - 8 * S - text_width(vt, fs * 0.85), wdg.y, wdg.h, fs * 0.85, C.detail, false, S)
            elseif wdg.kind == "segments" then
                box(wdg.x, wdg.y, wdg.w, wdg.h, S, C.control)
                outline(wdg.x, wdg.y, wdg.w, wdg.h, S, C.control_edge)
                for i, opt in ipairs(wdg.options) do
                    local sx = wdg.x + (i - 1) * wdg.seg
                    local sel = item.value == opt[2]
                    local off = opt[3]                       -- not available on this computer
                    if sel then box(sx + 2 * S, wdg.y + 2 * S, wdg.seg - 4 * S, wdg.h - 4 * S, S, C.accent)
                    elseif not off and inside(mx, my, sx, wdg.y, wdg.seg, wdg.h) then box(sx + 2 * S, wdg.y + 2 * S, wdg.seg - 4 * S, wdg.h - 4 * S, S, { 1, 1, 1, 0.08 }) end
                    text_mid(opt[1], sx + (wdg.seg - text_width(opt[1], fs * 0.85)) / 2, wdg.y, wdg.h, fs * 0.85,
                             sel and C.accent_text or (off and C.disabled or C.text), false, S)
                end
                -- why some choices are greyed out, next to them
                if item.note and inside(mx, my, L.listX, ry, L.listW, rh) then state.hover_note = item.note end
            elseif wdg.kind == "slider" then
                -- dragging: follow the mouse while the button is held
                if state.dragging == item.id then
                    if left_down then
                        local v = slider_value(item, wdg, mx)
                        if v ~= item.value then
                            local ok, err = set_value(item, v)
                            if ok == false then state.message = err else item.value = v end
                        end
                    else
                        state.dragging = nil
                        refresh()
                    end
                end
                local t = (item.max > item.min) and (item.value - item.min) / (item.max - item.min) or 0
                local ty = wdg.y + math.floor(wdg.h / 2) - 2 * S
                box(wdg.x, ty, wdg.w, 4 * S, S, C.track)
                box(wdg.x, ty, math.max(2 * S, math.floor(wdg.w * t)), 4 * S, S, C.accent)
                local kw, kh = 6 * S, 12 * S
                local kx = math.floor(wdg.x + wdg.w * t - kw / 2)
                local active = hov or state.dragging == item.id
                box(kx, wdg.y + math.floor((wdg.h - kh) / 2), kw, kh, S, C.knob)
                if active then outline(kx - S, wdg.y + math.floor((wdg.h - kh) / 2) - S, kw + 2 * S, kh + 2 * S, S, C.accent) end
                text_mid(value_text(item), wdg.valueX, wdg.y, wdg.h, fs * 0.85, C.text, false, S)
            end
        end
    end

    -- A scroll bar when the list is longer than its space
    if maxScroll > 0 then
        local trackX = L.cx + L.cw - 7 * S
        rect(trackX, L.listY, 2 * S, L.listH, { 1, 1, 1, 0.06 })
        local frac = #shown / math.max(1, #list)
        local th = math.max(16 * S, math.floor(L.listH * frac))
        local ty = L.listY + math.floor((L.listH - th) * (state.scroll / maxScroll))
        box(trackX, ty, 2 * S, th, 0, { 1, 1, 1, 0.35 })
    end
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function settings_set_tab(id)
    for _, t in ipairs(TABS) do
        if t.id == id then
            state.tab, state.scroll, state.waiting, state.editing = id, 0, nil, nil
            return true
        end
    end
    return false
end

function on_settings_opened()
    state.scroll = 0
    state.waiting = nil
    state.message = nil
    state.editing = nil
    state.dragging = nil
    refresh()
end

function render_settings(W, H)
    if not ui or not ui.draw_rect then return end
    if #state.binds == 0 then refresh() end

    local L = layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local in_game = engine.is_in_game and engine.is_in_game()

    rect(0, 0, W, H, in_game and C.backdrop_game or C.backdrop)

    -- The card: a soft shadow, the sidebar and the content
    box(L.cardX - 3 * S, L.cardY - 3 * S + 3 * S, L.cardW + 6 * S, L.cardH + 6 * S, 4 * S, { 0, 0, 0, 0.30 })
    box(L.cardX, L.cardY, L.sideW, L.cardH, 3 * S, C.sidebar)
    rect(L.cx, L.cardY, L.cw - 3 * S, L.cardH, C.content)
    rect(L.cx + L.cw - 3 * S, L.cardY + 3 * S, 3 * S, L.cardH - 6 * S, C.content)
    outline(L.cardX, L.cardY, L.cardW, L.cardH, 3 * S, C.card_edge)
    rect(L.cx, L.cardY + S, 1, L.cardH - 2 * S, C.divider)

    -- Sidebar
    text("Settings", L.cardX + 12 * S, L.cardY + 12 * S, fs * 1.2, C.title, true, S)
    text("Saved automatically", L.cardX + 12 * S, L.cardY + 12 * S + text_height(fs * 1.2) + 5 * S, fs * 0.68, C.detail, false, S)
    for _, n in ipairs(L.nav) do
        local active = state.tab == n.id
        local hov = inside(mx, my, n.x, n.y, n.w, n.h)
        if active then
            box(n.x, n.y, n.w, n.h, S, C.nav_active)
            rect(n.x, n.y + 3 * S, 2 * S, n.h - 6 * S, C.accent)
        elseif hov then
            box(n.x, n.y, n.w, n.h, S, C.nav_hover)
        end
        local col = active and C.title or (hov and C.text or C.detail)
        local is = 8 * S
        icon(n.id, n.x + 9 * S, n.y + math.floor((n.h - is) / 2), is, active and C.accent or col)
        text_mid(n.label, n.x + 9 * S + is + 7 * S, n.y, n.h, fs, col, false, S)
    end
    local advHover
    do  -- the Advanced switch
        local a = L.adv
        local hov = inside(mx, my, a.x, a.y, a.w, a.h)
        advHover = hov
        if hov then box(a.x, a.y, a.w, a.h, S, C.nav_hover) end
        text_mid("Advanced", a.x + 9 * S, a.y, a.h, fs * 0.9, hov and C.text or C.detail, false, S)
        local sw, sh = 26 * S, 13 * S
        local sx, sy = a.x + a.w - 9 * S - sw, a.y + math.floor((a.h - sh) / 2)
        box(sx, sy, sw, sh, 3 * S, state.advanced and C.accent or { 1, 1, 1, hov and 0.22 or 0.14 })
        local k = sh - 4 * S
        box(state.advanced and (sx + sw - 2 * S - k) or (sx + 2 * S), sy + 2 * S, k, k, 2 * S, C.knob)
    end
    draw_primary(L.done, mx, my, S, fs)

    -- Content header
    local tab
    for _, t in ipairs(TABS) do if t.id == state.tab then tab = t end end
    text(tab.title, L.cx + L.pad, L.headerY, fs * 1.1, C.title, true, S)
    local descW = L.cw - 2 * L.pad - (L.reset and (L.reset.w + 8 * S) or 0)
    local desc = tab.desc
    if state.tab == "server" then
        desc = (engine.has_server_icon and engine.has_server_icon()) and "Hosting a game (your server_icon.png is its icon), and network input for client mods."
            or "Hosting a game (add server_icon.png for an icon), and network input for client mods."
    end
    text(fit(desc, descW, fs * 0.72), L.cx + L.pad, L.headerY + text_height(fs * 1.1) + 5 * S, fs * 0.72, C.detail, false, S)
    if L.reset then draw_secondary(L.reset, mx, my, S, fs) end
    rect(L.cx + L.pad - 6 * S, L.dividerY, L.cw - 2 * L.pad + 12 * S, 1, C.divider)

    draw_list(L, mx, my)
    if advHover then state.hover_note = "Show every setting. Off: just the ones most players use." end

    -- Status line: what to do now, or what went wrong
    local note, color = nil, C.detail
    if state.message then note, color = state.message, C.error
    elseif state.hover_note then note, color = state.hover_note, C.detail
    elseif state.waiting then note, color = "Press a key or mouse button for this control. Escape cancels.", C.waiting
    elseif state.editing then note, color = "Type, then press Enter. Escape cancels.", C.waiting
    elseif state.tab == "server" then note = "Description colours: {gold}text  {#ff8800}text  {b}bold{/b}  {n} starts a 2nd line"
    elseif state.tab == "player" then note = "Your name: 1-16 letters, numbers, _ or -. The password is never shown or stored." end
    if note then
        text_mid(fit(note, L.cw - 2 * L.pad, fs * 0.7), L.cx + L.pad, L.statusY, 12 * S, fs * 0.7, color, false, S)
    end
end

function handle_settings_click(mx, my, W, H)
    local L = layout(W, H)

    -- Clicking anywhere except the box being typed in saves what was typed
    if state.editing then
        for _, v in ipairs((visible(L))) do
            local item = v.e.item
            if item and item.id == state.editing.id then
                local f = widget(L, item, v.y, v.h)
                if inside(mx, my, f.x, f.y, f.w, f.h) then   -- (the caret goes where you click)
                    te.focus("settings", state.edit_text, mx - (f.x + 5 * L.S), L.fs, item.secret and "*" or nil)
                    return true
                end
            end
        end
        commit_edit()
        if state.editing then return true end
    end
    state.message = nil

    for _, n in ipairs(L.nav) do
        if inside(mx, my, n.x, n.y, n.w, n.h) then settings_set_tab(n.id); return true end
    end
    if inside(mx, my, L.adv.x, L.adv.y, L.adv.w, L.adv.h) then
        set_advanced(not state.advanced)
        state.scroll = 0
        return true
    end
    if inside(mx, my, L.done.x, L.done.y, L.done.w, L.done.h) then
        state.waiting = nil
        engine.close_settings()
        return true
    end
    if L.reset and inside(mx, my, L.reset.x, L.reset.y, L.reset.w, L.reset.h) then
        engine.reset_keybinds()
        state.waiting = nil
        refresh()
        return true
    end

    for _, v in ipairs((visible(L))) do
        if v.e.more and inside(mx, my, L.listX, v.y, L.listW, v.h) then
            set_advanced(true)
            return true
        end
        local item = v.e.item
        if item then
            local w = widget(L, item, v.y, v.h)
            local hit = inside(mx, my, w.x, w.y - 3 * L.S, w.w, w.h + 6 * L.S)
            if hit and w.kind == "field" then
                state.editing = item
                state.edit_text = item.secret and "" or tostring(item.value)
                te.reset("settings")
                te.focus("settings", state.edit_text)
                state.waiting = nil
                return true
            elseif hit and w.kind == "key" then
                state.waiting = (state.waiting == item.action) and nil or item.action   -- click again to cancel
                return true
            elseif hit and w.kind == "switch" then
                local ok, err = set_value(item, (item.value ~= 0) and 0 or 1)
                if ok == false then state.message = err end
                refresh()
                return true
            elseif hit and w.kind == "segments" then
                local i = clamp(math.floor((mx - w.x) / w.seg) + 1, 1, #w.options)
                if w.options[i][3] then                  -- greyed out: say why instead
                    state.message = item.note or "Not available on this computer"
                    return true
                end
                local ok, err = set_value(item, w.options[i][2])
                if ok == false then state.message = err end
                refresh()
                return true
            elseif w.kind == "slider" and inside(mx, my, w.x - 6 * L.S, w.y - 3 * L.S, w.w + 12 * L.S, w.h + 6 * L.S) then
                local val = slider_value(item, w, mx)
                local ok, err = set_value(item, val)
                if ok == false then state.message = err else item.value = val end
                state.dragging = item.id            -- keeps following the mouse while held
                state.waiting = nil
                return true
            end
        end
    end

    state.waiting = nil   -- clicked somewhere else: stop waiting for a key
    return true
end

function handle_settings_scroll(dy)
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while drawing
end

function handle_settings_key(key, action, mods)
    if state.editing then
        if action ~= ui.actions.PRESS and action ~= ui.actions.REPEAT then return true end
        if key == ui.keys.ESCAPE then
            cancel_edit()
        elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            commit_edit()
        else
            -- Left / Right / Home / End / Backspace / Delete (Ctrl: by word)
            state.edit_text = te.key("settings", state.edit_text, key, action, mods)
        end
        return true
    end
    if state.waiting then
        if action ~= ui.actions.PRESS then return true end
        if key == ui.keys.ESCAPE then
            state.waiting = nil            -- cancel, stay in settings
            return true
        end
        local ok, err = engine.set_keybind(state.waiting, key)
        state.message = (ok == false) and err or nil
        state.waiting = nil
        refresh()
        return true
    end
    return false   -- Escape falls through to C++ (closes settings)
end

-- A mouse button while Settings is open (code = 1000 + button, as keybinds store it): if a
-- control is waiting for its new key, that button becomes it (middle mouse, side buttons...)
function handle_settings_mouse(code)
    if not state.waiting then return false end
    local ok, err = engine.set_keybind(state.waiting, code)
    state.message = (ok == false) and err or nil
    state.waiting = nil
    refresh()
    return true                    -- don't also click whatever is under the mouse
end

function handle_settings_char(codepoint, ch)
    local item = state.editing
    if not item or not ch or ch == "" then return false end
    if ch:match(item.allowed or "^[ -~]$") then
        state.edit_text = te.insert("settings", state.edit_text, ch, item.max or 32)
    end
    return true
end
