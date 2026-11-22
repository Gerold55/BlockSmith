-- hud.lua: on-screen HUD pieces a server (or your own game) shows to players - timers,
-- scoreboards, titles, boss bars, an action bar line, text and pictures - for minigames and
-- servers. Part of the base mod, so it's in every game and on every server.
--
-- On the server (or in singleplayer / when hosting), any mod can show things to one player,
-- several, or everyone:
--
--   hud.timer("*", "round", 300, { label = "Round ends in", on_done = function() ... end })
--   hud.sidebar("*", "Deaths", { { "Alex", 3 }, { "Sam", 1 } })
--   hud.title("Alex", "You're it!", "Tag someone else", { color = "#ffcc33" })
--   hud.bossbar("*", "boss", "The Ruin King", 0.75, "#cc3333")
--   hud.actionbar("Sam", "+10 points")
--   hud.text("*", "info", "Capture the flag", { position = "top_left" })
--   hud.update("*", "boss", { progress = 0.5 })   hud.hide("*", "boss")   hud.clear("Alex")
--
-- Who: a player's name, a list of names, or "*" for everyone. Things shown to everyone are
-- remembered, so players who join later see them too (with timers where they are now).
-- Players don't need any mod of their own: this file draws it all in their game.
--
-- Moddable:
--   hud.register_type(name, { size = fn, draw = fn })   a new kind of piece, or your own look for
--       a built-in one ("timer", "sidebar", "title", "bar", "actionbar", "text", "image")
--   hud.style                                           colours, sizes and spacing to change
--   hud.show_here(id, piece) / hud.hide_here(id)        pieces only in your own game (client mods)
--   hud.get(id), hud.list()                             what's on screen now
--   hud.set_visible(false)                              hide them all in your game
--   hud.encode(value) / hud.decode(text)                the safe text format pieces travel in
--
-- Everything sent is data: text, numbers, colours and picture names. Nothing a server sends is
-- ever run as code.

hud = hud or {}
local H = hud
H.version = 1
H.ALL = "*"

local CHANNEL = "hud"
local MAX_PIECES = 64            -- per player's game: more than this and the oldest go

