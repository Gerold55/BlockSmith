-- multiplayer.lua - the Multiplayer page: your servers, games on your network, joining
-- ---------------------------------------------------------------------------
-- A card over the title screen's panorama, in the same style as Singleplayer and Settings:
--   header   - "Multiplayer", who you're playing as; Direct Connect, + Add Server
--   tabs     - Servers (the ones you saved) and Local Network (games hosted near you, found
--              by themselves)
--   left     - the list: each server's icon, name, description, players and ping. Pinned
--              servers come first (the pin on a row, or Pin in the details, pins / unpins it)
--   right    - the selected server: icon, address, whether it's online, players, ping,
--              version, its description, who's playing; Join / Pin / Edit / Remove
--   dialogs  - Add / Edit Server, Direct Connect, Remove (to confirm), identity checks and Connecting
--
-- To play together, one player opens a world and clicks "Host Game" in the pause menu.
-- Everyone else finds it under Local Network (same network) or adds its address. Everybody
-- needs the same game selected (and the same mods).
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
--   engine.list_servers()                 -> { { name = "Home", address = "192.168.1.20", pinned = true }, ... }
--   engine.save_servers(list)             -- same shape as list_servers
--   engine.get_player_name()              -> "Steve"
--   engine.join_server(address)           -> true, or false + error   (starts connecting)
--   engine.ping_server(address)           -- checks a server without joining (takes up to ~5 s)
--   engine.get_server_status(address)     -> nil, or { state = "pinging"|"online"|"offline",
--                                             motd, players, max_players, ping, names = {...},
--                                             version, same_game, compatible, error,
--                                             game, game_title, game_installed,
--                                             engine_version, engine_commit, engine_compare = "newer"|"older"|"same"|"" }
--   engine.get_lan_games()                -> { { host, address, motd, players, max_players }, ... }
--   engine.cancel_join()
--   engine.get_connection_status()        -> { state, address, error, trust_pending, identity_changed, fingerprint }
--   engine.accept_server_identity()       -- explicitly trust and pin the displayed first-use key
--   engine.get_game_info()                -> { selected, loaded, title, can_play }
--   engine.get_update_info()              -> { available, required, latest, download, ... } (a newer BlockSmith)
--   engine.open_update_page()             -- its download page, in the player's browser
--   engine.back_to_title()
--   ui.draw_server_icon(address, x, y, size) -> true if the server has an icon
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
-- THEME (shared look with Singleplayer and Settings)
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

local C = {
    backdrop      = { 0.02, 0.03, 0.05, 0.62 },
    dim           = { 0.00, 0.00, 0.00, 0.55 },
    panel         = { 0.09, 0.10, 0.13, 0.93 },
    panel_dark    = { 0.05, 0.06, 0.08, 0.94 },
    edge          = { 1.00, 1.00, 1.00, 0.10 },
    divider       = { 1.00, 1.00, 1.00, 0.07 },
    accent        = { 1.00, 0.78, 0.28, 1.00 },
    accent_hover  = { 1.00, 0.85, 0.42, 1.00 },
    accent_text   = { 0.10, 0.08, 0.04, 1.00 },
    danger        = { 0.86, 0.30, 0.27, 1.00 },
    danger_hover  = { 0.95, 0.38, 0.34, 1.00 },
    title         = { 1.00, 1.00, 1.00, 1.00 },
    text          = { 0.90, 0.91, 0.93, 1.00 },
    detail        = { 0.60, 0.62, 0.67, 1.00 },
    faint         = { 0.42, 0.44, 0.50, 1.00 },
    row_hover     = { 1.00, 1.00, 1.00, 0.04 },
    row_selected  = { 1.00, 1.00, 1.00, 0.08 },
    control       = { 0.13, 0.14, 0.18, 1.00 },
    control_edge  = { 1.00, 1.00, 1.00, 0.16 },
    control_hover = { 1.00, 1.00, 1.00, 0.42 },
    error         = { 1.00, 0.45, 0.40, 1.00 },
    online        = { 0.40, 0.85, 0.45, 1.00 },
    pinging       = { 0.95, 0.80, 0.35, 1.00 },
    offline       = { 0.90, 0.38, 0.34, 1.00 },
    bar_off       = { 1.00, 1.00, 1.00, 0.14 },
}
local FONT = { char_w = 10, char_h = 14, base_scale = 0.5, y_offset = 1.0 }
local KEY_TAB = 258
local DOUBLE_CLICK = 0.35
local MAX_SERVER_NAME, MAX_ADDRESS = 32, 64

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    tab = "saved",          -- "saved" or "lan"
    dialog = nil,           -- nil, "edit", "direct", "delete" or "connecting"
    servers = nil,
    selected = nil,         -- index into servers (Servers tab)
    selected_lan = nil,     -- address (Local Network tab)
    scroll = 0,
    last_click = nil, last_click_time = 0,
    player_name = "",
    focus = nil,            -- "name" / "address" in the Add / Edit / Direct dialogs
    edit_index = nil,       -- nil = adding a new server
    edit_name = "",
    edit_address = "",
    message = nil,          -- a problem to show (in the footer, or in the open dialog)
    game = nil,
    lan_pinged = {},        -- LAN games we've asked for their icon / ping
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
local function box(x, y, w, h, r, c, a)
    rect(x + r, y, w - 2 * r, h, c, a)
    rect(x, y + r, r, h - 2 * r, c, a)
    rect(x + w - r, y + r, r, h - 2 * r, c, a)
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
    return #str * FONT.char_w * scale
end
local function text_height(scale)
    if ui.text_height then return ui.text_height(scale) end
    return FONT.char_h * scale
end
local function text(str, x, y, scale, c, shadow, S)
    y = y + text_height(scale) * FONT.y_offset
    if shadow then ui.draw_text(str, x + S, y + S, scale, 0, 0, 0, (c[4] or 1) * 0.45) end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end
local function text_mid(str, x, y, h, scale, c, shadow, S)
    text(str, x, y + (h - text_height(scale)) / 2, scale, c, shadow, S)
end
local function text_center(str, cx, y, h, scale, c, shadow, S)
    text_mid(str, cx - text_width(str, scale) / 2, y, h, scale, c, shadow, S)
