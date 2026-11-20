-- multiplayer.lua - Multiplayer screen: your name, saved servers, join / add / edit / delete
-- ---------------------------------------------------------------------------
-- Shown after clicking Multiplayer on the title screen. Everything is editable.
--
-- To play together, one player opens a world and clicks "Host Game" in the pause menu.
-- Everyone else adds that player's address here and joins. Everybody needs the same
-- game selected (and the same mods).
--
-- Your name is shown at the top; change it in Settings > Player.
--
-- Server descriptions (set by the owner in Settings > Server) can use tags:
--   {red} {orange} {gold} {yellow} {green} {aqua} {blue} {purple} {pink} {white} {gray}
--   {dark_gray} {black}, any colour as {#ff8800}, {b} bold on, {/b} bold off,
--   {r} back to normal, {n} new line (two lines are shown).
--   Example: {gold}{b}MY SERVER{/b} {gray}>> {aqua}Survival{n}{yellow}Season 1 - join us!
--
-- Engine functions used:
--   engine.list_servers()                 -> { { name = "Home", address = "192.168.1.20" }, ... }
--   engine.save_servers(list)             -- same shape as list_servers
--   engine.get_player_name()              -> "Steve"
--   engine.join_server(address)           -> true, or false + error   (starts connecting)
--   engine.ping_server(address)           -- checks a server without joining (takes up to ~5 s)
--   engine.get_server_status(address)     -> nil, or { state = "pinging"|"online"|"offline",
--                                             motd, players, max_players, ping, names = {...},
--                                             version, same_game, compatible, error }
--   engine.cancel_join()
--   engine.get_connection_status()        -> { state = "idle"|"connecting"|"joined", address, error }
--   engine.get_game_info()                -> { selected, loaded, title, can_play }
--   engine.back_to_title()
--
-- C++ calls:
--   render_multiplayer(W, H)
--   handle_multiplayer_click(mx, my, W, H)
--   handle_multiplayer_scroll(dy)
--   handle_multiplayer_key(key, action, mods)  -> true = handled
--   handle_multiplayer_char(codepoint, char)   -> true = handled
--   on_multiplayer_opened()
-- ---------------------------------------------------------------------------

print("[UI] multiplayer.lua loaded")

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
        background     = { 0.10, 0.10, 0.10, 1.00 },
        list_bg        = { 0.00, 0.00, 0.00, 0.45 },
        row_hover      = { 1.00, 1.00, 1.00, 0.08 },
        row_selected   = { 1.00, 1.00, 1.00, 1.00 },
        row_selected_bg= { 0.00, 0.00, 0.00, 1.00 },
        title          = { 1.00, 1.00, 1.00, 1.00 },
        name           = { 1.00, 1.00, 1.00, 1.00 },
        detail         = { 0.63, 0.63, 0.63, 1.00 },
        warning        = { 1.00, 0.45, 0.40, 1.00 },
        good           = { 0.55, 1.00, 0.55, 1.00 },
        bar_good       = { 0.35, 0.90, 0.35, 1.00 },
        bar_ok         = { 0.95, 0.85, 0.30, 1.00 },
        bar_bad        = { 0.95, 0.35, 0.30, 1.00 },
        bar_off        = { 0.30, 0.30, 0.30, 1.00 },
        tooltip_bg     = { 0.08, 0.02, 0.15, 0.95 },
        tooltip_border = { 0.30, 0.10, 0.60, 1.00 },
        outline        = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover  = { 1.00, 1.00, 1.00, 1.00 },
        button         = { 0.44, 0.44, 0.44, 1.00 },
        button_hover   = { 0.50, 0.55, 0.78, 1.00 },
        button_off     = { 0.27, 0.27, 0.27, 1.00 },
        danger         = { 0.55, 0.20, 0.20, 1.00 },
        danger_hover   = { 0.75, 0.25, 0.25, 1.00 },
        bevel_light    = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark     = { 0.00, 0.00, 0.00, 0.35 },
        text           = { 0.88, 0.88, 0.88, 1.00 },
        text_hover     = { 1.00, 1.00, 0.63, 1.00 },
        text_off       = { 0.63, 0.63, 0.63, 1.00 },
        field_bg       = { 0.00, 0.00, 0.00, 1.00 },
        field_border   = { 0.63, 0.63, 0.63, 1.00 },
        field_focus    = { 1.00, 1.00, 1.00, 1.00 },
    },

    -- Font metrics at scale 1.0, measured from a true-size screenshot of the game:
    --   char_w   average letter width (only used if the engine has no ui.text_width)
    --   char_h   height of capital letters
    --   y_offset where the text baseline sits, as a fraction of char_h below the top
    font = { char_w = 10, char_h = 14, base_scale = 0.5, y_offset = 1.0 },

    list_w = 360, row_h = 38,
    button_w = 100, button_h = 20, button_gap = 4,
    button_pad = 12,          -- buttons grow to fit their text plus this much space
    field_w = 200, field_h = 20,
    max_name = 16, max_server_name = 32, max_address = 64,
    double_click = 0.35,
}