-- ===========================================================================
-- The text format (data only: strings, numbers, booleans and tables of them)
-- ===========================================================================
local function encode_into(out, v, depth)
    local t = type(v)
    if t == "string" then
        out[#out + 1] = "s" .. #v .. ":" .. v
    elseif t == "number" then
        if v ~= v or v == math.huge or v == -math.huge then v = 0 end
        out[#out + 1] = "n" .. string.format("%.10g", v) .. ";"
    elseif t == "boolean" then
        out[#out + 1] = v and "T" or "F"
    elseif t == "table" then
        if depth > 8 then error("hud.encode: tables nested too deep") end
        local keys = {}
        for k, val in pairs(v) do
            local kt = type(k)
            local vt = type(val)
            if (kt == "string" or kt == "number") and (vt == "string" or vt == "number" or vt == "boolean" or vt == "table") then
                keys[#keys + 1] = k
            end
        end
        out[#out + 1] = "t" .. #keys .. ":"
        for _, k in ipairs(keys) do
            encode_into(out, k, depth + 1)
            encode_into(out, v[k], depth + 1)
        end
    else
        out[#out + 1] = "F"   -- (functions and the like don't travel)
    end
end

function H.encode(v)
    local out = {}
    encode_into(out, v, 0)
    return table.concat(out)
end

-- Reads what encode wrote. Anything else (damaged, too deep, too big) -> nil, why.
function H.decode(text)
    if type(text) ~= "string" then return nil, "not text" end
    local pos, n, budget = 1, #text, 20000
    local read
    local function number_until(stop)
        local e = text:find(stop, pos, true)
        if not e then error("bad data") end
        local v = tonumber(text:sub(pos, e - 1))
        if not v then error("bad number") end
        pos = e + 1
        return v
    end
    read = function(depth)
        budget = budget - 1
        if budget < 0 or depth > 9 then error("too big") end
        local tag = text:sub(pos, pos)
        pos = pos + 1
        if tag == "s" then
            local len = number_until(":")
            if len < 0 or len % 1 ~= 0 or pos + len - 1 > n then error("bad string") end
            local s = text:sub(pos, pos + len - 1)
            pos = pos + len
            return s
        elseif tag == "n" then
            return number_until(";")
        elseif tag == "T" then return true
        elseif tag == "F" then return false
        elseif tag == "t" then
            local count = number_until(":")
            if count < 0 or count % 1 ~= 0 then error("bad table") end
            local t = {}
            for _ = 1, count do
                local k = read(depth + 1)
                local v = read(depth + 1)
                if type(k) ~= "string" and type(k) ~= "number" then error("bad key") end
                t[k] = v
            end
            return t
        end
        error("bad data")
    end
    local ok, v = pcall(read, 0)
    if not ok then return nil, tostring(v) end
    if pos <= n then return nil, "extra data" end
    return v
end

-- ===========================================================================
-- A clock (seconds since this file loaded; the same idea on the server and in the game)
-- ===========================================================================
local clock = 0
local function now() return clock end

-- ===========================================================================
-- The server side: who sees what
-- ===========================================================================
local everyone = {}          -- id -> piece shown to everyone (kept for players who join later)
local everyone_order = {}    -- ids, oldest first
local per_player = {}        -- name -> { id -> piece }
local timers = {}            -- key -> { done_at, fn, id, target }

local function is_authority()
    return not engine.is_authority or engine.is_authority()
end

local function copy(t)
    if type(t) ~= "table" then return t end
    local c = {}
    for k, v in pairs(t) do c[k] = copy(v) end
    return c
end

-- What a piece looks like on the wire: its functions stay here
local function wire_piece(p)
    local w = {}
    for k, v in pairs(p) do
        if type(v) ~= "function" and k ~= "_shown_at" then w[k] = copy(v) end
    end
    if p.type == "timer" and p._shown_at then
        -- (sent later: the time it has now)
        local passed = now() - p._shown_at
        w.seconds = (p.seconds or 0) + ((p.direction == "up") and passed or -passed)
        if p.direction ~= "up" then w.seconds = math.max(0, w.seconds) end
    end
    if p.duration and p._shown_at then
        w.duration = math.max(0, p.duration - (now() - p._shown_at))
    end
    return w
end

local function send(target, msg)
    local text = H.encode(msg)
    if target == H.ALL or target == nil then
        if engine.send_to_all then engine.send_to_all(CHANNEL, text) end
    elseif type(target) == "table" then
        for _, name in ipairs(target) do
            if engine.send_to_player then engine.send_to_player(name, CHANNEL, text) end
        end
    elseif engine.send_to_player then
        engine.send_to_player(target, CHANNEL, text)
    end
end

local function for_each_name(target, fn)
    if type(target) == "table" then for _, n in ipairs(target) do fn(n) end
    elseif type(target) == "string" and target ~= H.ALL then fn(target) end
end

local function check_id(id)
    if type(id) ~= "string" or id == "" or #id > 64 then error("hud: a piece needs an id (text, up to 64 long)", 3) end
end

-- hud.show(who, id, piece): piece = { type = "timer" | "sidebar" | ..., position = ..., ... }
function H.show(target, id, piece)
    check_id(id)
    if type(piece) ~= "table" or type(piece.type) ~= "string" then error("hud.show: the piece needs a type", 2) end
    if not is_authority() then return false end
    local p = copy(piece)
    p.id = id
    p._shown_at = now()
    if target == H.ALL or target == nil then
        if not everyone[id] then everyone_order[#everyone_order + 1] = id end
        everyone[id] = p
    else
        for_each_name(target, function(name)
            per_player[name] = per_player[name] or {}
            per_player[name][id] = p
        end)
    end
    -- a timer's on_done runs here, on the server
    local key = (type(target) == "string" and target or "list") .. "|" .. id
    if p.type == "timer" and type(piece.on_done) == "function" and (p.direction or "down") ~= "up" then
        -- (a list of names is copied: the caller may change theirs later)
        timers[key] = { done_at = now() + (p.seconds or 0), fn = piece.on_done, id = id,
                        target = type(target) == "table" and copy(target) or target }
    else
        timers[key] = nil
    end
    send(target, { op = "show", id = id, piece = wire_piece(p) })
    return true
end

-- hud.update(who, id, changes): only the fields that changed (a bar's progress, new lines...)
function H.update(target, id, changes)
    check_id(id)
    if type(changes) ~= "table" or not is_authority() then return false end
    local function merge(p)
        for k, v in pairs(changes) do if type(v) ~= "function" then p[k] = copy(v) end end
        if changes.seconds and p.type == "timer" then p._shown_at = now() end
        if changes.duration then p._shown_at = now() end
    end
    if target == H.ALL or target == nil then
        if everyone[id] then merge(everyone[id]) end
    else
        for_each_name(target, function(name)
            local mine = per_player[name]
            if mine and mine[id] then merge(mine[id]) end
        end)
    end
    local wire = {}
    for k, v in pairs(changes) do if type(v) ~= "function" then wire[k] = copy(v) end end
    send(target, { op = "update", id = id, changes = wire })
    return true
end

function H.hide(target, id)
    check_id(id)
    if not is_authority() then return false end
    if target == H.ALL or target == nil then
        everyone[id] = nil
        for i, v in ipairs(everyone_order) do if v == id then table.remove(everyone_order, i) break end end
        for k, t in pairs(timers) do if t.id == id then timers[k] = nil end end
    else
        for_each_name(target, function(name) if per_player[name] then per_player[name][id] = nil end end)
        local key = (type(target) == "string" and target or "list") .. "|" .. id
        timers[key] = nil
    end
    send(target, { op = "hide", id = id })
    return true
end

-- hud.clear(who): everything that was shown to them (to "*": everything shown to everyone)
function H.clear(target)
    if not is_authority() then return false end
    if target == H.ALL or target == nil then
        everyone, everyone_order = {}, {}
        timers = {}
        per_player = {}
    else
        for_each_name(target, function(name)
            per_player[name] = nil
            -- their timers stop too, as hud.hide does: one shown to them alone goes; from one
            -- shown to a list they're taken off it (it goes once nobody is left on it). Timers
            -- of everyone else, and those shown to "*", are kept.
            for key, tm in pairs(timers) do
                if tm.target == name then
                    timers[key] = nil
                elseif type(tm.target) == "table" then
                    for i = #tm.target, 1, -1 do
                        if tm.target[i] == name then table.remove(tm.target, i) end
                    end
                    if #tm.target == 0 then timers[key] = nil end
                end
            end
        end)
    end
    send(target, { op = "clear" })
    return true
end

-- ---- Ready-made pieces (all return true when shown) ----

-- A countdown (or count up: opts.direction = "up"). opts: label, position ("top"), color,
-- on_done (runs on the server when it reaches 0), hide_when_done (seconds to stay at 0:00, or
-- false to stay), format ("clock" 1:05 | "seconds" 65s)
function H.timer(target, id, seconds, opts)
    opts = opts or {}
    local p = copy(opts)
    p.on_done = opts.on_done
    p.type = "timer"
    p.seconds = tonumber(seconds) or 0
    p.position = opts.position or "top"
    return H.show(target, id, p)
end

-- A scoreboard. lines: { "text", ... } or { { "Alex", 3 }, ... } (the number on the right).
-- opts.sort = "desc" / "asc" sorts by the number. Position "right".
function H.sidebar(target, title, lines, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "sidebar"
    p.title = title
    p.lines = copy(lines or {})
    p.position = opts.position or "right"
    return H.show(target, opts.id or "sidebar", p)
end

-- A big title in the middle, with an optional subtitle; it fades in and out.
-- opts: duration (3 s), fade_in (0.3), fade_out (0.8), color, sub_color, id ("title")
function H.title(target, text, subtitle, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "title"
    p.text = text
    p.subtitle = subtitle
    p.duration = opts.duration or 3
    p.position = opts.position or "center"
    return H.show(target, opts.id or "title", p)
end

-- A bar across the top (a boss's health, a capture point): progress 0..1
function H.bossbar(target, id, text, progress, color, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "bar"
    p.text = text
    p.progress = progress or 1
    p.color = color or opts.color
    p.position = opts.position or "top"
    return H.show(target, id, p)
end

-- One line above the hotbar for a moment ("+10 points")
function H.actionbar(target, text, seconds, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "actionbar"
    p.text = text
    p.duration = seconds or 2.5
    p.position = opts.position or "bottom"
    return H.show(target, opts.id or "actionbar", p)
end

-- Text anywhere. opts: position, color, scale, background (true / false)
function H.text(target, id, text, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "text"
    p.text = text
    p.position = opts.position or "top_left"
    return H.show(target, id, p)
end

-- A picture (a file name the players' games have, from a mod they also have). opts: width,
-- height, position
function H.image(target, id, image, opts)
    opts = opts or {}
    local p = copy(opts)
    p.type = "image"
    p.image = image
    p.position = opts.position or "top_left"
    return H.show(target, id, p)
end

-- What the server is showing (for a mod that wants to change one piece): a copy
function H.shown(target, id)
    if target == H.ALL or target == nil then return everyone[id] and wire_piece(everyone[id]) end
    local mine = per_player[target]
    return mine and mine[id] and wire_piece(mine[id]) or nil
end

-- ===========================================================================
-- The players' side: what's on screen
-- ===========================================================================
local pieces = {}        -- id -> piece (with _got = when it arrived, _here = shown by this game)
local visible = true

local function trim()
    local n, oldest, oldest_at = 0, nil, math.huge
    for id, p in pairs(pieces) do
        n = n + 1
        if p._got < oldest_at then oldest, oldest_at = id, p._got end
    end
    if n > MAX_PIECES and oldest then pieces[oldest] = nil end
end

local function receive(msg, here)
    if type(msg) ~= "table" then return end
    local id = msg.id
    if msg.op == "show" and type(id) == "string" and type(msg.piece) == "table" and type(msg.piece.type) == "string" then
        local p = msg.piece
        p.id = id
        p._got = now()
        p._here = here
        pieces[id] = p
        trim()
    elseif msg.op == "update" and type(id) == "string" and type(msg.changes) == "table" then
        local p = pieces[id]
        if not p then return end
        for k, v in pairs(msg.changes) do
            if k ~= "id" and k ~= "_got" and k ~= "_here" then p[k] = v end
        end
        if msg.changes.seconds or msg.changes.duration then p._got = now() end
    elseif msg.op == "hide" and type(id) == "string" then
        pieces[id] = nil
    elseif msg.op == "clear" then
        for k, p in pairs(pieces) do if p._here == here then pieces[k] = nil end end
    end
end

-- Pieces only in this game (a client mod's own HUD): same pieces as hud.show
function H.show_here(id, piece)
    check_id(id)
    if type(piece) ~= "table" or type(piece.type) ~= "string" then error("hud.show_here: the piece needs a type", 2) end
    local p = copy(piece)
    receive({ op = "show", id = id, piece = p }, true)
end
function H.update_here(id, changes) receive({ op = "update", id = id, changes = changes }, true) end
function H.hide_here(id) receive({ op = "hide", id = id }, true) end

function H.get(id) return pieces[id] end
function H.list()
    local out = {}
    for id, p in pairs(pieces) do out[#out + 1] = p end
    table.sort(out, function(a, b) return a.id < b.id end)
    return out
end
function H.set_visible(on) visible = on ~= false end
function H.is_visible() return visible end

-- ===========================================================================
-- Drawing
-- ===========================================================================
H.style = H.style or {
    scale = 0.5,                      -- text size of most pieces
    padding = 6,
    gap = 6,                          -- between pieces stacked at one position
    margin = 10,                      -- from the window's edges
    background = { 0, 0, 0, 0.45 },
    text = { 1, 1, 1 },
    dim = { 0.75, 0.75, 0.75 },
    accent = { 1, 0.85, 0.3 },        -- a sidebar's title, a timer's last seconds
    warning = { 1, 0.35, 0.3 },
    bar = { 0.85, 0.2, 0.25 },
    bar_back = { 0.15, 0.15, 0.15, 0.8 },
    radius = 3,
}
local S = H.style

-- "#rrggbb", { r, g, b [, a] } (0..1 or 0..255) or nil -> r, g, b, a
function H.color(c, fallback)
    if type(c) == "string" then
        local hex = c:match("^#?(%x%x%x%x%x%x)$")
        if hex then
            return tonumber(hex:sub(1, 2), 16) / 255, tonumber(hex:sub(3, 4), 16) / 255, tonumber(hex:sub(5, 6), 16) / 255, 1
        end
    elseif type(c) == "table" and tonumber(c[1]) and tonumber(c[2]) and tonumber(c[3]) then
        local r, g, b, a = tonumber(c[1]), tonumber(c[2]), tonumber(c[3]), tonumber(c[4]) or 1
        if r > 1 or g > 1 or b > 1 then r, g, b = r / 255, g / 255, b / 255 end
        return r, g, b, a
    end
    fallback = fallback or S.text
    return fallback[1], fallback[2], fallback[3], fallback[4] or 1
end
local color = H.color

local function str(v) if v == nil then return "" end return tostring(v) end
local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end

-- Text helpers for renderers: draw_text's y is the baseline, these take the top
function H.text_size(text, scale)
    return ui.text_width(text, scale), ui.text_height(scale)
end
function H.draw_text(text, x, top, scale, r, g, b, a)
    ui.draw_text(text, x, top + ui.text_height(scale), scale, r, g, b, a or 1)
end
function H.panel(x, y, w, h, alpha)
    local bg = S.background
    ui.draw_rounded_rect(x, y, w, h, S.radius, bg[1], bg[2], bg[3], (bg[4] or 0.45) * (alpha or 1))
end

-- How long a timer shows: 65 -> "1:05" (or "65s"); an hour or more -> "1:02:05"
function H.format_time(seconds, format)
    seconds = math.max(0, math.floor((seconds or 0) + 0.5))
    if format == "seconds" then return seconds .. "s" end
    local h, m, s = seconds // 3600, (seconds // 60) % 60, seconds % 60
    if h > 0 then return string.format("%d:%02d:%02d", h, m, s) end
    return string.format("%d:%02d", m, s)
end

-- How faded a piece with a duration is now (0..1), or nil once it's over
local function fade(p, t)
    local age = t - (p._got or t)
    if not p.duration then return 1 end
    if age >= p.duration then return nil end
    local fin, fout = tonumber(p.fade_in) or 0.25, tonumber(p.fade_out) or 0.6
    local a = 1
    if fin > 0 and age < fin then a = age / fin end
    local left = p.duration - age
    if fout > 0 and left < fout then a = math.min(a, left / fout) end
    return clamp(a, 0, 1)
end

H.types = H.types or {}

-- hud.register_type(name, def): def.size(piece, ctx) -> w, h and def.draw(piece, x, y, w, h, ctx).
-- ctx = { W, H, time, alpha (0..1, for fading), align ("left" | "center" | "right") }.
-- Registering a built-in name replaces how it looks.
function H.register_type(name, def)
    if type(name) ~= "string" or type(def) ~= "table" or type(def.size) ~= "function" or type(def.draw) ~= "function" then
        error("hud.register_type(name, { size = function, draw = function })", 2)
    end
    H.types[name] = def
end

-- ---- The built-in pieces ----

local function timer_seconds(p, t)
    local passed = t - (p._got or t)
    local s = tonumber(p.seconds) or 0
    if p.direction == "up" then return s + passed end
    return math.max(0, s - passed)
end

H.register_type("timer", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 1.6
        local tw = ui.text_width(H.format_time(timer_seconds(p, ctx.time) + 0.99, p.format), sc)
        local w, h = tw, ui.text_height(sc)
        if p.label and p.label ~= "" then
            local lw = ui.text_width(str(p.label), S.scale)
            w = math.max(w, lw)
            h = h + ui.text_height(S.scale) + 6
        end
        return w + S.padding * 2 + 8, h + S.padding * 2 + 2
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 1.6
        local left = timer_seconds(p, ctx.time)
        local text = H.format_time(p.direction == "up" and left or math.ceil(left - 0.001), p.format)
        H.panel(x, y, w, h, ctx.alpha)
        local ty = y + S.padding
        if p.label and p.label ~= "" then
            local lw = ui.text_width(str(p.label), S.scale)
            local r, g, b = color(p.label_color, S.dim)
            H.draw_text(str(p.label), x + (w - lw) / 2, ty, S.scale, r, g, b, ctx.alpha)
            ty = ty + ui.text_height(S.scale) + 6
        end
        local r, g, b = color(p.color, S.text)
        if p.direction ~= "up" and left <= (tonumber(p.warn_at) or 10) then
            r, g, b = color(p.warn_color, S.warning)
            if left > 0 and (ctx.time * 2) % 1 < 0.5 then r, g, b = color(p.color, S.text) end   -- (blinks)
        end
        local tw = ui.text_width(text, sc)
        H.draw_text(text, x + (w - tw) / 2, ty, sc, r, g, b, ctx.alpha)
    end,
})

local function sidebar_rows(p)
    local rows = {}
    for _, line in ipairs(type(p.lines) == "table" and p.lines or {}) do
        if type(line) == "table" then rows[#rows + 1] = { str(line[1]), line[2] ~= nil and str(line[2]) or "", tonumber(line[2]), line[3] }
        else rows[#rows + 1] = { str(line), "", nil } end
    end
    if p.sort == "desc" or p.sort == "asc" then
        table.sort(rows, function(a, b)
            local x, y = a[3] or 0, b[3] or 0
            if x == y then return a[1] < b[1] end
            if p.sort == "desc" then return x > y end
            return x < y
        end)
    end
    -- the most rows shown: a whole number >= 0 (0 = none); nothing usable (nil, NaN, infinite) = 15
    local max = tonumber(p.max_lines)
    if not max or max ~= max or max == math.huge or max == -math.huge then max = 15 end
    max = math.max(0, math.floor(max))
    while #rows > max do table.remove(rows) end
    return rows
end

H.register_type("sidebar", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        local rows = sidebar_rows(p)
        local w = p.title and ui.text_width(str(p.title), sc * 1.1) or 0
        for _, r in ipairs(rows) do
            w = math.max(w, ui.text_width(r[1], sc) + (r[2] ~= "" and ui.text_width(r[2], sc) + 18 or 0))
        end
        local lh = ui.text_height(sc) + 7
        local h = #rows * lh + (p.title and (ui.text_height(sc * 1.1) + 10) or 0)
        return math.max(w, 90) + S.padding * 2 + 4, h + S.padding * 2
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        H.panel(x, y, w, h, ctx.alpha)
        local ty = y + S.padding
        if p.title then
            local tw = ui.text_width(str(p.title), sc * 1.1)
            local r, g, b = color(p.title_color, S.accent)
            H.draw_text(str(p.title), x + (w - tw) / 2, ty, sc * 1.1, r, g, b, ctx.alpha)
            ty = ty + ui.text_height(sc * 1.1) + 10
        end
        local lh = ui.text_height(sc) + 7
        for _, row in ipairs(sidebar_rows(p)) do
            local r, g, b = color(row[4] or p.color, S.text)
            H.draw_text(row[1], x + S.padding + 2, ty, sc, r, g, b, ctx.alpha)
            if row[2] ~= "" then
                local vw = ui.text_width(row[2], sc)
                local vr, vg, vb = color(p.value_color, S.warning)
                H.draw_text(row[2], x + w - S.padding - 2 - vw, ty, sc, vr, vg, vb, ctx.alpha)
            end
            ty = ty + lh
        end
    end,
})

H.register_type("title", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 3
        local w = ui.text_width(str(p.text), sc)
        local h = ui.text_height(sc)
        if p.subtitle and p.subtitle ~= "" then
            w = math.max(w, ui.text_width(str(p.subtitle), sc * 0.45))
            h = h + ui.text_height(sc * 0.45) + 14
        end
        return w, h
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 3
        local tw = ui.text_width(str(p.text), sc)
        local r, g, b = color(p.color, S.text)
        H.draw_text(str(p.text), x + (w - tw) / 2 + 2, y + 2, sc, 0, 0, 0, ctx.alpha * 0.6)   -- (shadow)
        H.draw_text(str(p.text), x + (w - tw) / 2, y, sc, r, g, b, ctx.alpha)
        if p.subtitle and p.subtitle ~= "" then
            local ss = sc * 0.45
            local sw = ui.text_width(str(p.subtitle), ss)
            local sr, sg, sb = color(p.sub_color, S.dim)
            local sy = y + ui.text_height(sc) + 14
            H.draw_text(str(p.subtitle), x + (w - sw) / 2 + 1, sy + 1, ss, 0, 0, 0, ctx.alpha * 0.6)
            H.draw_text(str(p.subtitle), x + (w - sw) / 2, sy, ss, sr, sg, sb, ctx.alpha)
        end
    end,
})

H.register_type("bar", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        local w = math.min(ctx.W * 0.45, tonumber(p.width) or 420)
        return w, ui.text_height(sc) + 8 + 10
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        local text = str(p.text)
        local r, g, b = color(p.text_color, S.text)
        local tw = ui.text_width(text, sc)
        H.draw_text(text, x + (w - tw) / 2 + 1, y + 1, sc, 0, 0, 0, ctx.alpha * 0.6)
        H.draw_text(text, x + (w - tw) / 2, y, sc, r, g, b, ctx.alpha)
        local by = y + ui.text_height(sc) + 6
        local back = S.bar_back
        ui.draw_rounded_rect(x, by, w, 10, 3, back[1], back[2], back[3], (back[4] or 0.8) * ctx.alpha)
        local frac = clamp(tonumber(p.progress) or 1, 0, 1)
        if frac > 0 then
            local br, bg, bb = color(p.color, S.bar)
            ui.draw_rounded_rect(x + 1, by + 1, math.max(4, (w - 2) * frac), 8, 3, br, bg, bb, ctx.alpha)
        end
    end,
})

H.register_type("actionbar", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 1.1
        return ui.text_width(str(p.text), sc) + 16, ui.text_height(sc) + 10
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale * 1.1
        H.panel(x, y, w, h, ctx.alpha * 0.8)
        local r, g, b = color(p.color, S.accent)
        H.draw_text(str(p.text), x + 8, y + 5, sc, r, g, b, ctx.alpha)
    end,
})

H.register_type("text", {
    size = function(p, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        local lines, w = {}, 0
        for line in (str(p.text) .. "\n"):gmatch("(.-)\n") do
            lines[#lines + 1] = line
            w = math.max(w, ui.text_width(line, sc))
        end
        p._lines = lines
        local pad = p.background == false and 0 or S.padding
        return w + pad * 2, #lines * (ui.text_height(sc) + 6) - 6 + pad * 2
    end,
    draw = function(p, x, y, w, h, ctx)
        local sc = (tonumber(p.scale) or 1) * S.scale
        local pad = p.background == false and 0 or S.padding
        if p.background ~= false then H.panel(x, y, w, h, ctx.alpha) end
        local r, g, b = color(p.color, S.text)
        local ty = y + pad
        for _, line in ipairs(p._lines or { str(p.text) }) do
            local lx = x + pad
            if ctx.align == "right" then lx = x + w - pad - ui.text_width(line, sc)
            elseif ctx.align == "center" then lx = x + (w - ui.text_width(line, sc)) / 2 end
            H.draw_text(line, lx, ty, sc, r, g, b, ctx.alpha)
            ty = ty + ui.text_height(sc) + 6
        end
    end,
})

H.register_type("image", {
    size = function(p, ctx) return tonumber(p.width) or 48, tonumber(p.height) or 48 end,
    draw = function(p, x, y, w, h, ctx)
        if type(p.image) == "string" and p.image ~= "" then ui.draw_image(p.image, x, y, w, h) end
    end,
})

-- ---- Where pieces go ----
-- position: "top", "top_left", "top_right", "left", "right", "center", "bottom" (above the
-- hotbar), "bottom_left", "bottom_right", or { x = 0..1, y = 0..1 } (a point on the window; the
-- piece is centred on it). Pieces at one position stack; `order` (a number) sorts them.
local ANCHORS = {
    top          = { 0.5, 0,   "center", "down" },
    top_left     = { 0,   0,   "left",   "down" },
    top_right    = { 1,   0,   "right",  "down" },
    left         = { 0,   0.5, "left",   "middle" },
    right        = { 1,   0.5, "right",  "middle" },
    center       = { 0.5, 0.32, "center", "down" },
    bottom       = { 0.5, 1,   "center", "up" },
    bottom_left  = { 0,   1,   "left",   "up" },
    bottom_right = { 1,   1,   "right",  "up" },
}

local function draw_all(W, Hh)
    local t = now()
    local groups = {}
    for id, p in pairs(pieces) do
        local a = fade(p, t)
        if not a then
            if p.duration then pieces[id] = nil end
        elseif p.type == "timer" and p.direction ~= "up" and timer_seconds(p, t) <= 0 and tonumber(p.hide_when_done) then
            if t - (p._got or t) - (tonumber(p.seconds) or 0) > tonumber(p.hide_when_done) then pieces[id] = nil end
        end
        if pieces[id] and a then
            local key = type(p.position) == "table" and ("@" .. id) or (ANCHORS[p.position] and p.position or "top_left")
            groups[key] = groups[key] or {}
            local g = groups[key]
            g[#g + 1] = { p = p, alpha = a }
        end
    end
    -- the bottom stack starts above the hotbar (and the hearts over it)
    local bottom_y = Hh - S.margin
    if hotbar and hotbar.get_rect then
        local ok, _, by = pcall(hotbar.get_rect, W, Hh)
        if ok and by then bottom_y = by - 64 end
    end
    for key, list in pairs(groups) do
        table.sort(list, function(a, b)
            local oa, ob = tonumber(a.p.order) or 0, tonumber(b.p.order) or 0
            if oa ~= ob then return oa < ob end
            return a.p.id < b.p.id
        end)
        local anchor = ANCHORS[key]
        local ax, ay, align, grow
        if anchor then
            ax, ay, align, grow = anchor[1] * W, anchor[2] * Hh, anchor[3], anchor[4]
        else
            local pos = list[1].p.position
            ax, ay, align, grow = clamp(tonumber(pos.x) or 0.5, 0, 1) * W, clamp(tonumber(pos.y) or 0.5, 0, 1) * Hh, "center", "middle"
        end
        if key == "bottom" or key == "bottom_left" or key == "bottom_right" then ay = key == "bottom" and bottom_y or Hh - S.margin end
        if key == "top" or key == "top_left" or key == "top_right" then ay = ay + S.margin end
        -- sizes first (the middle and upward stacks need the total)
        local sizes, total = {}, 0
        for i, e in ipairs(list) do
            local def = H.types[e.p.type]
            local ctx = { W = W, H = Hh, time = t, alpha = e.alpha, align = align }
            local w, h = 0, 0
            if def then
                local ok, a, b = pcall(def.size, e.p, ctx)
                if ok then w, h = tonumber(a) or 0, tonumber(b) or 0 end
            end
            sizes[i] = { w, h, def, ctx }
            total = total + h + (i > 1 and S.gap or 0)
        end
        local y = ay
        if grow == "middle" then y = ay - total / 2 elseif grow == "up" then y = ay - total end
        for i, e in ipairs(list) do
            local w, h, def, ctx = sizes[i][1], sizes[i][2], sizes[i][3], sizes[i][4]
            if def and w > 0 and h > 0 then
                local x = ax - w / 2
                if align == "left" then x = ax + S.margin elseif align == "right" then x = ax - S.margin - w end
                local ok, err = pcall(def.draw, e.p, x, y, w, h, ctx)
                if not ok and not e.p._warned then
                    e.p._warned = true
                    print("[HUD] couldn't draw '" .. tostring(e.p.id) .. "' (" .. tostring(e.p.type) .. "): " .. tostring(err))
                end
            end
            y = y + h + S.gap
        end
    end
end

-- ===========================================================================
-- Hooks (chained: other mods' versions still run)
-- ===========================================================================
local prev_update = on_update
function on_update(dt, ...)
    clock = clock + (tonumber(dt) or 0)
    -- timers' on_done, on the server
    for key, tm in pairs(timers) do
        if timers[key] == tm and clock >= tm.done_at then
            timers[key] = nil
            local ok, err = pcall(tm.fn, tm.id, tm.target)
            if not ok then print("[HUD] timer '" .. tostring(tm.id) .. "' on_done: " .. tostring(err)) end
        end
    end
    if prev_update then return prev_update(dt, ...) end
end

local prev_hud = render_hud
function render_hud(W, Hh, ...)
    if prev_hud then prev_hud(W, Hh, ...) end
    if visible and next(pieces) and ui and ui.draw_text then draw_all(W, Hh) end
end

-- From the server (or our own server side): show, update, hide
local prev_server_message = on_server_message
function on_server_message(channel, data, ...)
    if channel == CHANNEL then
        local msg = H.decode(data)
        if msg then receive(msg, false) end
        return
    end
    if prev_server_message then return prev_server_message(channel, data, ...) end
end

-- Someone joined: what everyone sees, and anything kept for them
local prev_join = on_player_join
function on_player_join(name, ...)
    if is_authority() and engine.send_to_player then
        for _, id in ipairs(everyone_order) do
            local p = everyone[id]
            if p and not (p.duration and now() - p._shown_at >= p.duration) then
                engine.send_to_player(name, CHANNEL, H.encode({ op = "show", id = id, piece = wire_piece(p) }))
            end
        end
        for id, p in pairs(per_player[name] or {}) do
            engine.send_to_player(name, CHANNEL, H.encode({ op = "show", id = id, piece = wire_piece(p) }))
        end
    end
    if prev_join then return prev_join(name, ...) end
end

-- Left this server: its pieces go (ours stay)
local prev_mode = on_game_mode
function on_game_mode(mode, ...)
    if mode == nil then
        for id, p in pairs(pieces) do if not p._here then pieces[id] = nil end end
    end
    if prev_mode then return prev_mode(mode, ...) end
end