end
local function fit(str, w, scale)
    if text_width(str, scale) <= w then return str end
    while #str > 1 and text_width(str .. "...", scale) > w do str = str:sub(1, -2) end
    return (#str > 1) and (str .. "...") or ""
end
local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

-- Buttons: "primary" (gold), "secondary" (outlined), "danger" (red)
local function button(b, mx, my, S, fs)
    local enabled = b.enabled ~= false
    local hov = enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local kind = b.kind or "secondary"
    local tc
    if kind == "primary" then
        box(b.x, b.y, b.w, b.h, 2 * S, hov and C.accent_hover or C.accent, enabled and 1 or 0.35)
        if enabled then rect(b.x + 2 * S, b.y + b.h - S, b.w - 4 * S, S, { 0, 0, 0, 0.25 }) end
        tc = C.accent_text
    elseif kind == "danger" then
        box(b.x, b.y, b.w, b.h, 2 * S, hov and C.danger_hover or C.danger, enabled and 1 or 0.35)
        tc = C.title
    else
        box(b.x, b.y, b.w, b.h, S, { 1, 1, 1, hov and 0.14 or 0.06 })
        outline(b.x, b.y, b.w, b.h, S, hov and C.control_hover or C.control_edge)
        tc = b.text_color or C.text
    end
    if not enabled then tc = { tc[1], tc[2], tc[3], 0.45 } end
    text_center(b.label, b.x + b.w / 2, b.y, b.h, b.fs or fs, tc, false, S)
end

local function field(f, value, focused, placeholder, id, S, fs)
    box(f.x, f.y, f.w, f.h, S, C.control)
    outline(f.x, f.y, f.w, f.h, S, focused and C.accent or C.control_edge)
    local tx, tw = f.x + 5 * S, f.w - 10 * S
    if focused then
        local shown, caret_x = te.view(id, value, tw, fs)
        text_mid(shown, tx, f.y, f.h, fs, C.title, false, S)
        local th = text_height(fs)
        te.draw_caret(id, tx + caret_x, f.y + (f.h - th) / 2 - S, th + 2 * S, C.accent[1], C.accent[2], C.accent[3], 1)
    elseif value == "" then
        text_mid(placeholder or "", tx, f.y, f.h, fs, C.faint, false, S)
    else
        text_mid(fit(value, tw, fs), tx, f.y, f.h, fs, C.text, false, S)
    end
end

local function card(x, y, w, h, S)
    box(x, y + 3 * S, w, h, 4 * S, { 0, 0, 0, 0.30 })                  -- shadow
    box(x, y, w, h, 3 * S, C.panel)
    outline(x, y, w, h, 3 * S, C.edge)
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

local function seg_width(seg, scale, S) return text_width(seg.text, scale) + (seg.bold and S * 0.5 or 0) end

-- One line of segments, cut with "..." at max_w. align: "left" (x is the left edge) or
-- "center" (x is the middle).
local function draw_rich_line(segs, x, y, scale, max_w, S, align)
    local out, used = {}, 0
    for _, seg in ipairs(segs) do
        local w = seg_width(seg, scale, S)
        if used + w > max_w then
            local cut = fit(seg.text, max_w - used, scale)
            if cut ~= "" then out[#out + 1] = { text = cut, color = seg.color, bold = seg.bold } end
            break
        end
        out[#out + 1] = seg
        used = used + w
    end
    if align == "center" then
        local total = 0
        for _, seg in ipairs(out) do total = total + seg_width(seg, scale, S) end
        x = x - total / 2
    end
    for _, seg in ipairs(out) do
        local c = { seg.color[1], seg.color[2], seg.color[3], 1 }
        text(seg.text, x, y, scale, c, true, S)
        if seg.bold then text(seg.text, x + S * 0.5, y, scale, c, false, S) end
        x = x + seg_width(seg, scale, S)
    end
end

-- Shared with server_manager.lua: draws a description ({gold}... {n} ...) as up to two
-- centred lines.
function mp_draw_description(desc, cx, y, scale, max_w, S)
    local lines = parse_description(desc or "", C.detail)
    if lines[1] then draw_rich_line(lines[1], cx, y, scale, max_w, S, "center") end
    if lines[2] then draw_rich_line(lines[2], cx, y + text_height(scale) + 3 * S, scale, max_w, S, "center") end
end

-- Five signal bars. Animated while pinging, red when offline.
local function draw_ping_bars(x, y, h, st, S)
    local bars, color = 0, C.bar_off
    if st and st.state == "online" then
        local p = st.ping or 9999
        bars = p < 150 and 5 or p < 300 and 4 or p < 600 and 3 or p < 1000 and 2 or 1
        color = bars >= 4 and C.online or (bars >= 3 and C.pinging or C.offline)
    elseif st and st.state == "pinging" then
        bars = math.floor(ui.get_time() * 6) % 6
        color = C.detail
    end
    local bw, gap = math.max(1, math.floor(1.5 * S)), S
    for i = 1, 5 do
        local bh = math.floor(h * (0.2 + 0.16 * i))
        local c = (i <= bars) and color or C.bar_off
        if st and st.state == "offline" then c = { C.offline[1], C.offline[2], C.offline[3], 0.55 } end
        rect(x + (i - 1) * (bw + gap), y + h - bh, bw, bh, c)
    end
    return 5 * bw + 4 * gap
end

-- A server's icon, or a coloured tile with its first letter
local function draw_icon(entry, x, y, size, S, fs)
    if ui.draw_server_icon and entry.address and ui.draw_server_icon(entry.address, x, y, size) then
        outline(x, y, size, size, 0, { 1, 1, 1, 0.12 })
        return
    end
    local name = entry.name or "?"
    local h = 0
    for i = 1, #name do h = (h * 31 + name:byte(i)) % 360 end
    local hue = h / 60
    local f = 1 - math.abs(hue % 2 - 1)
    local rgb = ({ { 1, f, 0 }, { f, 1, 0 }, { 0, 1, f }, { 0, f, 1 }, { f, 0, 1 }, { 1, 0, f } })[math.floor(hue) + 1]
    local c = { rgb[1] * 0.45 + 0.15, rgb[2] * 0.45 + 0.15, rgb[3] * 0.45 + 0.15, 1 }
    box(x, y, size, size, math.max(1, math.floor(size / 16)), c)
    rect(x, y + size - math.max(1, math.floor(size / 10)), size, math.max(1, math.floor(size / 10)), { 0, 0, 0, 0.22 })
    local letter = (name:match("%w") or "?"):upper()
    local ls = fs * size / (28 * S) * 1.4
    text_center(letter, x + size / 2, y, size, ls, C.title, true, S)
end

-- A pin: a round-ish head, a bar and the needle
local function draw_pin(x, y, size, c)
    rect(x + size * 0.28, y, size * 0.44, size * 0.42, c)
    rect(x + size * 0.12, y + size * 0.40, size * 0.76, size * 0.14, c)
    rect(x + size * 0.45, y + size * 0.54, math.max(1, size * 0.1), size * 0.46, c)
end

-- Where a row's pin button sits (bottom right of the row)
local function pin_rect(rx, ry, rw, rh, S)
    local sz = 12 * S
    return rx + rw - 10 * S - sz, ry + rh - 6 * S - sz, sz, sz
end

-- ===========================================================================
-- DATA
-- ===========================================================================

local function can_play()
    return state.game == nil or state.game.can_play ~= false
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

local function ping_all()
    if not engine.ping_server then return end
    for _, s in ipairs(state.servers or {}) do engine.ping_server(s.address) end
    state.lan_pinged = {}
end

local function status_of(address)
    return engine.get_server_status and address and engine.get_server_status(address) or nil
end

local function lan_games()
    local out = {}
    for _, g in ipairs(engine.get_lan_games and engine.get_lan_games() or {}) do
        if not state.lan_pinged[g.address] and engine.ping_server then
            engine.ping_server(g.address)   -- for its icon, ping and who's playing
            state.lan_pinged[g.address] = true
        end
        out[#out + 1] = { kind = "lan", name = (g.host or "Someone") .. "'s world", address = g.address,
                          motd = g.motd, players = g.players, max_players = g.max_players, host = g.host }
    end
    return out
end

-- The list on the current tab
local function entries()
    if state.tab == "lan" then return lan_games() end
    local pinned, rest = {}, {}
    for i, s in ipairs(state.servers or {}) do
        local e = { kind = "saved", index = i, name = s.name, address = s.address, pinned = s.pinned == true }
        if e.pinned then pinned[#pinned + 1] = e else rest[#rest + 1] = e end
    end
    for _, e in ipairs(rest) do pinned[#pinned + 1] = e end
    return pinned
end

local function selected_entry()
    for _, e in ipairs(entries()) do
        if (e.kind == "saved" and e.index == state.selected) or (e.kind == "lan" and e.address == state.selected_lan) then
            return e
        end
    end
    return nil
end

-- Can we join it? -> true, or false + why not
local function joinable(e)
    if not e then return false end
    if not can_play() then return false, "This game isn't loaded. Restart with it selected to join its servers." end
    local st = status_of(e.address)
    if st and st.state == "online" then
        if st.compatible == false then
            -- which way round: the server's BlockSmith against this game's
            if st.engine_compare == "newer" then
                return false, "This server runs a newer BlockSmith (" .. st.engine_version .. "): update your game to join."
            elseif st.engine_compare == "older" then
                return false, "This server runs an older BlockSmith (" .. st.engine_version .. "): it needs updating."
            end
            return false, "This server runs a different version (" .. (st.version or "?") .. ")."
        end
        if st.same_game == false then
            local title = st.game_title or st.game or "another game"
            if st.game_installed then return false, "It plays " .. title .. ": select that game on the title screen to join." end
            return false, "It plays " .. title .. ", which you don't have installed."
        end
    end
    return true
end

-- ===========================================================================
-- ACTIONS
-- ===========================================================================

local function join(address)
    if not address or address == "" then return end
    state.trust_error = nil
    local ok, err = engine.join_server(address)
    if ok == false then state.message = err; return end
    state.message = nil
    state.dialog = "connecting"
end

local function join_selected()
    local e = selected_entry()
    if e and joinable(e) then join(e.address) end
end

-- Shared with server_manager.lua: join a server (shows the Connecting dialog)
function mp_join_address(address)
    state.dialog = nil
    join(address)
end

local function open_edit(index)
    local s = index and state.servers[index]
    state.dialog = "edit"
    state.edit_index = index
    state.edit_name = s and s.name or "My Server"
    state.edit_address = s and s.address or ""
    state.focus = s and "name" or "address"
    state.message = nil
    te.reset("mp:name")
    te.reset("mp:address")
end

local function open_direct()
    state.dialog = "direct"
    state.edit_address = ""
    state.focus = "address"
    state.message = nil
    te.reset("mp:address")
end

local function trimmed_address()
    return (state.edit_address:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function save_edit()
    local address = trimmed_address()
    if address == "" then state.message = "Type the server's address first"; return end
    local entry = { name = state.edit_name ~= "" and state.edit_name or "Server", address = address }
    if state.edit_index and state.servers[state.edit_index] then
        entry.pinned = state.servers[state.edit_index].pinned
        state.servers[state.edit_index] = entry
        state.selected = state.edit_index
    else
        table.insert(state.servers, entry)
        state.selected = #state.servers
    end
    save()
    if engine.ping_server then engine.ping_server(address) end
    state.tab = "saved"
    state.dialog, state.focus, state.message = nil, nil, nil
end

local function connect_direct()
    local address = trimmed_address()
    if address == "" then state.message = "Type the server's address first"; return end
    state.dialog, state.focus = nil, nil
    join(address)
end

-- Keep a LAN game in the Servers list
local function save_lan(e)
    if not e then return end
    for i, s in ipairs(state.servers) do
        if s.address == e.address then state.tab = "saved"; state.selected = i; return end
    end
    table.insert(state.servers, { name = e.name, address = e.address })
    save()
    state.tab = "saved"
    state.selected = #state.servers
end

-- Pin a saved server to the top of the list, or unpin it
local function toggle_pin(index)
    local s = index and state.servers[index]
    if not s then return end
    s.pinned = not s.pinned or nil
    save()
    state.selected = index
end

local function confirm_delete()
    if state.selected and state.servers[state.selected] then
        table.remove(state.servers, state.selected)
        save()
    end
    state.selected = nil
    state.dialog = nil
    refresh()
end

local function cancel_connect()
    engine.cancel_join()
    state.trust_error = nil
    state.dialog = nil
end

-- ===========================================================================
-- LAYOUT
-- ===========================================================================

local function layout(W, H)
    local S = gui_scale(W, H)
    local fs = FONT.base_scale * S
    local L = { S = S, fs = fs }
    local m = 10 * S
    L.cardW = math.floor(math.min(600 * S, W - 2 * m))
    L.cardH = math.floor(math.min(340 * S, H - 2 * m))
    L.cardX = math.floor((W - L.cardW) / 2)
    L.cardY = math.floor((H - L.cardH) / 2)
    L.pad = 14 * S

    -- Header: title and who you are; buttons on the right
    L.headerH = 44 * S
    local bh, by = 18 * S, L.cardY + 13 * S
    local right = L.cardX + L.cardW - L.pad
    L.addBtn = { x = right - 84 * S, y = by, w = 84 * S, h = bh, label = "+ Add Server", kind = "primary",
                 action = function() open_edit(nil) end }
    L.directBtn = { x = L.addBtn.x - 6 * S - 84 * S, y = by, w = 84 * S, h = bh, label = "Direct Connect",
                    fs = fs * 0.85, enabled = can_play(), action = open_direct }

    -- Body: tabs and list on the left, details on the right
    L.bodyY = L.cardY + L.headerH
    L.footerH = 26 * S
    L.bodyH = L.cardH - L.headerH - L.footerH
    L.detailW = math.floor(clamp(L.cardW * 0.36, 150 * S, 220 * S))
    L.detailX = L.cardX + L.cardW - L.detailW
    L.leftX = L.cardX + 6 * S
    L.leftW = L.cardW - L.detailW - 12 * S

    L.tabY = L.bodyY + 6 * S
    L.tabH = 18 * S
    local lanCount = #(engine.get_lan_games and engine.get_lan_games() or {})
    local t1 = "Servers  " .. #(state.servers or {})
    local t2 = "Local Network  " .. lanCount
    local tfs = fs * 0.8
    L.tabs = {
        { id = "saved", label = t1, x = L.leftX + 8 * S, w = text_width(t1, tfs) + 16 * S },
        { id = "lan", label = t2, w = text_width(t2, tfs) + 16 * S, live = lanCount > 0 },
    }
    L.tabs[2].x = L.tabs[1].x + L.tabs[1].w + 4 * S
    L.tabFs = tfs

    L.listX = L.leftX
    L.listW = L.leftW
    L.listY = L.tabY + L.tabH + 6 * S
    L.listH = L.bodyY + L.bodyH - 6 * S - L.listY
    L.rowH = 42 * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))

    -- Details
    local dp = 12 * S
    L.dx = L.detailX + dp
    L.dw = L.detailW - 2 * dp
    L.dy = L.bodyY + dp
    local bottom = L.bodyY + L.bodyH - dp
    L.joinBtn = { x = L.dx, y = bottom - 20 * S - 26 * S, w = L.dw, h = 22 * S, label = "Join Server", kind = "primary",
                  action = join_selected }
    local third = math.floor((L.dw - 8 * S) / 3)
    L.pinBtn = { x = L.dx, y = bottom - 18 * S, w = third, h = 18 * S, label = "Pin", fs = fs * 0.85,
                 action = function() toggle_pin(state.selected) end }
    L.editBtn = { x = L.dx + third + 4 * S, y = bottom - 18 * S, w = third, h = 18 * S, label = "Edit", fs = fs * 0.85,
                  action = function() open_edit(state.selected) end }
    L.delBtn = { x = L.dx + 2 * (third + 4 * S), y = bottom - 18 * S, w = L.dw - 2 * (third + 4 * S), h = 18 * S, label = "Remove",
                 fs = fs * 0.85, text_color = C.error, action = function() state.dialog = "delete"; state.message = nil end }
    L.saveLanBtn = { x = L.dx, y = bottom - 18 * S, w = L.dw, h = 18 * S, label = "Save to Servers", fs = fs * 0.85,
                     action = function() save_lan(selected_entry()) end }

    -- Footer
    L.footerY = L.cardY + L.cardH - L.footerH
    L.backBtn = { x = L.cardX + L.pad, y = L.footerY + 5 * S, w = 70 * S, h = 16 * S, label = "< Back",
                  fs = fs * 0.85, action = function() engine.back_to_title() end }
    L.refreshBtn = { x = L.backBtn.x + L.backBtn.w + 6 * S, y = L.backBtn.y, w = 70 * S, h = 16 * S, label = "Refresh",
                     fs = fs * 0.85, action = ping_all }
    return L
end

local function dialog_box(L, W, H, w, h)
    local D = { w = math.floor(math.min(w * L.S, W - 20 * L.S)), h = math.floor(math.min(h * L.S, H - 20 * L.S)) }
    D.x = math.floor((W - D.w) / 2)
    D.y = math.floor((H - D.h) / 2)
    D.ix, D.iw = D.x + 16 * L.S, D.w - 32 * L.S
    return D
end

local function two_buttons(D, S, left, right)
    local bw = math.floor((D.iw - 8 * S) / 2)
    left.x, left.y, left.w, left.h = D.ix, D.y + D.h - 32 * S, bw, 20 * S
    right.x, right.y, right.w, right.h = D.ix + bw + 8 * S, left.y, D.iw - bw - 8 * S, 20 * S
    D.left, D.right = left, right
    return D
end

local function edit_layout(L, W, H)
    local S = L.S
    local direct = state.dialog == "direct"
    local D = dialog_box(L, W, H, 280, direct and 136 or 176)
    local y = D.y + 40 * S
    if not direct then
        D.nameLabelY = y
        D.nameF = { x = D.ix, y = y + 12 * S, w = D.iw, h = 18 * S }
        y = D.nameF.y + 26 * S
    end
    D.addrLabelY = y
    D.addrF = { x = D.ix, y = y + 12 * S, w = D.iw, h = 18 * S }
    D.hintY = D.addrF.y + 22 * S
    return two_buttons(D, S,
        { label = "Cancel", action = function() state.dialog, state.focus, state.message = nil, nil, nil end },
        { label = direct and "Join" or (state.edit_index and "Save" or "Add Server"), kind = "primary",
          action = direct and connect_direct or save_edit })
end

local function delete_layout(L, W, H)
    local D = dialog_box(L, W, H, 260, 110)
    return two_buttons(D, L.S, { label = "Cancel", action = function() state.dialog = nil end },
                       { label = "Remove", kind = "danger", action = confirm_delete })
end

local function connecting_layout(L, W, H)
    local S = L.S
    local status = engine.get_connection_status()
    if status.trust_pending then
        local D = dialog_box(L, W, H, 320, 188)
        return two_buttons(D, S,
            { label = "Cancel", action = cancel_connect },
            { label = "Trust and Connect", kind = "primary", action = function()
                if not engine.accept_server_identity() then
                    state.trust_error = "Could not save the trusted server key. Check settings.txt and try again."
                else
                    state.trust_error = nil
                end
            end })
    end
    local D = dialog_box(L, W, H, 240, 110)
    D.cancel = { x = D.x + (D.w - 90 * S) / 2, y = D.y + D.h - 32 * S, w = 90 * S, h = 20 * S, label = "Cancel",
                 action = cancel_connect }
    return D
end

local function identity_changed_layout(L, W, H)
    local S = L.S
    local D = dialog_box(L, W, H, 320, 176)
    D.close = { x = D.x + (D.w - 100 * S) / 2, y = D.y + D.h - 32 * S, w = 100 * S, h = 20 * S,
                label = "Close", action = function() state.dialog = nil end }
    return D
end

-- ===========================================================================
-- DRAWING
-- ===========================================================================

local function draw_row(e, rx, ry, rw, rh, hmx, hmy, L, selected)
    local S, fs = L.S, L.fs
    local st = status_of(e.address)
    if selected then
        box(rx, ry, rw, rh, S, C.row_selected)
        rect(rx, ry + 4 * S, 2 * S, rh - 8 * S, C.accent)
    elseif inside(hmx, hmy, rx, ry, rw, rh) then
        box(rx, ry, rw, rh, S, C.row_hover)
    end
    local isz = rh - 8 * S
    draw_icon(e, rx + 8 * S, ry + 4 * S, isz, S, fs)

    local tx = rx + 8 * S + isz + 10 * S
    local right = rx + rw - 10 * S
    -- Top right: players and signal
    local barsH = 8 * S
    draw_ping_bars(right - 5 * 2.5 * S, ry + 8 * S, barsH, st, S)
    local players = st and st.state == "online" and st.players or e.players
    local maxp = st and st.state == "online" and st.max_players or e.max_players
    local cw = 0
    if players and maxp then
        local count = players .. " / " .. maxp
        cw = text_width(count, fs * 0.7) + 8 * S
        text(count, right - 5 * 2.5 * S - cw, ry + 8 * S + barsH - text_height(fs * 0.7), fs * 0.7, C.detail, false, S)
    end
    text(fit(e.name or "?", right - 5 * 2.5 * S - cw - tx - 6 * S, fs), tx, ry + 7 * S, fs, selected and C.title or C.text, false, S)

    -- Second line: the description (its first line), or what's wrong
    local y2 = ry + rh - 8 * S - text_height(fs * 0.68)
    local dw = right - tx
    local small = fs * 0.68
    local ok, why = joinable(e)
    if st and st.state == "offline" then
        text(fit((st.error and st.error ~= "") and st.error or "Can't reach this server", dw, small), tx, y2, small, C.offline, false, S)
    elseif st and st.state == "online" and not ok then
        text(fit(why or "", dw, small), tx, y2, small, C.error, false, S)
    elseif st and st.state == "pinging" and not e.motd then
        text("Checking...", tx, y2, small, C.faint, false, S)
    else
        local desc = (st and st.state == "online" and st.motd) or e.motd or ""
        if desc == "" then desc = "{dark_gray}" .. (e.address or "") end
        local lines = parse_description(desc, C.detail)
        if lines[1] then draw_rich_line(lines[1], tx, y2, small, dw - 16 * S, S, "left") end
    end
    -- The pin: always shown on pinned servers, on hover for the others (click to pin / unpin)
    if e.kind == "saved" then
        local px, py, pw, ph = pin_rect(rx, ry, rw, rh, S)
        local over = inside(hmx, hmy, px - 2 * S, py - 2 * S, pw + 4 * S, ph + 4 * S)
        if over then box(px - 2 * S, py - 2 * S, pw + 4 * S, ph + 4 * S, S, { 1, 1, 1, 0.10 }) end
        if e.pinned then
            draw_pin(px + S, py + S, pw - 2 * S, C.accent)
        elseif over or inside(hmx, hmy, rx, ry, rw, rh) then
            draw_pin(px + S, py + S, pw - 2 * S, over and C.text or C.faint)
        end
    end
end

local function draw_details(L, hmx, hmy)
    local S, fs = L.S, L.fs
    local e = selected_entry()
    if not e then
        local msg = state.tab == "lan" and "Select a game" or "Select a server"
        text_center(msg, L.detailX + L.detailW / 2, L.bodyY + L.bodyH / 2 - 8 * S, 16 * S, fs * 0.8, C.faint, false, S)
        return
    end
    local st = status_of(e.address)
    local isz = 40 * S
    draw_icon(e, L.dx, L.dy, isz, S, fs)
    local nx, nw = L.dx + isz + 8 * S, L.dw - isz - 8 * S
    text(fit(e.name or "?", nw, fs * 1.05), nx, L.dy + 4 * S, fs * 1.05, C.title, true, S)
    text(fit(e.address or "", nw, fs * 0.68), nx, L.dy + 8 * S + text_height(fs * 1.05), fs * 0.68, C.faint, false, S)
    -- status pill
    local label, col = "Unknown", C.faint
    if st and st.state == "online" then label, col = "Online", C.online
    elseif st and st.state == "pinging" then label, col = "Checking", C.pinging
    elseif st and st.state == "offline" then label, col = "Offline", C.offline end
    local pf = fs * 0.62
    local pw, ph = text_width(label, pf) + 14 * S, 10 * S
    local py = L.dy + isz - ph
    box(nx, py, pw, ph, S, col, 0.18)
    outline(nx, py, pw, ph, S, col)
    rect(nx + 4 * S, py + ph / 2 - S, 2 * S, 2 * S, col)
    text_mid(label, nx + 8 * S, py, ph, pf, col, false, S)

    -- Facts
    local y = L.dy + isz + 10 * S
    local small = fs * 0.7
    local function info(k, v, vc)
        text(k, L.dx, y, small, C.faint, false, S)
        local vt = fit(v, L.dw - 60 * S, small)
        text(vt, L.dx + L.dw - text_width(vt, small), y, small, vc or C.text, false, S)
        y = y + text_height(small) + 6 * S
    end
    if st and st.state == "online" then
        info("Players", (st.players or 0) .. " / " .. (st.max_players or "?"))
        info("Ping", (st.ping or "?") .. " ms", (st.ping or 9999) < 150 and C.online or ((st.ping or 9999) < 600 and C.pinging or C.offline))
        if st.game_title or st.game then info("Game", st.game_title or st.game) end
        if st.engine_version and st.engine_version ~= "" then
            local vc = (st.engine_compare == "newer") and C.accent or ((st.engine_compare == "older") and C.faint or C.text)
            info("BlockSmith", st.engine_version, vc)
        end
    elseif e.players and e.max_players then
        info("Players", e.players .. " / " .. e.max_players)
    end

    -- A newer BlockSmith on the server (still joinable): there's an update for this game
    if st and st.state == "online" and st.engine_compare == "newer" and st.compatible ~= false then
        text(fit("Update available: this server runs " .. st.engine_version, L.dw, fs * 0.62), L.dx, y, fs * 0.62, C.accent, false, S)
        y = y + text_height(fs * 0.62) + 6 * S
    end
    -- An older BlockSmith on the server: a newer game can still join it
    if st and st.state == "online" and st.engine_compare == "older" and st.compatible ~= false then
        text(fit("Runs an older BlockSmith (" .. st.engine_version .. "): you can still join", L.dw, fs * 0.62), L.dx, y, fs * 0.62, C.detail, false, S)
        y = y + text_height(fs * 0.62) + 6 * S
    end

    -- Description (two lines), or what's wrong
    local ok, why = joinable(e)
    y = y + 2 * S
    rect(L.dx, y, L.dw, 1, C.divider)
    y = y + 7 * S
    if not ok and why then
        -- (wrapped: these say what to do, so all of it shows - up to 4 lines)
        local line, lines = "", {}
        for word in why:gmatch("%S+") do
            local try = (line == "") and word or (line .. " " .. word)
            if text_width(try, fs * 0.62) > L.dw and line ~= "" then lines[#lines + 1] = line; line = word else line = try end
        end
        if line ~= "" then lines[#lines + 1] = line end
        for i = 1, math.min(4, #lines) do
            text(fit(lines[i], L.dw, fs * 0.62), L.dx, y, fs * 0.62, C.error, false, S)
            y = y + text_height(fs * 0.62) + 3 * S
        end
        y = y + 3 * S
    elseif st and st.state == "offline" then
        text(fit((st.error and st.error ~= "") and st.error or "Can't reach this server right now.", L.dw, fs * 0.62),
             L.dx, y, fs * 0.62, C.offline, false, S)
        y = y + text_height(fs * 0.62) + 6 * S
    else
        local desc = (st and st.state == "online" and st.motd) or e.motd or ""
        if desc ~= "" then
            local lines = parse_description(desc, C.detail)
            for i = 1, math.min(2, #lines) do
                draw_rich_line(lines[i], L.dx, y, fs * 0.66, L.dw, S, "left")
                y = y + text_height(fs * 0.66) + 4 * S
            end
            y = y + 2 * S
        end
    end
    -- Who's playing
    if st and st.state == "online" and st.names and #st.names > 0 then
        local room = math.floor((L.joinBtn.y - 6 * S - y) / (text_height(fs * 0.62) + 4 * S)) - 1
        if room >= 1 then
            text("PLAYING NOW", L.dx, y, fs * 0.56, C.faint, false, S)
            y = y + text_height(fs * 0.56) + 5 * S
            local more = (st.players or #st.names) - #st.names
            for i, n in ipairs(st.names) do
                if i > room then more = more + (#st.names - room); break end
                rect(L.dx, y + text_height(fs * 0.62) / 2 - S, 2 * S, 2 * S, C.online)
                text(fit(n, L.dw - 8 * S, fs * 0.62), L.dx + 6 * S, y, fs * 0.62, C.text, false, S)
                y = y + text_height(fs * 0.62) + 4 * S
            end
            if more > 0 then text("and " .. more .. " more", L.dx + 6 * S, y, fs * 0.62, C.faint, false, S) end
        end
    end

    L.joinBtn.enabled = ok
    L.joinBtn.label = e.kind == "lan" and "Join Game" or "Join Server"
    button(L.joinBtn, hmx, hmy, S, fs)
    if e.kind == "saved" then
        L.pinBtn.label = e.pinned and "Unpin" or "Pin"
        button(L.pinBtn, hmx, hmy, S, fs)
        button(L.editBtn, hmx, hmy, S, fs)
        button(L.delBtn, hmx, hmy, S, fs)
    else
        button(L.saveLanBtn, hmx, hmy, S, fs)
    end
end

local function draw_dots(cx, cy, S)
    local t = math.floor(ui.get_time() * 3) % 3
    for i = 0, 2 do
        local size = (i == t) and 4 * S or 3 * S
        rect(cx + (i - 1) * 8 * S - size / 2, cy - size / 2, size, size, (i == t) and C.accent or C.faint)
    end
end

local function draw_main(L, W, H, mx, my, interactive)
    local S, fs = L.S, L.fs
    local hmx, hmy = mx, my
    if not interactive then hmx, hmy = -1, -1 end

    card(L.cardX, L.cardY, L.cardW, L.cardH, S)
    rect(L.detailX, L.bodyY, L.detailW - 3 * S, L.bodyH, C.panel_dark)
    rect(L.detailX, L.bodyY, 1, L.bodyH, C.divider)
    rect(L.cardX + S, L.bodyY, L.cardW - 2 * S, 1, C.divider)
    rect(L.cardX + S, L.footerY, L.cardW - 2 * S, 1, C.divider)

    -- Header
    text("Multiplayer", L.cardX + L.pad, L.cardY + 9 * S, fs * 1.2, C.title, true, S)
    local sub = "Playing as " .. (state.player_name ~= "" and state.player_name or "?")
    local game = state.game and state.game.title
    if game and game ~= "" then sub = sub .. "  -  " .. game end
    text(fit(sub, L.directBtn.x - L.cardX - L.pad - 8 * S, fs * 0.68), L.cardX + L.pad,
         L.cardY + 13 * S + text_height(fs * 1.2), fs * 0.68, C.detail, false, S)
    button(L.directBtn, hmx, hmy, S, fs)
    button(L.addBtn, hmx, hmy, S, fs * 0.9)

    -- Tabs
    for _, t in ipairs(L.tabs) do
        local sel = state.tab == t.id
        local hov = inside(hmx, hmy, t.x, L.tabY, t.w, L.tabH)
        if sel then box(t.x, L.tabY, t.w, L.tabH, S, { 1, 1, 1, 0.08 })
        elseif hov then box(t.x, L.tabY, t.w, L.tabH, S, { 1, 1, 1, 0.04 }) end
        text_center(t.label, t.x + t.w / 2, L.tabY, L.tabH, L.tabFs, sel and C.title or C.detail, false, S)
        if sel then rect(t.x + 4 * S, L.tabY + L.tabH - S, t.w - 8 * S, S, C.accent) end
        if t.live then rect(t.x + t.w - 5 * S, L.tabY + 4 * S, 2 * S, 2 * S, C.online) end
    end

    -- List
    local list = entries()
    state.scroll = clamp(state.scroll, 0, math.max(0, #list - L.visible))
    local cx, cy = L.listX + L.listW / 2, L.listY + L.listH / 2
    L.emptyBtn = nil
    if #list == 0 then
        if state.tab == "lan" then
            text_center("Looking for games on your network", cx, cy - 30 * S, 14 * S, fs * 0.95, C.title, true, S)
            draw_dots(cx, cy - 6 * S, S)
            text_center("When someone on your network clicks Host Game,", cx, cy + 6 * S, 10 * S, fs * 0.66, C.detail, false, S)
            text_center("their world shows up here by itself.", cx, cy + 18 * S, 10 * S, fs * 0.66, C.detail, false, S)
        else
            text_center("No servers yet", cx, cy - 30 * S, 14 * S, fs * 1.1, C.title, true, S)
            text_center("Add a server by its address to keep it here.", cx, cy - 12 * S, 12 * S, fs * 0.72, C.detail, false, S)
            L.emptyBtn = { x = cx - 55 * S, y = cy + 8 * S, w = 110 * S, h = 20 * S, label = "Add Server",
                           kind = "primary", action = function() open_edit(nil) end }
            button(L.emptyBtn, hmx, hmy, S, fs)
        end
    end
    for row = 1, L.visible do
        local e = list[row + state.scroll]
        if not e then break end
        local rx, ry = L.listX, L.listY + (row - 1) * L.rowH
        local sel = (e.kind == "saved" and e.index == state.selected) or (e.kind == "lan" and e.address == state.selected_lan)
        draw_row(e, rx, ry, L.listW - 6 * S, L.rowH - 2 * S, hmx, hmy, L, sel)
        local nxt = list[row + state.scroll + 1]
        if e.pinned and nxt and not nxt.pinned and row < L.visible then
            rect(rx + 8 * S, ry + L.rowH - S, L.listW - 22 * S, 1, { C.accent[1], C.accent[2], C.accent[3], 0.25 })
        end
    end
    if #list > L.visible then
        local trackX = L.listX + L.listW - 2 * S
        rect(trackX, L.listY, 2 * S, L.listH, { 1, 1, 1, 0.05 })
        local th = math.max(16 * S, math.floor(L.listH * L.visible / #list))
        local ty = L.listY + math.floor((L.listH - th) * state.scroll / math.max(1, #list - L.visible))
        rect(trackX, ty, 2 * S, th, { 1, 1, 1, 0.35 })
    end

    draw_details(L, hmx, hmy)

    -- Footer
    button(L.backBtn, hmx, hmy, S, fs)
    button(L.refreshBtn, hmx, hmy, S, fs)
    local note, color = "To host: open a world, press Esc, then Host Game.", C.faint
    if not can_play() then note, color = "This game isn't loaded. Restart with it selected to join its servers.", C.error end
    if state.message and not state.dialog then note, color = state.message, C.error end
    local nx = L.refreshBtn.x + L.refreshBtn.w + 12 * S
    -- A newer BlockSmith (the game's own update check): a link on the right of the footer
    state.update_link = nil
    local u = engine.get_update_info and engine.get_update_info() or nil
    local right = L.cardX + L.cardW - L.pad
    if u and (u.available or u.required) then
        local label = (u.required and "Update required: " or "Update available: ") .. (u.latest or "?")
        if u.download and u.download ~= "" then label = label .. "  -  Download" end
        local lf = fs * 0.7
        local lw = text_width(label, lf) + 14 * S
        local lx, ly, lh = right - lw, L.footerY + 5 * S, 16 * S
        local col = u.required and C.error or C.accent
        local hov = interactive and inside(mx, my, lx, ly, lw, lh)
        box(lx, ly, lw, lh, S, col, hov and 0.28 or 0.16)
        outline(lx, ly, lw, lh, S, col)
        text_center(label, lx + lw / 2, ly, lh, lf, col, false, S)
        if u.download and u.download ~= "" then state.update_link = { x = lx, y = ly, w = lw, h = lh } end
        right = lx - 10 * S
    end
    text_mid(fit(note, right - nx, fs * 0.66), nx, L.footerY, L.footerH, fs * 0.66, color, false, S)
end

local function draw_edit(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local D = edit_layout(L, W, H)
    card(D.x, D.y, D.w, D.h, S)
    local title = state.dialog == "direct" and "Direct Connect" or (state.edit_index and "Edit Server" or "Add Server")
    text(title, D.ix, D.y + 14 * S, fs * 1.1, C.title, true, S)
    if D.nameF then
        text("Server Name", D.ix, D.nameLabelY, fs * 0.7, C.detail, false, S)
        field(D.nameF, state.edit_name, state.focus == "name", "", "mp:name", S, fs)
    end
    text("Server Address", D.ix, D.addrLabelY, fs * 0.7, C.detail, false, S)
    field(D.addrF, state.edit_address, state.focus == "address", "e.g. 192.168.1.20", "mp:address", S, fs)
    if state.message then
        text(fit(state.message, D.iw, fs * 0.64), D.ix, D.hintY, fs * 0.64, C.error, false, S)
    else
        text("Add :port at the end if it isn't 30000.", D.ix, D.hintY, fs * 0.64, C.faint, false, S)
    end
    button(D.left, mx, my, S, fs)
    button(D.right, mx, my, S, fs)
end

local function draw_delete(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local D = delete_layout(L, W, H)
    local s = state.selected and state.servers[state.selected]
    card(D.x, D.y, D.w, D.h, S)
    text("Remove this server?", D.ix, D.y + 14 * S, fs * 1.1, C.title, true, S)
    text(fit("'" .. (s and s.name or "?") .. "' will be taken off your list.", D.iw, fs * 0.78), D.ix, D.y + 42 * S, fs * 0.78, C.text, false, S)
    text("You can add it again any time.", D.ix, D.y + 56 * S, fs * 0.7, C.faint, false, S)
    button(D.left, mx, my, S, fs)
    button(D.right, mx, my, S, fs)
end

local function draw_connecting(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local status = engine.get_connection_status()
    local D = connecting_layout(L, W, H)
    card(D.x, D.y, D.w, D.h, S)
    if status.trust_pending then
        text_center("Verify server identity", D.x + D.w / 2, D.y + 12 * S, 16 * S, fs * 1.05, C.title, true, S)
        text_center(fit("First connection. Compare this fingerprint with the server owner through a trusted channel.", D.iw, fs * 0.72),
                    D.x + D.w / 2, D.y + 37 * S, 36 * S, fs * 0.72, C.detail, false, S)
        text_center(fit(status.fingerprint or "", D.iw, fs * 0.92), D.x + D.w / 2, D.y + 82 * S,
                    18 * S, fs * 0.92, C.title, true, S)
        text_center("Do not trust it if the fingerprints differ.", D.x + D.w / 2, D.y + 108 * S,
                    14 * S, fs * 0.68, C.error, false, S)
        if state.trust_error then
            text_center(fit(state.trust_error, D.iw, fs * 0.62), D.x + D.w / 2, D.y + 126 * S,
                        20 * S, fs * 0.62, C.error, false, S)
        end
        button(D.left, mx, my, S, fs)
        button(D.right, mx, my, S, fs)
        return
    end
    local line = status.state == "joined" and "Joining world" or "Connecting"
    text_center(line, D.x + D.w / 2, D.y + 14 * S, 14 * S, fs * 1.05, C.title, true, S)
    text_center(fit(status.address or "", D.iw, fs * 0.7), D.x + D.w / 2, D.y + 32 * S, 10 * S, fs * 0.7, C.detail, false, S)
    -- a bar sweeping left to right
    local bx, by, bw, bh = D.ix, D.y + 52 * S, D.iw, 3 * S
    rect(bx, by, bw, bh, { 1, 1, 1, 0.08 })
    local p = (ui.get_time() * 0.8) % 1
    local sw = bw * 0.3
    local sx = bx + (bw + sw) * p - sw
    local x0, x1 = math.max(bx, sx), math.min(bx + bw, sx + sw)
    rect(x0, by, x1 - x0, bh, C.accent)
    button(D.cancel, mx, my, S, fs)
end

local function draw_identity_changed(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local status = engine.get_connection_status()
    local D = identity_changed_layout(L, W, H)
    card(D.x, D.y, D.w, D.h, S)
    text_center("Server identity changed", D.x + D.w / 2, D.y + 12 * S, 16 * S, fs * 1.05, C.error, true, S)
    text_center(fit("Do not continue unless you verified this new fingerprint with the server owner.", D.iw, fs * 0.72),
                D.x + D.w / 2, D.y + 38 * S, 34 * S, fs * 0.72, C.detail, false, S)
    text_center(fit(status.fingerprint or "", D.iw, fs * 0.92), D.x + D.w / 2, D.y + 82 * S,
                18 * S, fs * 0.92, C.title, true, S)
    text_center(fit("To trust a replacement key, remove this server's saved key from settings.txt, then reconnect.", D.iw, fs * 0.66),
                D.x + D.w / 2, D.y + 108 * S, 32 * S, fs * 0.66, C.detail, false, S)
    button(D.close, mx, my, S, fs)
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_multiplayer_opened()
    if server_manager_close then server_manager_close() end
    refresh()
    ping_all()
    state.selected_lan = nil
    state.scroll = 0
    state.focus = nil
    local status = engine.get_connection_status and engine.get_connection_status() or {}
    if status.state == "connecting" then
        state.dialog = "connecting"
    elseif status.identity_changed then
        state.dialog = "identity_changed"
    else
        state.dialog = nil
        state.message = status.error   -- e.g. "Lost connection to the server"
    end
end

function render_multiplayer(W, H)
    if not ui or not ui.draw_rect then return end
    if server_manager_is_open and server_manager_is_open() then return render_server_manager(W, H) end
    if not state.servers then refresh() end
    if state.dialog == "connecting" then
        local status = engine.get_connection_status()
        if status.state == "idle" then          -- failed or cancelled: back, with the reason
            if status.identity_changed then
                state.dialog = "identity_changed"
            else
                state.dialog = nil
                state.message = status.error
            end
        end
    end
    -- On the network tab, select the first game found
    if state.tab == "lan" and not selected_entry() then
        local list = entries()
        state.selected_lan = list[1] and list[1].address or nil
    end
    local L = layout(W, H)
    local mx, my = ui.get_mouse()
    rect(0, 0, W, H, C.backdrop)
    draw_main(L, W, H, mx, my, state.dialog == nil)
    if state.dialog then
        rect(0, 0, W, H, C.dim)
        if state.dialog == "edit" or state.dialog == "direct" then draw_edit(L, W, H, mx, my)
        elseif state.dialog == "delete" then draw_delete(L, W, H, mx, my)
        elseif state.dialog == "identity_changed" then draw_identity_changed(L, W, H, mx, my)
        elseif state.dialog == "connecting" then draw_connecting(L, W, H, mx, my) end
    end
end

local function click_button(b, mx, my)
    if b and b.enabled ~= false and inside(mx, my, b.x, b.y, b.w, b.h) then b.action(); return true end
    return false
end

function handle_multiplayer_click(mx, my, W, H)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_click(mx, my, W, H) end
    local L = layout(W, H)
    local S = L.S
    if state.dialog == "edit" or state.dialog == "direct" then
        local D = edit_layout(L, W, H)
        if click_button(D.left, mx, my) or click_button(D.right, mx, my) then return true end
        if D.nameF and inside(mx, my, D.nameF.x, D.nameF.y, D.nameF.w, D.nameF.h) then
            state.focus = "name"; te.focus("mp:name", state.edit_name, mx - (D.nameF.x + 5 * S), L.fs)
        elseif inside(mx, my, D.addrF.x, D.addrF.y, D.addrF.w, D.addrF.h) then
            state.focus = "address"; te.focus("mp:address", state.edit_address, mx - (D.addrF.x + 5 * S), L.fs)
        end
        return true
    elseif state.dialog == "delete" then
        local D = delete_layout(L, W, H)
        if not click_button(D.left, mx, my) then click_button(D.right, mx, my) end
        return true
    elseif state.dialog == "connecting" then
        local D = connecting_layout(L, W, H)
        if D.left then
            if not click_button(D.left, mx, my) then click_button(D.right, mx, my) end
        else
            click_button(D.cancel, mx, my)
        end
        return true
    elseif state.dialog == "identity_changed" then
        click_button(identity_changed_layout(L, W, H).close, mx, my)
        return true
    end

    state.message = nil
    local ul = state.update_link
    if ul and inside(mx, my, ul.x, ul.y, ul.w, ul.h) then
        -- the new version's download page, in the player's browser
        if not (engine.open_update_page and engine.open_update_page()) then state.message = "Couldn't open the download page" end
        return true
    end
    for _, b in ipairs({ L.directBtn, L.addBtn, L.backBtn, L.refreshBtn }) do
        if click_button(b, mx, my) then return true end
    end
    for _, t in ipairs(L.tabs) do
        if inside(mx, my, t.x, L.tabY, t.w, L.tabH) then
            if state.tab ~= t.id then state.tab = t.id; state.scroll = 0 end
            return true
        end
    end
    -- the details' buttons (they're drawn with the selected entry's state)
    local e = selected_entry()
    if e then
        L.joinBtn.enabled = joinable(e)
        if click_button(L.joinBtn, mx, my) then return true end
        if e.kind == "saved" then
            if click_button(L.pinBtn, mx, my) or click_button(L.editBtn, mx, my) or click_button(L.delBtn, mx, my) then return true end
        elseif click_button(L.saveLanBtn, mx, my) then return true end
    end
    local list = entries()
    if #list == 0 and state.tab == "saved" then
        local cx, cy = L.listX + L.listW / 2, L.listY + L.listH / 2
        if inside(mx, my, cx - 55 * S, cy + 8 * S, 110 * S, 20 * S) then open_edit(nil) end
        return true
    end
    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local hit = list[math.floor((my - L.listY) / L.rowH) + 1 + state.scroll]
        if hit and hit.kind == "saved" then
            local row = math.floor((my - L.listY) / L.rowH)
            local px, py, pw, ph = pin_rect(L.listX, L.listY + row * L.rowH, L.listW - 6 * S, L.rowH - 2 * S, S)
            if inside(mx, my, px - 2 * S, py - 2 * S, pw + 4 * S, ph + 4 * S) then
                toggle_pin(hit.index)
                return true
            end
        end
        if hit then
            local key = hit.kind == "saved" and ("s" .. hit.index) or ("l" .. hit.address)
            local now = ui.get_time()
            local double = state.last_click == key and (now - state.last_click_time) < DOUBLE_CLICK
            if hit.kind == "saved" then state.selected = hit.index else state.selected_lan = hit.address end
            state.last_click, state.last_click_time = key, now
            if double then join_selected() end
        end
    end
    return true
end

function handle_multiplayer_scroll(dy)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_scroll(dy) end
    if state.dialog then return end
    if dy > 0 then state.scroll = state.scroll - 1 elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end
end

local function move_selection(d)
    local list = entries()
    if #list == 0 then return end
    local i = 0
    for k, e in ipairs(list) do
        if (e.kind == "saved" and e.index == state.selected) or (e.kind == "lan" and e.address == state.selected_lan) then i = k end
    end
    i = clamp(i + d, 1, #list)
    if list[i].kind == "saved" then state.selected = list[i].index else state.selected_lan = list[i].address end
    if i <= state.scroll then state.scroll = i - 1 end
    if i > state.scroll + 4 then state.scroll = i - 4 end
end

function handle_multiplayer_key(key, action, mods)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_key(key, action, mods) end
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT
    if not press then return true end
    local enter = key == ui.keys.ENTER or key == ui.keys.KP_ENTER

    if state.dialog == "connecting" then
        if key == ui.keys.ESCAPE then cancel_connect() end
        return true
    elseif state.dialog == "identity_changed" then
        if key == ui.keys.ESCAPE or enter then state.dialog = nil end
        return true
    end
    if state.dialog == "delete" then
        if key == ui.keys.ESCAPE then state.dialog = nil end   -- (Enter doesn't remove: it needs a click)
        return true
    end
    if state.dialog == "edit" or state.dialog == "direct" then
        if key == ui.keys.ESCAPE then
            state.dialog, state.focus, state.message = nil, nil, nil
        elseif key == KEY_TAB and state.dialog == "edit" then
            state.focus = (state.focus == "name") and "address" or "name"
        elseif enter then
            if state.dialog == "direct" then connect_direct() else save_edit() end
        elseif state.focus then
            local fld = state.focus == "name" and "edit_name" or "edit_address"
            state[fld] = te.key("mp:" .. state.focus, state[fld], key, action, mods)
        end
        return true
    end

    if key == ui.keys.UP then move_selection(-1); return true
    elseif key == ui.keys.DOWN then move_selection(1); return true
    elseif key == KEY_TAB then
        state.tab = (state.tab == "saved") and "lan" or "saved"; state.scroll = 0
        return true
    elseif enter then join_selected(); return true
    elseif key == 80 then                  -- P: pin / unpin
        if state.tab == "saved" then toggle_pin(state.selected) end
        return true
    elseif key == ui.keys.DELETE then
        if state.tab == "saved" and state.selected and state.servers[state.selected] then state.dialog = "delete" end
        return true
    end
    return false   -- Escape: C++ goes back to the title screen
end

function handle_multiplayer_char(codepoint, ch)
    if server_manager_is_open and server_manager_is_open() then return handle_server_manager_char(codepoint, ch) end
    if not ch or ch == "" or codepoint < 32 or codepoint >= 127 then return state.focus ~= nil end
    if state.dialog == "edit" or state.dialog == "direct" then
        if state.focus == "name" and state.dialog == "edit" then
            state.edit_name = te.insert("mp:name", state.edit_name, ch, MAX_SERVER_NAME)
        elseif ch ~= " " then
            state.edit_address = te.insert("mp:address", state.edit_address, ch, MAX_ADDRESS)
        end
        return true
    end
    return false
end