local C = Theme.colors
local KEY_TAB = 258

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    mode = "list",          -- "list", "edit", "delete", "connecting"
    servers = nil,
    selected = nil,
    scroll = 0,
    last_click_row = nil,
    last_click_time = 0,
    player_name = "",
    focus = nil,            -- "name"/"address" on the edit screen
    edit_index = nil,       -- nil = adding a new server
    edit_name = "",
    edit_address = "",
    message = nil,          -- error line
    game = nil,
    selected_lan = nil,     -- address of the selected LAN game (instead of a saved server)
    lan_pinged = {},        -- LAN games we've asked for their icon / ping
}

-- ===========================================================================
-- HELPERS
-- ===========================================================================

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function inside(mx, my, x, y, w, h) return mx >= x and mx < x + w and my >= y and my < y + h end

local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end

-- Exact width from the engine when it can measure text, otherwise an estimate
local function text_width(str, scale)
    if ui.text_width then return ui.text_width(str, scale) end
    return #str * Theme.font.char_w * scale
end
local function text_height(scale)
    if ui.text_height then return ui.text_height(scale) end      -- capital letter height
    return Theme.font.char_h * scale
end

local function text(str, x, y, scale, c, shadow, S)
    y = y + (ui.text_height and ui.text_height(scale) or Theme.font.char_h * scale) * Theme.font.y_offset
    if shadow then
        ui.draw_text(str, x + S, y + S, scale, c[1] * 0.25, c[2] * 0.25, c[3] * 0.25, c[4] or 1)
    end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

local function text_centered(str, cx, y, scale, c, S)
    text(str, cx - text_width(str, scale) / 2, y, scale, c, true, S)
end

local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

local function can_play()
    return state.game == nil or state.game.can_play ~= false
end

local function selected_server()
    return state.servers and state.selected and state.servers[state.selected] or nil
end

local function refresh()
    state.game = engine.get_game_info and engine.get_game_info() or nil
    state.servers = engine.list_servers and engine.list_servers() or {}
    state.player_name = engine.get_player_name and engine.get_player_name() or ""
    if state.selected and state.selected > #state.servers then state.selected = nil end
    if not state.selected and #state.servers > 0 then state.selected = 1 end
end

local function save()
    if engine.save_servers then engine.save_servers(state.servers) end
end

-- Ask every server in the list how it's doing (results show up as they arrive)
local function ping_all()
    if not engine.ping_server then return end
    for _, s in ipairs(state.servers or {}) do engine.ping_server(s.address) end
end

local function server_status(s)
    return engine.get_server_status and engine.get_server_status(s.address) or nil
end

-- ===========================================================================
-- ACTIONS
-- ===========================================================================

local function join(address)
    if not address or address == "" then return end
    local ok, err = engine.join_server(address)
    if ok == false then
        state.message = err
        return
    end
    state.message = nil
    state.mode = "connecting"
end

local function join_selected()
    if state.selected_lan then join(state.selected_lan) return end
    local s = selected_server()
    if s then join(s.address) end
end

local function open_edit(index)
    local s = index and state.servers[index]
    state.mode = "edit"
    state.edit_index = index
    state.edit_name = s and s.name or "My Server"
    state.edit_address = s and s.address or ""
    state.focus = s and "name" or "address"
    state.message = nil
    te.reset("mp:name")         -- carets at the end of the text
    te.reset("mp:address")
end

local function save_edit()
    local name = state.edit_name ~= "" and state.edit_name or "Server"
    local address = state.edit_address:gsub("^%s+", ""):gsub("%s+$", "")
    if address == "" then
        state.message = "Type the server's address first"
        return
    end
    local entry = { name = name, address = address }
    if state.edit_index and state.servers[state.edit_index] then
        state.servers[state.edit_index] = entry
        state.selected = state.edit_index
    else
        table.insert(state.servers, entry)
        state.selected = #state.servers
    end
    save()
    if engine.ping_server then engine.ping_server(address) end
    state.mode = "list"
    state.message = nil
end

local function connect_direct()
    local address = state.edit_address:gsub("^%s+", ""):gsub("%s+$", "")
    if address == "" then
        state.message = "Type the server's address first"
        return
    end
    state.mode = "list"
    join(address)
end

