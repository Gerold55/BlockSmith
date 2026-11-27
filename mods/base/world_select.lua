-- world_select.lua - the Singleplayer page: your worlds, and creating / deleting them
-- ---------------------------------------------------------------------------
-- A card over the title screen's panorama:
--   header   - "Singleplayer", the game and how many worlds; a search box; New World
--   left     - the worlds, most recently played first: a picture of each (its thumbnail),
--              its name, Survival / Creative and when it was last played
--   right    - the selected world: a big picture, its details, and Play / Mods / Delete
--   dialogs  - Create New World (name, seed, game mode), Delete (to confirm) and Mods (which
--              mods from the mods/ folder this world runs)
--
-- Worlds belong to the game they were made with: only the selected game's worlds are listed.
-- Each world runs its game's own mods plus the mods/ folder mods that are on for it: the
-- game's choice (the Mods screen on the title screen) unless the world chose otherwise here.
-- Playing a world whose mods differ from the loaded ones loads its mods first, behind the
-- loading screen - no restart.
--
-- Engine functions used:
--   engine.list_worlds()               -> { { name, seed, game_mode, played, has_thumbnail }, ... }
--                                         newest first ("played" = "2 hours ago")
--   engine.create_world(name, seed, options) -> folder name, or nil + error
--   engine.play_world(name)            -> true, or false + error
--   engine.delete_world(name)          -> true, or false + error
--   engine.get_game_info()             -> { selected, loaded, title, can_play }
--   engine.get_world_mods(name)        -> { game, mods = { { id, title, description, source,
--                                           required, enabled, default, choice, loaded }, ... } }
--   engine.set_world_mod(name, id, on) -> true, or false + error (on = nil: follow the game)
--   engine.take_world_error()
--   engine.back_to_title()
--   ui.world_thumbnail(name)           -> a picture to ui.draw_image, or nil (saves/<world>/thumbnail.png,
--                                         taken from the game view when you pause)
--
-- C++ calls:
--   render_world_select(W, H)
--   handle_world_select_click(mx, my, W, H)
--   handle_world_select_scroll(dy)
--   handle_world_select_key(key, action, mods)  -> true = handled
--   handle_world_select_char(codepoint, char)   -> true = handled
--   on_world_select_opened()
-- ---------------------------------------------------------------------------

print("[UI] world_select.lua loaded")

-- ===========================================================================
-- THEME (shared look with the Settings page)
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
    dim           = { 0.00, 0.00, 0.00, 0.55 },   -- behind a dialog
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
    survival      = { 0.85, 0.36, 0.30, 1.00 },
    creative      = { 0.33, 0.62, 0.95, 1.00 },
    error         = { 1.00, 0.45, 0.40, 1.00 },
    knob          = { 0.96, 0.97, 0.98, 1.00 },
}
local FONT = { char_w = 10, char_h = 14, base_scale = 0.5, y_offset = 1.0 }
local KEY_TAB = 258
local DOUBLE_CLICK = 0.35

local MODE_INFO = {
    survival = { label = "Survival", color = C.survival, text = "Gather what you need. Watch your health and hunger." },
    creative = { label = "Creative", color = C.creative, text = "Every block, unlimited. Fly, and never get hurt." },
}

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    dialog = nil,          -- nil, "create", "delete" or "mods"
    worlds = {},
    selected = nil,        -- the selected world's name
    scroll = 0,
    search = "",
    focus = nil,           -- focused search, name, seed or superflat-layer field
    name = "New World",
    seed = "",
    game_mode = "survival",
    mapgen_style = "normal",
    flat_layers = {
        { block = "grass", depth = "1" },
        { block = "dirt", depth = "3" },
        { block = "bedrock", depth = "1" },
    },
    message = nil,
    game = nil,
    mods = nil,            -- the Mods dialog's list
    mods_scroll = 0,
    last_click = nil, last_click_time = 0,
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
    return str .. "..."
end
local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

-- Buttons: "primary" (gold), "secondary" (outlined), "danger" (red)
local function button(b, mx, my, S, fs)
    local enabled = b.enabled ~= false
    local hov = enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local kind = b.kind or "secondary"
    local tc = C.text
    if kind == "primary" then
        box(b.x, b.y, b.w, b.h, 2 * S, hov and C.accent_hover or C.accent, enabled and 1 or 0.35)
        rect(b.x + 2 * S, b.y + b.h - S, b.w - 4 * S, S, { 0, 0, 0, 0.25 })
        tc = C.accent_text
    elseif kind == "danger" then
        box(b.x, b.y, b.w, b.h, 2 * S, hov and C.danger_hover or C.danger, enabled and 1 or 0.35)
        tc = C.title
    else
        box(b.x, b.y, b.w, b.h, S, { 1, 1, 1, hov and 0.14 or 0.06 })
        outline(b.x, b.y, b.w, b.h, S, hov and C.control_hover or C.control_edge)
        tc = b.text_color or C.text
    end
    if not enabled then tc = { tc[1], tc[2], tc[3], 0.4 } end
    local f = b.fs or fs
    text_center(b.label, b.x + b.w / 2, b.y, b.h, f, tc, false, S)
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

-- A world's picture, or (none yet) a little landscape in the world's own colours
local function hash(s)
    local h = 5381
    for i = 1, #s do h = (h * 33 + s:byte(i)) % 2147483647 end
    return h