local function confirm_delete()
    if state.selected and state.servers[state.selected] then
        table.remove(state.servers, state.selected)
        save()
    end
    state.selected = nil
    state.mode = "list"
    refresh()
end

-- ===========================================================================
-- WIDGETS
-- ===========================================================================

local function draw_button(b, mx, my, S, fs)
    local hovered = b.enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local fill
    if not b.enabled then fill = C.button_off
    elseif b.danger then fill = hovered and C.danger_hover or C.danger
    else fill = hovered and C.button_hover or C.button end

    -- The game's button picture (like the title screen); plain boxes if the engine has none
    local state = ((not b.enabled) and "disabled") or (hovered and "hover") or "normal"
    if not (ui.draw_button and ui.draw_button(b.x, b.y, b.w, b.h, state, b.danger)) then
        rect(b.x, b.y, b.w, b.h, hovered and C.outline_hover or C.outline)
        rect(b.x + S, b.y + S, b.w - 2 * S, b.h - 2 * S, fill)
        if b.enabled then
            rect(b.x + S, b.y + S, b.w - 2 * S, S, C.bevel_light)
            rect(b.x + S, b.y + 2 * S, S, b.h - 3 * S, C.bevel_light)
            rect(b.x + S, b.y + b.h - 2 * S, b.w - 2 * S, S, C.bevel_dark)
        end
    end
    local tc = (not b.enabled) and C.text_off or (hovered and C.text_hover or C.text)
    text_centered(b.label, b.x + b.w / 2, b.y + (b.h - text_height(fs)) / 2, fs, tc, S)
end

local function draw_field(f, value, focused, placeholder, S, fs, id)
    rect(f.x, f.y, f.w, f.h, focused and C.field_focus or C.field_border)
    rect(f.x + S, f.y + S, f.w - 2 * S, f.h - 2 * S, C.field_bg)

    local ty = f.y + (f.h - text_height(fs)) / 2
    if value == "" and not focused and placeholder then
        text(placeholder, f.x + 4 * S, ty, fs, C.detail, false, S)
        return
    end
    if focused and id then
        -- scrolled so the caret stays in view; the caret where it is (lualib/textedit.lua)
        local shown, caret_x = te.view(id, value, f.w - 8 * S, fs)
        text(shown, f.x + 4 * S, ty, fs, C.name, false, S)
        te.draw_caret(id, f.x + 4 * S + caret_x, ty - S, text_height(fs) + 2 * S, C.name[1], C.name[2], C.name[3], 1)
        return
    end
    local shown = value                                    -- not being typed in: its start
    while #shown > 1 and (ui.text_width and ui.text_width(shown, fs) or #shown * 8 * fs) > f.w - 8 * S do
        shown = shown:sub(1, -2)
    end
    text(shown, f.x + 4 * S, ty, fs, C.name, false, S)
end

local function click_buttons(buttons, mx, my)
    for _, b in ipairs(buttons) do
        if b.enabled and inside(mx, my, b.x, b.y, b.w, b.h) then
            b.action()
            return true
        end
    end
    return false
end

-- Rows of `per_row` equal buttons, centred at the bottom
local function bottom_buttons(W, H, S, rows)
    local bh, gap = Theme.button_h * S, Theme.button_gap * S
    -- Every button is as wide as the longest label needs (so they all line up)
    local fs = Theme.font.base_scale * S
    local bw = Theme.button_w * S
    for _, row in ipairs(rows) do
        for _, b in ipairs(row) do
            bw = math.max(bw, text_width(b.label, fs) + Theme.button_pad * S)
        end
    end
    local out = {}
    local y = H - 10 * S - bh * #rows - gap * (#rows - 1)
    for _, row in ipairs(rows) do
        local total = bw * #row + gap * (#row - 1)
        local x = math.floor((W - total) / 2)
        for _, b in ipairs(row) do
            b.x, b.y, b.w, b.h = x, y, bw, bh
            if b.enabled == nil then b.enabled = true end
            table.insert(out, b)
            x = x + bw + gap
        end
        y = y + bh + gap
    end
    return out, out[1] and out[1].y or H
end

-- ===========================================================================
-- SERVER LIST SCREEN
-- ===========================================================================