end
local function thumbnail(world, x, y, w, h, S)
    local path = world and world.has_thumbnail and ui.world_thumbnail and ui.world_thumbnail(world.name)
    if path and ui.draw_image then
        ui.draw_image(path, x, y, w, h)
    else
        local k = hash(world and world.name or "?")
        local hue = (k % 360) / 360
        local function ch(o) return 0.45 + 0.25 * math.cos(6.2832 * (hue + o)) end
        rect(x, y, w, h * 0.62, { ch(0) * 0.6, ch(0.33) * 0.75, 0.55 + ch(0.67) * 0.4, 1 })        -- sky
        local sun = math.max(2, math.floor(h * 0.16))
        rect(x + w * 0.72, y + h * 0.14, sun, sun, { 1.0, 0.93, 0.65, 1 })                         -- sun
        local cols = 8
        for i = 0, cols - 1 do                                                                    -- hills
            local hh = h * (0.22 + 0.2 * (((k // (i * 7 + 1)) + i * 13) % 5) / 4)
            local cx = x + i * w / cols
            rect(cx, y + h - hh, w / cols + 1, hh, { 0.30, 0.55, 0.26, 1 })
            rect(cx, y + h - hh, w / cols + 1, math.max(1, S), { 0.42, 0.68, 0.34, 1 })
        end
        rect(x, y + h * 0.86, w, h * 0.14, { 0.40, 0.29, 0.20, 1 })                                -- dirt
    end
    outline(x, y, w, h, 0, { 1, 1, 1, 0.12 })
end

-- ===========================================================================
-- DATA
-- ===========================================================================

local function has_game_modes()
    return type(survival) == "table" and survival.set_mode ~= nil
end

local function shown_worlds()
    local q = state.search:lower()
    if q == "" then return state.worlds end
    local out = {}
    for _, w in ipairs(state.worlds) do
        if w.name:lower():find(q, 1, true) then out[#out + 1] = w end
    end
    return out
end

local function selected_world()
    for _, w in ipairs(state.worlds) do if w.name == state.selected then return w end end
    return nil
end

local function refresh_worlds()
    state.game = (engine.get_game_info and engine.get_game_info()) or nil
    state.worlds = (engine.list_worlds and engine.list_worlds()) or {}
    if not selected_world() then state.selected = state.worlds[1] and state.worlds[1].name or nil end
end

-- How many mods from the mods/ folder a world runs (shown in its details)
local function extra_mods(w)
    if not (w and engine.get_world_mods) then return nil end
    local info = engine.get_world_mods(w.name)
    if not info then return nil end
    local n = 0
    for _, m in ipairs(info.mods or {}) do
        if m.source == "Global" and m.enabled and not m.required then n = n + 1 end
    end
    return n
end

-- ===========================================================================
-- ACTIONS
-- ===========================================================================

local function play_selected()
    local w = selected_world()
    if not w then return end
    local ok, err = engine.play_world(w.name)
    if ok == false then state.message = err end
end

local function open_create()
    state.dialog = "create"
    state.name = "New World"
    state.seed = ""
    state.focus = "name"
    te.reset("world:name")
    te.reset("world:seed")
    state.message = nil
    state.game_mode = (has_game_modes() and survival.settings and survival.settings.default_mode) or "survival"
    state.mapgen_style = "normal"
    state.flat_layers = {
        { block = "grass", depth = "1" },
        { block = "dirt", depth = "3" },
        { block = "bedrock", depth = "1" },
    }
end

local function serialize_flat_layers()
    if #state.flat_layers == 0 or #state.flat_layers > 8 then
        return nil, "Superflat needs between 1 and 8 layers."
    end
    local encoded, total = {}, 0
    for i, layer in ipairs(state.flat_layers) do
        local block = layer.block:match("^%s*(.-)%s*$")
        local depth = tonumber(layer.depth)
        if block == "" or not block:match("^[%w_:%-%.]+$") then
            return nil, "Layer " .. i .. " needs a valid block name."
        end
        if not depth or depth < 1 or depth > 256 or depth ~= math.floor(depth) then
            return nil, "Layer " .. i .. " depth must be a whole number from 1 to 256."
        end
        local id = engine.get_block_id and engine.get_block_id(block)
        if not id or id == 0 then
            return nil, "Unknown block in layer " .. i .. ": " .. block
        end
        total = total + depth
        if total > 256 then return nil, "Superflat layers cannot exceed 256 blocks total." end
        encoded[#encoded + 1] = block .. ":" .. depth
    end
    return table.concat(encoded, ","), nil
end

local function create_and_play()
    local options = has_game_modes() and { game_mode = state.game_mode } or {}
    options.mapgen_style = state.mapgen_style
    if state.mapgen_style == "superflat" then
        local layers, err = serialize_flat_layers()
        if not layers then state.message = err; return end
        options.mapgen_layers = layers
    end
    local folder, err = engine.create_world(state.name, state.seed, options)
    if not folder then state.message = err; return end
    state.dialog = nil
    state.focus = nil
    refresh_worlds()
    state.selected = folder
    local ok, perr = engine.play_world(folder)
    if ok == false then state.message = perr end
end

local function confirm_delete()
    local w = selected_world()
    state.dialog = nil
    if not w then return end
    local ok, err = engine.delete_world(w.name)
    state.message = (ok == false) and err or nil
    state.selected = nil
    refresh_worlds()
end

local function refresh_mods()
    local w = selected_world()
    state.mods = (w and engine.get_world_mods and engine.get_world_mods(w.name)) or { mods = {} }
end

local function open_mods()
    if not selected_world() or not engine.get_world_mods then return end
    state.dialog = "mods"
    state.mods_scroll = 0
    state.message = nil
    refresh_mods()
end

-- On for this world, or off: the same as the game's choice clears the world's own
local function toggle_world_mod(m)
    local w = selected_world()
    if not w or m.required or m.source ~= "Global" then return end
    local want = not m.enabled
    local choice = nil                     -- nil: follow the game's choice
    if want ~= m.default then choice = want end
    local ok, err = engine.set_world_mod(w.name, m.id, choice)
    if ok == false then state.message = err end
    refresh_mods()
end

local function reset_world_mods()
    local w = selected_world()
    if not w or not state.mods then return end
    for _, m in ipairs(state.mods.mods or {}) do
        if m.choice ~= nil then engine.set_world_mod(w.name, m.id, nil) end
    end
    refresh_mods()
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

    -- Header: title, then search and New World on the right
    L.headerH = 44 * S
    L.newBtn = { x = L.cardX + L.cardW - L.pad - 84 * S, y = L.cardY + 13 * S, w = 84 * S, h = 18 * S,
                 label = "+ New World", kind = "primary", action = open_create }
    L.searchF = { x = L.newBtn.x - 8 * S - 130 * S, y = L.newBtn.y, w = 130 * S, h = 18 * S }

    -- Body: the list on the left, the details on the right
    L.bodyY = L.cardY + L.headerH
    L.footerH = 26 * S
    L.bodyH = L.cardH - L.headerH - L.footerH
    L.detailW = math.floor(clamp(L.cardW * 0.36, 150 * S, 220 * S))
    L.listX = L.cardX + 6 * S
    L.listW = L.cardW - L.detailW - 12 * S
    L.listY = L.bodyY + 6 * S
    L.listH = L.bodyH - 12 * S
    L.rowH = 42 * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))
    L.detailX = L.cardX + L.cardW - L.detailW

    -- Details: picture, name, info, buttons
    local dp = 12 * S
    L.thumbX = L.detailX + dp
    L.thumbW = L.detailW - 2 * dp
    L.thumbH = math.floor(L.thumbW * 9 / 16)
    L.thumbY = L.bodyY + dp
    local by = L.bodyY + L.bodyH - dp - 20 * S
    L.playBtn = { x = L.thumbX, y = by - 26 * S, w = L.thumbW, h = 22 * S, label = "Play", kind = "primary",
                  action = play_selected }
    local half = math.floor((L.thumbW - 6 * S) / 2)
    L.modsBtn = { x = L.thumbX, y = by, w = half, h = 18 * S, label = "Mods", action = open_mods }
    L.delBtn = { x = L.thumbX + half + 6 * S, y = by, w = L.thumbW - half - 6 * S, h = 18 * S, label = "Delete",
                 text_color = C.error, action = function() state.dialog = "delete"; state.message = nil end }

    -- Footer
    L.footerY = L.cardY + L.cardH - L.footerH
    L.backBtn = { x = L.cardX + L.pad, y = L.footerY + 5 * S, w = 70 * S, h = 16 * S, label = "< Back",
                  fs = fs * 0.85, action = function() engine.back_to_title() end }
    return L
end

-- Dialogs: a smaller card in the middle
local function dialog_layout(L, W, H, w, h)
    local D = { w = math.floor(math.min(w * L.S, W - 20 * L.S)), h = math.floor(math.min(h * L.S, H - 20 * L.S)) }
    D.x = math.floor((W - D.w) / 2)
    D.y = math.floor((H - D.h) / 2)
    return D
end

local function create_layout(L, W, H)
    local S = L.S
    local game_modes = has_game_modes()
    local superflat = state.mapgen_style == "superflat"
    local height
    if superflat then
        height = (game_modes and 288 or 228) + #state.flat_layers * 22
    else
        height = game_modes and 252 or 194
    end
    local D = dialog_layout(L, W, H, 280, height)
    local x, w = D.x + 16 * S, D.w - 32 * S
    D.nameLabelY = D.y + 40 * S
    D.nameF = { x = x, y = D.nameLabelY + 12 * S, w = w, h = 18 * S }
    D.seedLabelY = D.nameF.y + 26 * S
    D.seedF = { x = x, y = D.seedLabelY + 12 * S, w = w, h = 18 * S }
    if has_game_modes() then
        D.modeLabelY = D.seedF.y + 26 * S
        D.modes = { x = x, y = D.modeLabelY + 12 * S, w = w, h = 20 * S }
        D.modeTextY = D.modes.y + 24 * S
    end
    D.mapgenLabelY = D.modeTextY and (D.modeTextY + 6 * S) or (D.seedF.y + 26 * S)
    D.mapgenButtons = {}
    local mapgenW = (w - 4 * S) / 2
    for i, choice in ipairs({ { "normal", "Game default" }, { "superflat", "Superflat" } }) do
        local style, label = choice[1], choice[2]
        D.mapgenButtons[i] = {
            x = x + (i - 1) * (mapgenW + 4 * S), y = D.mapgenLabelY + 12 * S,
            w = mapgenW, h = 20 * S, label = label,
            kind = state.mapgen_style == style and "primary" or "secondary",
            action = function()
                state.mapgen_style = style
                if style ~= "superflat" and state.focus and state.focus:match("^layer_") then
                    state.focus = nil
                end
                state.message = nil
            end,
        }
    end
    if superflat then
        D.layersLabelY = D.mapgenButtons[1].y + 22 * S
        D.layerRows = {}
        for i, layer in ipairs(state.flat_layers) do
            local index = i
            local y = D.layersLabelY + 12 * S + (i - 1) * 20 * S
            local removeW, depthW = 20 * S, 42 * S
            D.layerRows[i] = {
                block = { x = x, y = y, w = w - depthW - removeW - 8 * S, h = 18 * S },
                depth = { x = x + w - depthW - removeW - 4 * S, y = y, w = depthW, h = 18 * S },
                remove = {
                    x = x + w - removeW, y = y, w = removeW, h = 18 * S, label = "X",
                    enabled = #state.flat_layers > 1,
                    action = function()
                        table.remove(state.flat_layers, index)
                        state.focus = nil
                        state.message = nil
                    end,
                },
                value = layer,
            }
        end
        local addY = D.layersLabelY + 12 * S + #state.flat_layers * 20 * S
        D.addLayer = {
            x = x, y = addY, w = 78 * S, h = 18 * S, label = "Add layer",
            enabled = #state.flat_layers < 8,
            action = function()
                if #state.flat_layers < 8 then
                    state.flat_layers[#state.flat_layers + 1] = { block = "dirt", depth = "1" }
                end
                state.message = nil
            end,
        }
    end
    local bw = math.floor((w - 8 * S) / 2)
    D.cancel = { x = x, y = D.y + D.h - 32 * S, w = bw, h = 20 * S, label = "Cancel",
                 action = function() state.dialog = nil; state.focus = nil; state.message = nil end }
    D.ok = { x = x + bw + 8 * S, y = D.cancel.y, w = w - bw - 8 * S, h = 20 * S, label = "Create World", kind = "primary",
             action = create_and_play }
    return D
end

local function delete_layout(L, W, H)
    local S = L.S
    local D = dialog_layout(L, W, H, 260, 120)
    local x, w = D.x + 16 * S, D.w - 32 * S
    local bw = math.floor((w - 8 * S) / 2)
    D.cancel = { x = x, y = D.y + D.h - 32 * S, w = bw, h = 20 * S, label = "Cancel",
                 action = function() state.dialog = nil end }
    D.ok = { x = x + bw + 8 * S, y = D.cancel.y, w = w - bw - 8 * S, h = 20 * S, label = "Delete Forever", kind = "danger",
             action = confirm_delete }
    return D
end

local function mods_layout(L, W, H)
    local S = L.S
    local D = dialog_layout(L, W, H, 380, 290)
    D.listX = D.x + 10 * S
    D.listW = D.w - 20 * S
    D.listY = D.y + 46 * S
    D.listH = D.h - 46 * S - 40 * S
    D.rowH = 30 * S
    D.visible = math.max(1, math.floor(D.listH / D.rowH))
    local any_choice = false
    for _, m in ipairs((state.mods and state.mods.mods) or {}) do if m.choice ~= nil then any_choice = true end end
    local x, w = D.x + 16 * S, D.w - 32 * S
    local bw = math.floor((w - 8 * S) / 2)
    D.reset = { x = x, y = D.y + D.h - 30 * S, w = bw, h = 20 * S, label = "Use Game's Mods",
                enabled = any_choice, action = reset_world_mods }
    D.ok = { x = x + bw + 8 * S, y = D.reset.y, w = w - bw - 8 * S, h = 20 * S, label = "Done", kind = "primary",
             action = function() state.dialog = nil; state.message = nil end }
    return D
end

-- ===========================================================================
-- DRAWING
-- ===========================================================================

local function draw_mode_pill(mode, x, y, S, fs)
    local info = MODE_INFO[mode]
    if not info then return x end
    local f = fs * 0.62
    local w = text_width(info.label, f) + 8 * S
    local h = 10 * S
    box(x, y, w, h, S, info.color, 0.22)
    outline(x, y, w, h, S, info.color)
    text_center(info.label, x + w / 2, y, h, f, { info.color[1] * 0.4 + 0.6, info.color[2] * 0.4 + 0.6, info.color[3] * 0.4 + 0.6, 1 }, false, S)
    return x + w
end

local function card(x, y, w, h, S)
    box(x, y + 3 * S, w, h, 4 * S, { 0, 0, 0, 0.30 })                  -- shadow
    box(x, y, w, h, 3 * S, C.panel)
    outline(x, y, w, h, 3 * S, C.edge)
end

local function draw_main(L, W, H, mx, my, interactive)
    local S, fs = L.S, L.fs
    local hmx, hmy = mx, my
    if not interactive then hmx, hmy = -1, -1 end            -- (a dialog is open: no hovering)

    card(L.cardX, L.cardY, L.cardW, L.cardH, S)
    -- the details side, darker
    rect(L.detailX, L.bodyY, L.detailW - 3 * S, L.bodyH, C.panel_dark)
    rect(L.detailX, L.bodyY, 1, L.bodyH, C.divider)
    rect(L.cardX + S, L.bodyY, L.cardW - 2 * S, 1, C.divider)
    rect(L.cardX + S, L.footerY, L.cardW - 2 * S, 1, C.divider)

    -- Header
    local count = #state.worlds
    local gameTitle = (state.game and state.game.title and state.game.title ~= "") and state.game.title or "This game"
    text("Singleplayer", L.cardX + L.pad, L.cardY + 9 * S, fs * 1.2, C.title, true, S)
    text(gameTitle .. "  -  " .. count .. (count == 1 and " world" or " worlds"), L.cardX + L.pad,
         L.cardY + 13 * S + text_height(fs * 1.2), fs * 0.68, C.detail, false, S)
    field(L.searchF, state.search, state.focus == "search", "Search worlds", "world:search", S, fs * 0.85)
    button(L.newBtn, hmx, hmy, S, fs * 0.9)

    -- The list
    local list = shown_worlds()
    state.scroll = clamp(state.scroll, 0, math.max(0, #list - L.visible))
    if #state.worlds == 0 then
        local cx = L.listX + L.listW / 2
        local cy = L.listY + L.listH / 2
        text_center("No worlds yet", cx, cy - 30 * S, 14 * S, fs * 1.1, C.title, true, S)
        text_center("Create a world to start playing.", cx, cy - 12 * S, 12 * S, fs * 0.75, C.detail, false, S)
        L.emptyBtn = { x = cx - 55 * S, y = cy + 8 * S, w = 110 * S, h = 20 * S, label = "Create World",
                       kind = "primary", action = open_create }
        button(L.emptyBtn, hmx, hmy, S, fs)
    elseif #list == 0 then
        text_center("No worlds match \"" .. fit(state.search, 120 * S, fs * 0.8) .. "\"",
                    L.listX + L.listW / 2, L.listY + L.listH / 2 - 8 * S, 16 * S, fs * 0.8, C.detail, false, S)
    end
    for row = 1, L.visible do
        local w = list[row + state.scroll]
        if not w then break end
        local rx, ry, rw, rh = L.listX, L.listY + (row - 1) * L.rowH, L.listW, L.rowH - 2 * S
        local sel = w.name == state.selected
        if sel then
            box(rx, ry, rw, rh, S, C.row_selected)
            rect(rx, ry + 4 * S, 2 * S, rh - 8 * S, C.accent)
        elseif inside(hmx, hmy, rx, ry, rw, rh) then
            box(rx, ry, rw, rh, S, C.row_hover)
        end
        local th = rh - 8 * S
        local tw = math.floor(th * 16 / 9)
        thumbnail(w, rx + 8 * S, ry + 4 * S, tw, th, S)
        local tx = rx + 8 * S + tw + 10 * S
        text(fit(w.name, rx + rw - tx - 8 * S, fs), tx, ry + 7 * S, fs, sel and C.title or C.text, false, S)
        local px = tx
        if w.game_mode and MODE_INFO[w.game_mode] then
            px = draw_mode_pill(w.game_mode, tx, ry + rh - 16 * S, S, fs) + 6 * S
        end
        text_mid(fit(w.played or "", rx + rw - px - 8 * S, fs * 0.68), px, ry + rh - 16 * S, 10 * S, fs * 0.68, C.detail, false, S)
    end
    -- scroll bar
    if #list > L.visible then
        local trackX = L.listX + L.listW - 2 * S
        rect(trackX, L.listY, 2 * S, L.listH, { 1, 1, 1, 0.05 })
        local th = math.max(16 * S, math.floor(L.listH * L.visible / #list))
        local ty = L.listY + math.floor((L.listH - th) * state.scroll / math.max(1, #list - L.visible))
        rect(trackX, ty, 2 * S, th, { 1, 1, 1, 0.35 })
    end

    -- The selected world's details
    local w = selected_world()
    if w then
        thumbnail(w, L.thumbX, L.thumbY, L.thumbW, L.thumbH, S)
        local y = L.thumbY + L.thumbH + 8 * S
        text(fit(w.name, L.thumbW, fs * 1.05), L.thumbX, y, fs * 1.05, C.title, true, S)
        y = y + text_height(fs * 1.05) + 9 * S
        local function info(k, v)
            text(k, L.thumbX, y, fs * 0.7, C.faint, false, S)
            local vt = fit(v, L.thumbW - 54 * S, fs * 0.7)
            text(vt, L.thumbX + L.thumbW - text_width(vt, fs * 0.7), y, fs * 0.7, C.text, false, S)
            y = y + text_height(fs * 0.7) + 6 * S
        end
        if w.game_mode and MODE_INFO[w.game_mode] then info("Mode", MODE_INFO[w.game_mode].label) end
        info("Last played", w.played or "-")
        info("Seed", tostring(w.seed))
        local n = extra_mods(w)
        if n then info("Extra mods", n == 0 and "None" or tostring(n)) end
        L.playBtn.enabled = true
        button(L.playBtn, hmx, hmy, S, fs)
        button(L.modsBtn, hmx, hmy, S, fs * 0.85)
        button(L.delBtn, hmx, hmy, S, fs * 0.85)
    else
        text_center("Select a world", L.detailX + L.detailW / 2, L.bodyY + L.bodyH / 2 - 8 * S, 16 * S, fs * 0.8, C.faint, false, S)
    end

    -- Footer: back, and a hint or a problem
    button(L.backBtn, hmx, hmy, S, fs * 0.85)
    local note, color = "Double-click a world to play it.", C.faint
    if state.message and not state.dialog then note, color = state.message, C.error end
    text_mid(fit(note, L.cardW - 2 * L.pad - 90 * S, fs * 0.68), L.backBtn.x + L.backBtn.w + 12 * S, L.footerY, L.footerH, fs * 0.68, color, false, S)
end

local function draw_create(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local D = create_layout(L, W, H)
    card(D.x, D.y, D.w, D.h, S)
    text("Create New World", D.x + 16 * S, D.y + 14 * S, fs * 1.1, C.title, true, S)
    text("World Name", D.nameF.x, D.nameLabelY, fs * 0.7, C.detail, false, S)
    field(D.nameF, state.name, state.focus == "name", "", "world:name", S, fs)
    text("Seed", D.seedF.x, D.seedLabelY, fs * 0.7, C.detail, false, S)
    field(D.seedF, state.seed, state.focus == "seed", "Random", "world:seed", S, fs)
    if D.modes then
        text("Game Mode", D.modes.x, D.modeLabelY, fs * 0.7, C.detail, false, S)
        local seg = D.modes.w / 2
        box(D.modes.x, D.modes.y, D.modes.w, D.modes.h, S, C.control)
        outline(D.modes.x, D.modes.y, D.modes.w, D.modes.h, S, C.control_edge)
        for i, id in ipairs({ "survival", "creative" }) do
            local sx = D.modes.x + (i - 1) * seg
            local sel = state.game_mode == id
            local col = MODE_INFO[id].color
            if sel then
                box(sx + 2 * S, D.modes.y + 2 * S, seg - 4 * S, D.modes.h - 4 * S, S, col)
            elseif inside(mx, my, sx, D.modes.y, seg, D.modes.h) then
                box(sx + 2 * S, D.modes.y + 2 * S, seg - 4 * S, D.modes.h - 4 * S, S, { 1, 1, 1, 0.08 })
            end
            text_center(MODE_INFO[id].label, sx + seg / 2, D.modes.y, D.modes.h, fs * 0.85, sel and C.title or C.text, false, S)
        end
        text(fit(MODE_INFO[state.game_mode].text, D.modes.w, fs * 0.66), D.modes.x, D.modeTextY, fs * 0.66, C.detail, false, S)
    end
    text("World Type", D.mapgenButtons[1].x, D.mapgenLabelY, fs * 0.7, C.detail, false, S)
    button(D.mapgenButtons[1], mx, my, S, fs * 0.75)
    button(D.mapgenButtons[2], mx, my, S, fs * 0.75)
    if D.layerRows then
        text("Superflat layers (top to bottom)", D.layerRows[1].block.x, D.layersLabelY,
            fs * 0.68, C.detail, false, S)
        for i, row in ipairs(D.layerRows) do
            local blockId = "world:flat:block:" .. i
            local depthId = "world:flat:depth:" .. i
            field(row.block, row.value.block, state.focus == "layer_block_" .. i, "Block name", blockId, S, fs * 0.68)
            field(row.depth, row.value.depth, state.focus == "layer_depth_" .. i, "Depth", depthId, S, fs * 0.68)
            button(row.remove, mx, my, S, fs * 0.65)
        end
        button(D.addLayer, mx, my, S, fs * 0.65)
    end
    if state.message then
        text(fit(state.message, D.w - 32 * S, fs * 0.66), D.x + 16 * S, D.cancel.y - 14 * S, fs * 0.66, C.error, false, S)
    end
    button(D.cancel, mx, my, S, fs)
    button(D.ok, mx, my, S, fs)
end

local function draw_delete(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local D = delete_layout(L, W, H)
    local w = selected_world()
    card(D.x, D.y, D.w, D.h, S)
    text("Delete this world?", D.x + 16 * S, D.y + 14 * S, fs * 1.1, C.title, true, S)
    text(fit("'" .. (w and w.name or "?") .. "' will be deleted forever.", D.w - 32 * S, fs * 0.8), D.x + 16 * S, D.y + 40 * S, fs * 0.8, C.text, false, S)
    text("This can't be undone.", D.x + 16 * S, D.y + 54 * S, fs * 0.8, C.error, false, S)
    button(D.cancel, mx, my, S, fs)
    button(D.ok, mx, my, S, fs)
end

local function draw_mods(L, W, H, mx, my)
    local S, fs = L.S, L.fs
    local D = mods_layout(L, W, H)
    local w = selected_world()
    local list = (state.mods and state.mods.mods) or {}
    card(D.x, D.y, D.w, D.h, S)
    text("Mods for " .. fit(w and w.name or "?", D.w - 120 * S, fs * 1.1), D.x + 16 * S, D.y + 12 * S, fs * 1.1, C.title, true, S)
    text(state.message or "Switched-on mods run in this world. Changes apply the next time it's opened.",
         D.x + 16 * S, D.y + 14 * S + text_height(fs * 1.1) + 4 * S, fs * 0.62, state.message and C.error or C.detail, false, S)
    state.mods_scroll = clamp(state.mods_scroll, 0, math.max(0, #list - D.visible))
    for row = 1, D.visible do
        local m = list[row + state.mods_scroll]
        if not m then break end
        local rx, ry, rw, rh = D.listX, D.listY + (row - 1) * D.rowH, D.listW, D.rowH - 2 * S
        local locked = m.required or m.source ~= "Global"
        if not locked and inside(mx, my, rx, ry, rw, rh) then box(rx, ry, rw, rh, S, C.row_hover) end
        -- the switch
        local sw, sh = 24 * S, 12 * S
        local sx, sy = rx + rw - sw - 8 * S, ry + math.floor((rh - sh) / 2)
        local on = m.enabled
        box(sx, sy, sw, sh, 3 * S, on and (locked and { 0.6, 0.6, 0.6, 1 } or C.accent) or { 1, 1, 1, 0.14 })
        local k = sh - 4 * S
        box(on and (sx + sw - 2 * S - k) or (sx + 2 * S), sy + 2 * S, k, k, 2 * S, C.knob)
        -- name and what it is
        local tw = sx - rx - 20 * S
        text(fit(m.title or m.id, tw, fs * 0.9), rx + 8 * S, ry + 4 * S, fs * 0.9, locked and C.detail or C.text, false, S)
        local how
        if m.source ~= "Global" then how = "Part of the game"
        elseif m.required then how = "Required"
        elseif m.choice ~= nil then how = "Set for this world"
        else how = "The game's choice" end
        if m.description and m.description ~= "" then how = how .. "  -  " .. m.description end
        text(fit(how, tw, fs * 0.62), rx + 8 * S, ry + rh - 4 * S - text_height(fs * 0.62), fs * 0.62, C.faint, false, S)
    end
    if #list > D.visible then
        local trackX = D.listX + D.listW - 2 * S
        rect(trackX, D.listY, 2 * S, D.listH, { 1, 1, 1, 0.05 })
        local th = math.max(16 * S, math.floor(D.listH * D.visible / #list))
        local ty = D.listY + math.floor((D.listH - th) * state.mods_scroll / math.max(1, #list - D.visible))
        rect(trackX, ty, 2 * S, th, { 1, 1, 1, 0.35 })
    end
    button(D.reset, mx, my, S, fs * 0.85)
    button(D.ok, mx, my, S, fs)
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_world_select_opened()
    state.dialog = nil
    state.scroll = 0
    state.focus = nil
    state.search = ""
    -- Why the last world was closed or couldn't open (e.g. its world generator failed), if so
    state.message = engine.take_world_error and engine.take_world_error() or nil
    refresh_worlds()
end

function render_world_select(W, H)
    if not ui or not ui.draw_rect then return end
    if not state.game then refresh_worlds() end
    local L = layout(W, H)
    local mx, my = ui.get_mouse()
    rect(0, 0, W, H, C.backdrop)
    draw_main(L, W, H, mx, my, state.dialog == nil)
    if state.dialog then
        rect(0, 0, W, H, C.dim)
        if state.dialog == "create" then draw_create(L, W, H, mx, my)
        elseif state.dialog == "delete" then draw_delete(L, W, H, mx, my)
        elseif state.dialog == "mods" then draw_mods(L, W, H, mx, my) end
    end
end

local function click_button(b, mx, my)
    if b and b.enabled ~= false and inside(mx, my, b.x, b.y, b.w, b.h) then b.action(); return true end
    return false
end

local function focused_layer_field()
    if not state.focus then return nil end
    local part, index = state.focus:match("^layer_(%a+)_(%d+)$")
    index = tonumber(index)
    if (part ~= "block" and part ~= "depth") or not index or not state.flat_layers[index] then return nil end
    return part, index
end

function handle_world_select_click(mx, my, W, H)
    local L = layout(W, H)
    local S = L.S
    if state.dialog == "create" then
        local D = create_layout(L, W, H)
        if click_button(D.cancel, mx, my) or click_button(D.ok, mx, my) then return true end
        if inside(mx, my, D.nameF.x, D.nameF.y, D.nameF.w, D.nameF.h) then
            state.focus = "name"; te.focus("world:name", state.name, mx - (D.nameF.x + 5 * S), L.fs); return true
        end
        if inside(mx, my, D.seedF.x, D.seedF.y, D.seedF.w, D.seedF.h) then
            state.focus = "seed"; te.focus("world:seed", state.seed, mx - (D.seedF.x + 5 * S), L.fs); return true
        end
        if click_button(D.mapgenButtons[1], mx, my) or click_button(D.mapgenButtons[2], mx, my) then return true end
        if D.layerRows then
            for i, row in ipairs(D.layerRows) do
                if click_button(row.remove, mx, my) then return true end
                if inside(mx, my, row.block.x, row.block.y, row.block.w, row.block.h) then
                    state.focus = "layer_block_" .. i
                    te.focus("world:flat:block:" .. i, row.value.block,
                        mx - (row.block.x + 5 * S), L.fs * 0.68)
                    return true
                end
                if inside(mx, my, row.depth.x, row.depth.y, row.depth.w, row.depth.h) then
                    state.focus = "layer_depth_" .. i
                    te.focus("world:flat:depth:" .. i, row.value.depth,
                        mx - (row.depth.x + 5 * S), L.fs * 0.68)
                    return true
                end
            end
            if click_button(D.addLayer, mx, my) then return true end
        end
        if D.modes and inside(mx, my, D.modes.x, D.modes.y, D.modes.w, D.modes.h) then
            state.game_mode = (mx < D.modes.x + D.modes.w / 2) and "survival" or "creative"
        end
        return true
    elseif state.dialog == "delete" then
        local D = delete_layout(L, W, H)
        if not click_button(D.cancel, mx, my) then click_button(D.ok, mx, my) end
        return true
    elseif state.dialog == "mods" then
        local D = mods_layout(L, W, H)
        if click_button(D.reset, mx, my) or click_button(D.ok, mx, my) then return true end
        if inside(mx, my, D.listX, D.listY, D.listW, D.listH) then
            local m = state.mods and state.mods.mods and state.mods.mods[math.floor((my - D.listY) / D.rowH) + 1 + state.mods_scroll]
            if m then toggle_world_mod(m) end
        end
        return true
    end

    -- The page
    state.message = nil
    if inside(mx, my, L.searchF.x, L.searchF.y, L.searchF.w, L.searchF.h) then
        state.focus = "search"
        te.focus("world:search", state.search, mx - (L.searchF.x + 5 * S), L.fs * 0.85)
        return true
    end
    state.focus = nil
    if click_button(L.newBtn, mx, my) or click_button(L.backBtn, mx, my) then return true end
    if #state.worlds == 0 then
        local cx, cy = L.listX + L.listW / 2, L.listY + L.listH / 2
        if inside(mx, my, cx - 55 * S, cy + 8 * S, 110 * S, 20 * S) then open_create() end
        return true
    end
    if selected_world() then
        if click_button(L.playBtn, mx, my) or click_button(L.modsBtn, mx, my) or click_button(L.delBtn, mx, my) then return true end
    end
    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local list = shown_worlds()
        local w = list[math.floor((my - L.listY) / L.rowH) + 1 + state.scroll]
        if w then
            local now = ui.get_time()
            local double = state.last_click == w.name and (now - state.last_click_time) < DOUBLE_CLICK
            state.selected = w.name
            state.last_click, state.last_click_time = w.name, now
            if double then play_selected() end
        end
    end
    return true
end

function handle_world_select_scroll(dy)
    local step = (dy > 0) and -1 or ((dy < 0) and 1 or 0)
    if state.dialog == "mods" then
        state.mods_scroll = math.max(0, state.mods_scroll + step)
    elseif not state.dialog then
        state.scroll = math.max(0, state.scroll + step)   -- (the upper bound is kept while drawing)
    end
end

local function move_selection(d)
    local list = shown_worlds()
    if #list == 0 then return end
    local i = 0
    for k, w in ipairs(list) do if w.name == state.selected then i = k end end
    i = clamp(i + d, 1, #list)
    state.selected = list[i].name
    -- keep it in view (the list shows a few at a time)
    if i <= state.scroll then state.scroll = i - 1 end
    if i > state.scroll + 4 then state.scroll = i - 4 end
end

function handle_world_select_key(key, action, mods)
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT
    if state.dialog == "delete" then
        -- Escape cancels. Enter does nothing here on purpose: deleting needs a click.
        if press and key == ui.keys.ESCAPE then state.dialog = nil end
        return true
    end
    if state.dialog == "mods" then
        if press and (key == ui.keys.ESCAPE or key == ui.keys.ENTER) then state.dialog = nil; state.message = nil end
        return true
    end
    if state.dialog == "create" then
        if not press then return true end
        if key == ui.keys.ESCAPE then
            state.dialog = nil; state.focus = nil; state.message = nil
        elseif key == KEY_TAB then
            state.focus = (state.focus == "name") and "seed" or "name"
        elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            create_and_play()
        elseif state.focus == "name" or state.focus == "seed" then
            state[state.focus] = te.key("world:" .. state.focus, state[state.focus], key, action, mods)
        else
            local part, index = focused_layer_field()
            if part then
                local layer = state.flat_layers[index]
                local id = "world:flat:" .. part .. ":" .. index
                layer[part] = te.key(id, layer[part], key, action, mods)
            end
        end
        return true
    end

    if not press then return false end
    if state.focus == "search" then
        if key == ui.keys.ESCAPE or key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            state.focus = nil
        else
            state.search = te.key("world:search", state.search, key, action, mods)
            state.scroll = 0
        end
        return true
    end
    if key == ui.keys.UP then move_selection(-1); return true
    elseif key == ui.keys.DOWN then move_selection(1); return true
    elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then play_selected(); return true
    elseif key == ui.keys.DELETE then
        if selected_world() then state.dialog = "delete" end
        return true
    end
    return false   -- Escape: back to the title screen (C++)
end

function handle_world_select_char(codepoint, ch)
    if not ch or ch == "" or codepoint < 32 or codepoint >= 127 then return state.focus ~= nil end
    if state.dialog == "create" and (state.focus == "name" or state.focus == "seed") then
        state[state.focus] = te.insert("world:" .. state.focus, state[state.focus], ch, 32)
        return true
    end
    if state.dialog == "create" then
        local part, index = focused_layer_field()
        if part then
            local layer = state.flat_layers[index]
            local id = "world:flat:" .. part .. ":" .. index
            layer[part] = te.insert(id, layer[part], ch, part == "block" and 64 or 3)
            return true
        end
    end
    if not state.dialog and state.focus == "search" then
        state.search = te.insert("world:search", state.search, ch, 32)
        state.scroll = 0
        return true
    end
    return false
end