local function list_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local has_sel = selected_server() ~= nil
    local can_join = (has_sel or state.selected_lan ~= nil) and can_play()

    local buttons, top = bottom_buttons(W, H, S, {
        {
            { label = "Join Server", enabled = can_join, action = join_selected },
            { label = "Direct Connect", enabled = can_play(), action = function()
                open_edit(nil); state.mode = "direct" end },
            { label = "Add Server", action = function() open_edit(nil) end },
            { label = "My Servers", action = function()
                if server_manager_open then server_manager_open() end end },
        },
        {
            { label = "Edit", enabled = has_sel, action = function() open_edit(state.selected) end },
            { label = "Delete", enabled = has_sel, action = function()
                state.mode = "delete"; state.message = nil end },
            { label = "Refresh", action = ping_all },
            { label = "Back", action = function() engine.back_to_title() end },
        },
    })

    local L = { S = S, fs = fs, buttons = buttons }
    L.titleY = 10 * S
    L.nameY = L.titleY + text_height(fs) + 8 * S
    L.messageY = L.nameY + text_height(fs) + 5 * S
    L.listW = math.min(Theme.list_w * S, W - 20 * S)
    L.listX = math.floor((W - L.listW) / 2)
    L.listY = L.messageY + text_height(fs) + 6 * S
    L.listH = top - 8 * S - L.listY
    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))
    return L
end

-- Cuts text to fit a width, adding "..." if it was too long
local function fit(str, width, scale)
    if text_width(str, scale) <= width then return str end
    local cut = str
    while #cut > 0 and text_width(cut .. "...", scale) > width do cut = cut:sub(1, -2) end
    return #cut > 0 and (cut .. "...") or ""
end

-- ---------------------------------------------------------------------------
-- Coloured descriptions: "{gold}{b}MY SERVER{/b}{n}{gray}line two"
-- ---------------------------------------------------------------------------
local NAMED_COLORS = {
    red = { 1.00, 0.33, 0.33 }, orange = { 1.00, 0.60, 0.20 }, gold = { 1.00, 0.67, 0.00 },
    yellow = { 1.00, 1.00, 0.33 }, green = { 0.33, 1.00, 0.33 }, aqua = { 0.33, 1.00, 1.00 },
    blue = { 0.40, 0.55, 1.00 }, purple = { 0.75, 0.40, 1.00 }, pink = { 1.00, 0.45, 0.80 },
    white = { 1.00, 1.00, 1.00 }, gray = { 0.67, 0.67, 0.67 }, dark_gray = { 0.40, 0.40, 0.40 },
    black = { 0.10, 0.10, 0.10 },
}

-- -> { line1_segments, line2_segments }, each segment = { text, color, bold }
local function parse_description(str, default_color)
    local lines = { {} }
    local color, bold = default_color, false
    local pos = 1
    while pos <= #str do
        local open = str:find("{", pos, true)
        local close = open and str:find("}", open, true)
        local tag = close and str:sub(open + 1, close - 1):lower()
        local known = tag and (NAMED_COLORS[tag] or tag:match("^#%x%x%x%x%x%x$")
                               or tag == "b" or tag == "/b" or tag == "r" or tag == "n")
        local stop = known and (open - 1) or #str
        if not known and open then stop = open end   -- not a tag: keep the "{" as text
        if stop >= pos then
            local cur = lines[#lines]
            cur[#cur + 1] = { text = str:sub(pos, stop), color = color, bold = bold }
        end
        if not known then
            pos = stop + 1
        else
            if NAMED_COLORS[tag] then color = NAMED_COLORS[tag]
            elseif tag:sub(1, 1) == "#" then
                color = { tonumber(tag:sub(2, 3), 16) / 255, tonumber(tag:sub(4, 5), 16) / 255,
                          tonumber(tag:sub(6, 7), 16) / 255 }
            elseif tag == "b" then bold = true
            elseif tag == "/b" then bold = false
            elseif tag == "r" then color, bold = default_color, false
            elseif tag == "n" then
                if #lines >= 2 then break end
                lines[#lines + 1] = {}
            end
            pos = close + 1
        end
    end
    return lines
end

local function segments_width(segs, scale, S)
    local w = 0
    for _, seg in ipairs(segs) do w = w + text_width(seg.text, scale) + (seg.bold and S * 0.5 or 0) end
    return w
end

-- Draws one line of segments centred on cx, cut with "..." if wider than max_w
local function draw_rich_line(segs, cx, y, scale, max_w, S)
    local out, used = {}, 0
    for _, seg in ipairs(segs) do
        local w = text_width(seg.text, scale) + (seg.bold and S * 0.5 or 0)
        if used + w > max_w then
            local room = max_w - used
            local cut = fit(seg.text, room, scale)
            if cut ~= "" then out[#out + 1] = { text = cut, color = seg.color, bold = seg.bold } end
            break
        end
        out[#out + 1] = seg
        used = used + w
    end
    local x = cx - segments_width(out, scale, S) / 2
    for _, seg in ipairs(out) do
        local c = { seg.color[1], seg.color[2], seg.color[3], 1 }
        text(seg.text, x, y, scale, c, true, S)
        if seg.bold then text(seg.text, x + S * 0.5, y, scale, c, false, S) end
        x = x + text_width(seg.text, scale) + (seg.bold and S * 0.5 or 0)
    end
end

-- Shared with server_manager.lua: draws a description ({gold}... {n} ...) as up to two
-- centred lines, the same way the server list shows it.
function mp_draw_description(desc, cx, y, scale, max_w, S)
    local lines = parse_description(desc or "", C.detail)
    if lines[1] then draw_rich_line(lines[1], cx, y, scale, max_w, S) end
    if lines[2] then draw_rich_line(lines[2], cx, y + text_height(scale) + 3 * S, scale, max_w, S) end
end

-- Shared with server_manager.lua: join a server (shows the Connecting screen).
function mp_join_address(address)
    state.mode = "list"
    join(address)
end

-- Five ping bars. Grey while pinging (animated), red when offline.
local function draw_ping_bars(x, y, h, st, S)
    local bars, color = 0, C.bar_off
    if st and st.state == "online" then
        local p = st.ping or 9999
        bars = p < 150 and 5 or p < 300 and 4 or p < 600 and 3 or p < 1000 and 2 or 1
        color = bars >= 4 and C.bar_good or (bars >= 3 and C.bar_ok or C.bar_bad)
    elseif st and st.state == "pinging" then
        bars = math.floor(ui.get_time() * 6) % 6
    end
    for i = 1, 5 do
        local bh = math.floor(h * (0.25 + 0.15 * i))
        local c = (i <= bars) and color or C.bar_off
        if st and st.state == "pinging" and i == bars then c = C.detail end
        if st and st.state == "offline" then c = C.bar_bad end
        rect(x + (i - 1) * 2 * S, y + h - bh, S, bh, c)
    end
end

-- Placeholder icon: a coloured tile with the server's first letter
local function draw_letter_icon(name, x, y, size, S, fs)
    local h = 0
    for i = 1, #name do h = (h * 31 + name:byte(i)) % 360 end
    local hue = h / 60
    local f = 1 - math.abs(hue % 2 - 1)
    local rgb = ({ { 1, f, 0 }, { f, 1, 0 }, { 0, 1, f }, { 0, f, 1 }, { f, 0, 1 }, { 1, 0, f } })[math.floor(hue) + 1]
    local c = { rgb[1] * 0.5 + 0.15, rgb[2] * 0.5 + 0.15, rgb[3] * 0.5 + 0.15, 1 }
    rect(x, y, size, size, { c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, 1 })
    rect(x + S, y + S, size - 2 * S, size - 2 * S, c)
    local letter = (name:match("%w") or "?"):upper()
    local ls = fs * 1.6
    text(letter, x + (size - text_width(letter, ls)) / 2, y + (size - text_height(ls)) / 2, ls, C.name, true, S)
end

-- One server row (saved or LAN). Returns tooltip info when the player count is hovered.
local function draw_server_row(entry, rx, ry, rw, rh, mx, my, L, selected)
    local S, fs = L.S, L.fs
    local small = fs * 0.85
    local st = engine.get_server_status and engine.get_server_status(entry.address) or nil
    local tooltip = nil

    if selected then
        rect(rx, ry, rw, rh, C.row_selected)
        rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_selected_bg)
    elseif inside(mx, my, rx, ry, rw, rh) then
        rect(rx, ry, rw, rh, C.row_hover)
    end

    -- Icon
    local isize = rh - 4 * S
    local ix, iy = rx + 2 * S, ry + 2 * S
    if not (ui.draw_server_icon and ui.draw_server_icon(entry.address, ix, iy, isize)) then
        draw_letter_icon(entry.name, ix, iy, isize, S, fs)
    end

    -- Top line: name on the left, players + ping bars on the right
    local left = ix + isize + 5 * S
    local right = rx + rw - 5 * S
    local lineH = text_height(fs)
    local smallH = text_height(small)
    local gap = 3 * S
    local top = ry + math.floor((rh - (lineH + 2 * (smallH + gap))) / 2)

    local barsW = 10 * S
    draw_ping_bars(right - barsW, top, lineH, st, S)
    local countW = 0
    local players = st and st.state == "online" and st.players or entry.players
    local maxp = st and st.state == "online" and st.max_players or entry.max_players
    if players and maxp then
        local count = players .. "/" .. maxp
        countW = text_width(count, fs)
        local cx = right - barsW - 8 * S - countW
        text(count, cx, top, fs, C.detail, false, S)
        if st and inside(mx, my, cx, top, countW, lineH) and st.names and #st.names > 0 then
            tooltip = { names = st.names, more = st.players - #st.names }
        end
    end
    local nameW = right - barsW - 8 * S - countW - 8 * S - left
    text(fit(entry.name, nameW, fs), left, top, fs, C.name, true, S)

    -- Two description lines, centred in the space right of the icon
    local descW = right - left
    local cx = left + descW / 2
    local y1 = top + lineH + gap
    local y2 = y1 + smallH + gap
    if st and st.state == "offline" then
        text_centered(st.error ~= "" and st.error or "Can't connect to server", cx, y1, small, C.warning, S)
    elseif st and st.state == "online" and not st.compatible then
        text_centered("Different version: " .. (st.version or "?"), cx, y1, small, C.warning, S)
    elseif st and st.state == "online" and not st.same_game then
        -- The server runs another game made with the engine
        local title = st.game_title or st.game or "?"
        local hint = st.game_installed and "select it on the title screen to join"
                                        or "you don't have this game installed"
        text_centered("Plays " .. title .. " - " .. hint, cx, y1, small, C.warning, S)
    elseif st and st.state == "pinging" and not entry.motd then
        text_centered("Pinging...", cx, y1, small, C.detail, S)
    else
        local desc = (st and st.state == "online" and st.motd) or entry.motd or ""
        if desc == "" then desc = "{gray}" .. entry.address end
        local lines = parse_description(desc, C.detail)
        if lines[1] then draw_rich_line(lines[1], cx, y1, small, descW, S) end
        if lines[2] then draw_rich_line(lines[2], cx, y2, small, descW, S) end
    end
    return tooltip
end

-- Everything in the list, top to bottom: saved servers, the LAN heading, LAN games
local function list_entries()
    local entries = {}
    for i, s in ipairs(state.servers or {}) do
        entries[#entries + 1] = { kind = "saved", index = i, name = s.name, address = s.address }
    end
    entries[#entries + 1] = { kind = "lan_header" }
    local lan = engine.get_lan_games and engine.get_lan_games() or {}
    for _, g in ipairs(lan) do
        if not state.lan_pinged[g.address] and engine.ping_server then
            engine.ping_server(g.address)   -- for its icon and ping
            state.lan_pinged[g.address] = true
        end
        entries[#entries + 1] = { kind = "lan", name = g.host .. "'s world", address = g.address,
                                  motd = g.motd, players = g.players, max_players = g.max_players }
    end
    return entries
end

local function render_list(W, H)
    local L = list_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local tooltip = nil
    local small = fs * 0.85

    rect(0, 0, W, H, C.background)
    text_centered("Play Multiplayer", W / 2, L.titleY, fs, C.title, S)
    text_centered("Playing as " .. state.player_name .. "  (change it in Settings > Player)",
                  W / 2, L.nameY, small, C.detail, S)

    local warning = state.message
    if not warning and not can_play() then
        warning = "This game isn't loaded. Restart with it selected to join its servers."
    end
    if warning then
        text_centered(warning, W / 2, L.messageY, small, C.warning, S)
    else
        text_centered("To host: open a world, press Esc, click Host Game", W / 2, L.messageY, small, C.detail, S)
    end

    rect(L.listX, L.listY, L.listW, L.listH, C.list_bg)

    local entries = list_entries()
    state.scroll = clamp(state.scroll, 0, math.max(0, #entries - L.visible))
    for row = 1, L.visible do
        local e = entries[row + state.scroll]
        if not e then break end
        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S

        if e.kind == "lan_header" then
            local lanCount = #entries - #(state.servers or {}) - 1
            local label = lanCount > 0 and "Games on your local network" or "Scanning for games on your local network"
            local cy = ry + rh / 2 - text_height(fs)
            text_centered(label, W / 2, cy, fs, C.name, S)
            if lanCount == 0 then
                -- Three dots, one at a time growing - our own take on the "o o O"
                local t = math.floor(ui.get_time() * 3) % 3
                local d = 3 * S
                for i = 0, 2 do
                    local size = (i == t) and d * 1.6 or d
                    rect(W / 2 + (i - 1) * 8 * S - size / 2, cy + text_height(fs) + 6 * S - size / 2,
                         size, size, (i == t) and C.name or C.detail)
                end
            end
        else
            local selected = (e.kind == "saved" and e.index == state.selected and not state.selected_lan)
                          or (e.kind == "lan" and e.address == state.selected_lan)
            tooltip = draw_server_row(e, rx, ry, rw, rh, mx, my, L, selected) or tooltip
        end
    end

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end

    -- Hovering the player count lists who's online (drawn last, on top)
    if tooltip then
        local lines = {}
        for _, n in ipairs(tooltip.names) do lines[#lines + 1] = n end
        if tooltip.more > 0 then lines[#lines + 1] = "...and " .. tooltip.more .. " more" end
        local w = 0
        for _, l in ipairs(lines) do w = math.max(w, text_width(l, small)) end
        local lh = text_height(small) + 3 * S
        local tw, th = w + 8 * S, #lines * lh + 5 * S
        local tx = math.min(mx + 10 * S, W - tw - 2 * S)
        local ty = math.min(my + 10 * S, H - th - 2 * S)
        rect(tx - S, ty - S, tw + 2 * S, th + 2 * S, C.tooltip_border)
        rect(tx, ty, tw, th, C.tooltip_bg)
        for j, l in ipairs(lines) do
            text(l, tx + 4 * S, ty + 3 * S + (j - 1) * lh, small, C.name, false, S)
        end
    end
end

local function click_list(mx, my, W, H)
    local L = list_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end

    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local entries = list_entries()
        local e = entries[math.floor((my - L.listY) / L.rowH) + 1 + state.scroll]
        if not e or e.kind == "lan_header" then return end
        local key = e.kind == "saved" and ("s" .. e.index) or ("l" .. e.address)
        local now = ui.get_time()
        local double = state.last_click_row == key and (now - state.last_click_time) < Theme.double_click
        if e.kind == "saved" then
            state.selected = e.index
            state.selected_lan = nil
        else
            state.selected_lan = e.address
        end
        state.last_click_row = key
        state.last_click_time = now
        state.message = nil
        if double and can_play() then join_selected() end
    end
end

-- ===========================================================================
-- ADD / EDIT SERVER and DIRECT CONNECT SCREENS
-- ===========================================================================

local function edit_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local direct = state.mode == "direct"
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = direct and "Join Server" or "Done", action = direct and connect_direct or save_edit },
            { label = "Cancel", action = function() state.mode = "list"; state.message = nil end },
        },
    })
    local fw, fh = Theme.field_w * S, Theme.field_h * S
    local fx = math.floor((W - fw) / 2)
    local L = { S = S, fs = fs, fx = fx, buttons = buttons, direct = direct }
    L.titleY = 12 * S
    local y = 46 * S
    if not direct then
        L.nameLabelY = y
        L.nameField = { x = fx, y = y + text_height(fs) + 4 * S, w = fw, h = fh }
        y = L.nameField.y + fh + 14 * S
    end
    L.addrLabelY = y
    L.addrField = { x = fx, y = y + text_height(fs) + 4 * S, w = fw, h = fh }
    L.hintY = L.addrField.y + fh + 4 * S
    L.messageY = L.hintY + text_height(fs) + 6 * S
    return L
end

local function render_edit(W, H)
    local L = edit_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    local title = L.direct and "Direct Connect" or (state.edit_index and "Edit Server Info" or "Add Server")
    text_centered(title, W / 2, L.titleY, fs, C.title, S)

    if L.nameField then
        text("Server Name", L.fx, L.nameLabelY, fs, C.detail, false, S)
        draw_field(L.nameField, state.edit_name, state.focus == "name", nil, S, fs, "mp:name")
    end
    text("Server Address", L.fx, L.addrLabelY, fs, C.detail, false, S)
    draw_field(L.addrField, state.edit_address, state.focus == "address", "e.g. 192.168.1.20", S, fs, "mp:address")
    text("Add :port if it isn't 30000", L.fx, L.hintY, fs * 0.85, C.detail, false, S)
    if state.message then text_centered(state.message, W / 2, L.messageY, fs * 0.85, C.warning, S) end

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_edit(mx, my, W, H)
    local L = edit_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end
    -- A click in a box: typing goes there, with the caret where you clicked
    local f = L.nameField
    if f and inside(mx, my, f.x, f.y, f.w, f.h) then
        state.focus = "name"
        te.focus("mp:name", state.edit_name, mx - (f.x + 4 * L.S), L.fs)
        return
    end
    f = L.addrField
    if inside(mx, my, f.x, f.y, f.w, f.h) then
        state.focus = "address"
        te.focus("mp:address", state.edit_address, mx - (f.x + 4 * L.S), L.fs)
        return
    end
end

-- ===========================================================================
-- DELETE CONFIRMATION
-- ===========================================================================

local function delete_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = "Delete", danger = true, action = confirm_delete },
            { label = "Cancel", action = function() state.mode = "list" end },
        },
    })
    return { S = S, fs = fs, buttons = buttons, titleY = math.floor(H / 2 - 30 * S) }
end

local function render_delete(W, H)
    local L = delete_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local s = selected_server()
    rect(0, 0, W, H, C.background)
    text_centered("Remove this server from the list?", W / 2, L.titleY, fs, C.title, S)
    text_centered("'" .. (s and s.name or "?") .. "' will be removed.", W / 2,
                  L.titleY + text_height(fs) + 10 * S, fs, C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

-- ===========================================================================
-- CONNECTING SCREEN
-- ===========================================================================

local function connecting_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        { { label = "Cancel", action = function()
            engine.cancel_join()
            state.mode = "list"
        end } },
    })
    return { S = S, fs = fs, buttons = buttons }
end

local function render_connecting(W, H)
    local status = engine.get_connection_status()
    if status.state == "idle" then
        -- Failed (or was cancelled): back to the list with the reason
        state.mode = "list"
        state.message = status.error
        render_list(W, H)
        return
    end

    local L = connecting_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    rect(0, 0, W, H, C.background)
    local dots = string.rep(".", math.floor(ui.get_time() * 2) % 4)
    local line = status.state == "joined" and "Joining world" or "Connecting to the server"
    text_centered(line .. dots, W / 2, H / 2 - 20 * S, fs, C.title, S)
    text_centered(status.address or "", W / 2, H / 2, fs * 0.85, C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_multiplayer_opened()
    if server_manager_close then server_manager_close() end
    refresh()
    ping_all()
    state.selected_lan = nil
    state.lan_pinged = {}
    state.scroll = 0
    local status = engine.get_connection_status and engine.get_connection_status() or {}
    if status.state == "connecting" then
        state.mode = "connecting"
    else
        state.mode = "list"
        state.message = status.error   -- e.g. "Lost connection to the server"
    end
    state.focus = nil
end

function render_multiplayer(W, H)
    if not ui or not ui.draw_rect then return end
    if server_manager_is_open and server_manager_is_open() then return render_server_manager(W, H) end
    if not state.servers then refresh() end
    if state.mode == "edit" or state.mode == "direct" then render_edit(W, H)
    elseif state.mode == "delete" then render_delete(W, H)
    elseif state.mode == "connecting" then render_connecting(W, H)
    else render_list(W, H) end
end

function handle_multiplayer_click(mx, my, W, H)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_click(mx, my, W, H) end
    if state.mode == "edit" or state.mode == "direct" then click_edit(mx, my, W, H)
    elseif state.mode == "delete" then click_buttons(delete_layout(W, H).buttons, mx, my)
    elseif state.mode == "connecting" then click_buttons(connecting_layout(W, H).buttons, mx, my)
    else click_list(mx, my, W, H) end
    return true
end

function handle_multiplayer_scroll(dy)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_scroll(dy) end
    if state.mode ~= "list" then return end
    if dy > 0 then state.scroll = state.scroll - 1 elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end
end

function handle_multiplayer_key(key, action, mods)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_key(key, action, mods) end
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT
    if not press then return true end
    local enter = key == ui.keys.ENTER or key == ui.keys.KP_ENTER

    if state.mode == "connecting" then
        if key == ui.keys.ESCAPE then engine.cancel_join(); state.mode = "list" end
        return true
    end
    if state.mode == "delete" then
        if key == ui.keys.ESCAPE then state.mode = "list" end
        return true
    end
    if state.mode == "edit" or state.mode == "direct" then
        if key == ui.keys.ESCAPE then
            state.mode = "list"; state.message = nil
        elseif key == KEY_TAB and state.mode == "edit" then
            state.focus = (state.focus == "name") and "address" or "name"
        elseif enter then
            if state.mode == "direct" then connect_direct() else save_edit() end
        else
            -- Left / Right / Home / End / Backspace / Delete (Ctrl: by word)
            local field = state.focus == "name" and "edit_name" or "edit_address"
            state[field] = te.key("mp:" .. (state.focus == "name" and "name" or "address"), state[field], key, action, mods)
        end
        return true
    end

    -- List screen
    local count = state.servers and #state.servers or 0
    if key == ui.keys.UP and count > 0 then
        state.selected = clamp((state.selected or 1) - 1, 1, count)
        return true
    elseif key == ui.keys.DOWN and count > 0 then
        state.selected = clamp((state.selected or 0) + 1, 1, count)
        return true
    elseif enter then
        if can_play() then join_selected() end
        return true
    elseif key == ui.keys.DELETE and selected_server() then
        state.mode = "delete"
        return true
    end
    return false   -- Escape: C++ goes back to the title screen
end

function handle_multiplayer_char(codepoint, ch)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_char(codepoint, ch) end
    if not ch or ch == "" or codepoint < 32 or codepoint >= 127 then return false end
    if state.mode == "edit" or state.mode == "direct" then
        if state.focus == "name" and state.mode == "edit" then
            state.edit_name = te.insert("mp:name", state.edit_name, ch, Theme.max_server_name)
        else
            if ch ~= " " then
                state.edit_address = te.insert("mp:address", state.edit_address, ch, Theme.max_address)
            end
        end
        return true
    end
    return false
end
