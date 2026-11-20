-- ui_kit: screens, windows and drag & drop for BlockSmith mods.
--
--   local kit = require("ui_kit")
--   local board = kit.form{
--       id = "mymod:board", width = 12, height = 7, title = "Notice board",
--       draw = function(f, state)
--           f:label(0, 0, "No digging under the church.")
--           state.name = f:field("name", 0, 1.5, 7, 1, state.name, { placeholder = "Your name" })
--           if f:button("sign", 8, 1.5, 3.5, 1, "Sign") then f:close() end
--       end,
--   }
--   board:open({})
--
-- Each frame, while a form is open, its draw(f, state) runs and describes the whole screen;
-- widgets draw themselves and return what the player did. Positions and sizes are "units": the
-- kit picks a unit size in pixels that fits the window. (0, 0) is the top-left under the title.
--
-- Beyond forms (see docs/MODDING.md, section 14.5):
--   f:window(...)                  movable, resizable, stacking windows (a desktop / an "OS")
--   f:draggable / f:drop_target    drag & drop anything (f:item does it for inventories)
--   f:scroll_area(...)             a scrolling region with anything inside
--   f:icon, f:popup                desktop icons (double-click) and menus
--   kit.embed{...}                 all of the above inside ANY screen: the HUD, the creative
--                                  inventory, a menu - not only the mod-screen slot

local kit = {}

kit.theme = {
    dim          = { 0, 0, 0, 0.55 },
    panel        = { 0.14, 0.14, 0.16, 0.97 },
    border       = { 0.03, 0.03, 0.04, 1 },
    title        = { 1, 0.88, 0.55 },
    text         = { 0.95, 0.95, 0.95 },
    muted        = { 0.62, 0.62, 0.68 },
    field        = { 0.06, 0.06, 0.07, 1 },
    field_focus  = { 0.09, 0.09, 0.13, 1 },
    field_border = { 0.30, 0.30, 0.34, 1 },
    focus_border = { 0.95, 0.85, 0.45, 1 },
    accent       = { 0.38, 0.72, 0.38, 1 },
    row_hover    = { 1, 1, 1, 0.08 },
    row_selected = { 0.35, 0.55, 0.90, 0.45 },
    slot         = { 0.24, 0.24, 0.27, 1 },
    slot_border  = { 0.07, 0.07, 0.08, 1 },
    tooltip      = { 0.05, 0.05, 0.07, 0.95 },
    window       = { 0.17, 0.17, 0.20, 0.98 },
    window_title = { 0.22, 0.30, 0.48, 1 },        -- the focused window's title bar
    window_title_idle = { 0.24, 0.24, 0.28, 1 },
    window_shadow = { 0, 0, 0, 0.35 },
    drop_hover   = { 0.95, 0.85, 0.45, 0.35 },
    marquee      = { 0.40, 0.65, 1.00, 0.18 },   -- the box drawn while selecting (f:marquee)
    marquee_edge = { 0.55, 0.78, 1.00, 0.90 },
    selected_edge = { 1.00, 0.85, 0.35, 1.00 },  -- around selected windows
}
kit.text_size = 0.42     -- capital letters are this many units tall

local current = nil      -- the open form (the mod-screen slot)
local legacy = { presses = {}, scroll = 0, chars = {}, keys = {} }   -- input from hooks (older engines)

-- ---------------------------------------------------------------------------
-- Older engines: anything missing from ui.* is filled in from what's there (ui.draw_rect,
-- ui.get_mouse), so mods using the newer drawing functions still work - just less smoothly.
-- Nothing that exists is replaced. kit.full_engine says whether the engine has it all.
-- ---------------------------------------------------------------------------
kit.full_engine = ui ~= nil and ui.get_mouse_events ~= nil and ui.draw_gradient ~= nil
if ui and ui.draw_rect and not kit.full_engine and not ui.push_clip then
    -- Clipping, done here: everything drawn while a clip is pushed is trimmed to it (rectangles
    -- exactly; text a character at a time; buttons and pictures cut to size; icons hidden when
    -- they're mostly outside). Without it, a window's contents spill out when it's made smaller.
    local clips = {}
    local function clip() return clips[#clips] end
    ui.push_clip = function(x, y, w, h)
        local c = clip()
        if c then
            local x0, y0 = math.max(x, c[1]), math.max(y, c[2])
            local x1, y1 = math.min(x + w, c[1] + c[3]), math.min(y + h, c[2] + c[4])
            x, y, w, h = x0, y0, math.max(0, x1 - x0), math.max(0, y1 - y0)
        end
        clips[#clips + 1] = { x, y, w, h }
    end
    ui.pop_clip = function() clips[#clips] = nil end
    kit._clear_clips = function() clips = {} end
    local function cut(x, y, w, h)                               -- -> the visible part, or nil
        local c = clip()
        if not c then return x, y, w, h end
        local x0, y0 = math.max(x, c[1]), math.max(y, c[2])
        local x1, y1 = math.min(x + w, c[1] + c[3]), math.min(y + h, c[2] + c[4])
        if x1 <= x0 or y1 <= y0 then return nil end
        return x0, y0, x1 - x0, y1 - y0
    end
    local draw_rect = ui.draw_rect
    ui.draw_rect = function(x, y, w, h, ...)
        local cx, cy, cw, ch = cut(x, y, w, h)
        if cx then draw_rect(cx, cy, cw, ch, ...) end
    end
    local draw_text = ui.draw_text
    if draw_text then
        ui.draw_text = function(text, x, y, scale, ...)
            local c = clip()
            if not c or type(scale) ~= "number" or select("#", ...) < 3 then return draw_text(text, x, y, scale, ...) end
            text = tostring(text)
            local h = ui.text_height and ui.text_height(scale) or 14 * scale
            if y - h * 0.6 < c[2] or y - h * 0.4 > c[2] + c[4] then return end      -- (y is the baseline)
            local right = c[1] + c[3]
            while text ~= "" and x < c[1] do                          -- characters left of the clip
                local n = utf8.offset(text, 2) or (#text + 1)
                x = x + ui.text_width(text:sub(1, n - 1), scale)
                text = text:sub(n)
            end
            while text ~= "" and x + ui.text_width(text, scale) > right do  -- ...and right of it
                text = text:sub(1, (utf8.offset(text, -1) or #text) - 1)
            end
            if text ~= "" then return draw_text(text, x, y, scale, ...) end
        end
    end
    local draw_button = ui.draw_button
    if draw_button then
        ui.draw_button = function(x, y, w, h, ...)
            local cx, cy, cw, ch = cut(x, y, w, h)
            if not cx then return true end
            return draw_button(cx, cy, cw, ch, ...)
        end
    end
    local draw_icon = ui.draw_block_icon
    if draw_icon then
        ui.draw_block_icon = function(name, x, y, size, ...)
            local c = clip()
            if c then
                local mx, my = x + size / 2, y + size / 2
                if mx < c[1] or mx > c[1] + c[3] or my < c[2] or my > c[2] + c[4] then return end
            end
            return draw_icon(name, x, y, size, ...)
        end
    end
    local draw_image0 = ui.draw_image
    if draw_image0 then
        ui.draw_image = function(path, x, y, w, h, u0, v0, u1, v1)
            local c = clip()
            if not c or type(u0) == "table" or w <= 0 or h <= 0 then return draw_image0(path, x, y, w, h, u0, v0, u1, v1) end
            u0, v0, u1, v1 = u0 or 0, v0 or 0, u1 or 1, v1 or 1
            local cx, cy, cw, ch = cut(x, y, w, h)
            if not cx then return end
            local du, dv = (u1 - u0) / w, (v1 - v0) / h           -- crop the picture to match
            return draw_image0(path, cx, cy, cw, ch, u0 + (cx - x) * du, v0 + (cy - y) * dv,
                               u0 + (cx + cw - x) * du, v0 + (cy + ch - y) * dv)
        end
    end
end
if ui and ui.draw_rect and not kit.full_engine then
    local rect = ui.draw_rect
    local function fill_rows(y0, y1, span, r, g, b, a)          -- span(y) -> x0, x1 (or nil)
        local rows = math.max(1, math.min(96, math.floor(y1 - y0)))
        local h = (y1 - y0) / rows
        for i = 0, rows - 1 do
            local y = y0 + (i + 0.5) * h
            local x0, x1 = span(y)
            if x0 and x1 > x0 then rect(x0, y0 + i * h, x1 - x0, h + 0.5, r, g, b, a) end
        end
    end
    ui.draw_rect_outline = ui.draw_rect_outline or function(x, y, w, h, t, r, g, b, a)
        rect(x, y, w, t, r, g, b, a); rect(x, y + h - t, w, t, r, g, b, a)
        rect(x, y + t, t, h - 2 * t, r, g, b, a); rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a)
    end
    ui.draw_rounded_rect = ui.draw_rounded_rect or function(x, y, w, h, rad, r, g, b, a)
        rad = math.max(0, math.min(rad, w / 2, h / 2))
        fill_rows(y, y + h, function(yy)
            local d = 0
            local dy = math.max(y + rad - yy, yy - (y + h - rad), 0)
            if dy > 0 then d = rad - math.sqrt(math.max(0, rad * rad - dy * dy)) end
            return x + d, x + w - d
        end, r, g, b, a)
    end
    ui.draw_circle = ui.draw_circle or function(cx, cy, rad, r, g, b, a)
        fill_rows(cy - rad, cy + rad, function(yy)
            local dx = math.sqrt(math.max(0, rad * rad - (yy - cy) ^ 2))
            return cx - dx, cx + dx
        end, r, g, b, a)
    end
    ui.draw_line = ui.draw_line or function(x1, y1, x2, y2, t, r, g, b, a)
        local len = math.sqrt((x2 - x1) ^ 2 + (y2 - y1) ^ 2)
        local n = math.max(1, math.min(200, math.floor(len / math.max(1, t * 0.6))))
        for i = 0, n do
            local f = i / n
            rect(x1 + (x2 - x1) * f - t / 2, y1 + (y2 - y1) * f - t / 2, t, t, r, g, b, a)
        end
    end
    ui.draw_ring = ui.draw_ring or function(cx, cy, rad, t, r, g, b, a)
        local n = math.max(12, math.min(120, math.floor(rad)))
        for i = 0, n - 1 do
            local ang = i / n * math.pi * 2
            rect(cx + math.cos(ang) * (rad - t / 2) - t / 2, cy + math.sin(ang) * (rad - t / 2) - t / 2, t, t, r, g, b, a)
        end
    end
    local function polygon(pts, r, g, b, a)                  -- scanline fill of a convex shape
        local n = #pts // 2
        if n < 3 then return end
        local y0, y1 = math.huge, -math.huge
        for i = 1, n do y0 = math.min(y0, pts[i * 2]); y1 = math.max(y1, pts[i * 2]) end
        fill_rows(y0, y1, function(yy)
            local lo, hi = math.huge, -math.huge
            for i = 1, n do
                local ax, ay = pts[i * 2 - 1], pts[i * 2]
                local j = i % n + 1
                local bx, by = pts[j * 2 - 1], pts[j * 2]
                if (ay <= yy and by > yy) or (by <= yy and ay > yy) then
                    local x = ax + (yy - ay) / (by - ay) * (bx - ax)
                    lo, hi = math.min(lo, x), math.max(hi, x)
                end
            end
            if lo <= hi then return lo, hi end
        end, r, g, b, a)
    end
    ui.draw_polygon = ui.draw_polygon or polygon
    ui.draw_triangle = ui.draw_triangle or function(x1, y1, x2, y2, x3, y3, r, g, b, a)
        polygon({ x1, y1, x2, y2, x3, y3 }, r, g, b, a)
    end
    ui.draw_pie = ui.draw_pie or function(cx, cy, rad, from, to, r, g, b, a)
        local steps = math.max(2, math.floor(math.abs(to - from) / 10))
        for i = 0, steps - 1 do
            local a1 = math.rad(from + (to - from) * i / steps)
            local a2 = math.rad(from + (to - from) * (i + 1) / steps)
            polygon({ cx, cy, cx + math.cos(a1) * rad, cy + math.sin(a1) * rad,
                      cx + math.cos(a2) * rad, cy + math.sin(a2) * rad }, r, g, b, a)
        end
    end
    ui.draw_gradient = ui.draw_gradient or function(x, y, w, h, c1, c2, dir)
        local n = 32
        for i = 0, n - 1 do
            local f = (i + 0.5) / n
            local cr, cg, cb = c1[1] + (c2[1] - c1[1]) * f, c1[2] + (c2[2] - c1[2]) * f, c1[3] + (c2[3] - c1[3]) * f
            local ca = (c1[4] or 1) + ((c2[4] or 1) - (c1[4] or 1)) * f
            if dir == "horizontal" then rect(x + w * i / n, y, w / n + 0.5, h, cr, cg, cb, ca)
            else rect(x, y + h * i / n, w, h / n + 0.5, cr, cg, cb, ca) end
        end
    end
    ui.set_cursor = ui.set_cursor or function() end
    ui.is_mouse_down = ui.is_mouse_down or function(which)
        local _, _, l, rgt = ui.get_mouse()
        if which == "right" then return rgt and true or false end
        return l and true or false
    end
    if ui.draw_image and not ui.get_image_size then         -- the options table: only the part of the picture
        local draw_image = ui.draw_image
        ui.draw_image = function(path, x, y, w, h, a, b, c, d)
            if type(a) == "table" then
                return draw_image(path, x, y, w, h, a.u0 or 0, a.v0 or 0, a.u1 or 1, a.v1 or 1)
            end
            if a == nil then return draw_image(path, x, y, w, h) end
            return draw_image(path, x, y, w, h, a, b, c, d)
        end
    end
    print("[ui_kit] This engine is older than ui_kit: shapes are drawn from rectangles and clipping is done in Lua. " ..
          "Rebuild the engine with the latest Inventory.cpp, main.cpp and UiDraw.h for everything.")
end

local K = (ui and ui.keys) or {}
local TITLE_H = 1.0     -- a window's title bar (units)

-- The caret in text fields (lualib/textedit.lua): move it with the arrows, Home / End, a click
local te_ok, te = pcall(require, "textedit")
if not te_ok then te = nil end
local EDIT_KEYS = { [K.LEFT or -1] = true, [K.RIGHT or -1] = true, [K.HOME or -1] = true,
                    [K.END or -1] = true, [K.BACKSPACE or -1] = true, [K.DELETE or -1] = true }
local A = (ui and ui.actions) or { PRESS = 1, RELEASE = 0, REPEAT = 2 }

local function color(c, a) return c[1], c[2], c[3], a or c[4] or 1 end
local function inside(px, py, x, y, w, h) return px >= x and px < x + w and py >= y and py < y + h end
local function cursor(name) if ui.set_cursor then ui.set_cursor(name) end end
local function queues() return ui.get_mouse_events ~= nil end
local function mouse_down()
    if ui.is_mouse_down then return ui.is_mouse_down("left") end
    local _, _, l = ui.get_mouse()
    return l and true or false
end

local cap_per_scale = nil
local function text_scale(u, size)
    if not cap_per_scale or cap_per_scale <= 0 then
        cap_per_scale = (ui.text_height and ui.text_height(1)) or 14
        if cap_per_scale <= 0 then cap_per_scale = 14 end
    end
    return (u * size) / cap_per_scale
end

local function drop_last_char(s)
    local i = #s
    while i > 0 do
        local b = s:byte(i)
        if b < 0x80 or b >= 0xC0 then break end
        i = i - 1
    end
    return s:sub(1, math.max(0, i - 1))
end
local function char_of(cp)
    if utf8 and utf8.char then return utf8.char(cp) end
    return cp < 128 and string.char(cp) or ""
end

-- ---------------------------------------------------------------------------
-- Input of one frame for one host (a form or an embed)
-- ---------------------------------------------------------------------------
-- (older engines) a press within half a second of the last one, in the same place: a double-click
local last_press = nil
local function ctrl_down()
    if not (ui and ui.is_key_down and ui.keys) then return false end
    return (ui.keys.LEFT_CONTROL and ui.is_key_down(ui.keys.LEFT_CONTROL)) or
           (ui.keys.RIGHT_CONTROL and ui.is_key_down(ui.keys.RIGHT_CONTROL)) or false
end
local function count_clicks(p)
    if p.ctrl == nil then p.ctrl = ctrl_down() end
    local now = ui.get_time and ui.get_time() or 0
    if last_press and last_press.button == p.button and now - last_press.t <= 0.5
       and math.abs(p.x - last_press.x) <= 6 and math.abs(p.y - last_press.y) <= 6 then
        p.clicks = math.min(3, last_press.clicks + 1)
    else
        p.clicks = 1
    end
    last_press = { t = now, x = p.x, y = p.y, button = p.button, clicks = p.clicks }
    return p
end

local function gather_input(host_has_input, host)
    local inp = { press = nil, releases = {}, scroll = 0, text = {}, keys = {} }
    if not host_has_input then
        if host then host.prev_l, host.prev_r = nil, nil end
        return inp
    end
    if queues() then
        for _, e in ipairs(ui.get_mouse_events()) do
            if e.pressed and not inp.press then
                inp.press = { x = e.x, y = e.y, button = e.button, clicks = e.clicks or 1, shift = e.shift, ctrl = e.ctrl }
            elseif e.released then
                inp.releases[#inp.releases + 1] = { x = e.x, y = e.y, button = e.button }
            end
        end
        local _, dy = ui.get_scroll()
        inp.scroll = dy or 0
        local typed = ui.get_typed_text()
        if typed ~= "" then
            for _, cp in utf8.codes(typed) do inp.text[#inp.text + 1] = char_of(cp) end
        end
        for _, k in ipairs(ui.get_key_events()) do
            inp.keys[#inp.keys + 1] = { key = k.key, press = k.action ~= "release", first = k.action == "press", ctrl = k.ctrl, shift = k.shift }
        end
    elseif host and host.embed then                    -- older engine, in any screen: watch the buttons
        local mx, my, l, rgt = ui.get_mouse()
        l, rgt = l and true or false, rgt and true or false
        if host.prev_l ~= nil then
            if l and not host.prev_l then inp.press = count_clicks({ x = mx, y = my, button = "left" })
            elseif rgt and not host.prev_r then inp.press = count_clicks({ x = mx, y = my, button = "right" }) end
            if host.prev_l and not l then inp.releases[1] = { x = mx, y = my, button = "left" } end
        end
        host.prev_l, host.prev_r = l, rgt
    else                                               -- older engine: what the hooks collected
        inp.press = legacy.presses[1] and count_clicks(legacy.presses[1])
        inp.scroll = legacy.scroll
        inp.text = legacy.chars
        for _, k in ipairs(legacy.keys) do
            inp.keys[#inp.keys + 1] = { key = k.key, press = k.action ~= A.RELEASE, first = k.action == A.PRESS,
                                        ctrl = (k.mods or 0) % 4 >= 2, shift = (k.mods or 0) % 2 == 1 }
        end
        legacy = { presses = {}, scroll = 0, chars = {}, keys = {} }
    end
    return inp
end

-- ---------------------------------------------------------------------------
-- Frame: what draw functions get. Coordinates are units inside the frame.
-- ---------------------------------------------------------------------------
local Frame = {}
Frame.__index = Frame

-- Closes the form this frame belongs to (does nothing in an embed)
function Frame:close() local form = self.host_ref.form if form then form:close() end end

function Frame:px(x, y) return self.cx + x * self.u, self.cy + y * self.u end
function Frame:pw(w) return w * self.u end
function Frame:sid(id) return self.prefix .. tostring(id) end   -- ids are per window / scroll area

-- Is this point (pixels) visible in this frame, and is this frame what's on top there?
function Frame:owns(px, py)
    local c = self.clip
    if c and not inside(px, py, c[1], c[2], c[3], c[4]) then return false end
    return self.host_ref:owner_at(px, py) == self.owner
end

function Frame:hovered(x, y, w, h)
    local px, py = self:px(x, y)
    return inside(self.mx, self.my, px, py, w * self.u, h * self.u) and self:owns(self.mx, self.my)
end

-- This frame's press, if it was inside the rectangle, on this frame, and nothing took it yet.
-- button: "left" (default), "right", "middle" or "any"
function Frame:take_click(x, y, w, h, button)
    local c = self.input.press
    if not c or c.used then return nil end
    button = button or "left"
    if button ~= "any" and c.button ~= button then return nil end
    local px, py = self:px(x, y)
    if not inside(c.x, c.y, px, py, w * self.u, h * self.u) or not self:owns(c.x, c.y) then return nil end
    c.used = true
    return c
end

function Frame:rect(x, y, w, h, c)
    local px, py = self:px(x, y)
    ui.draw_rect(px, py, w * self.u, h * self.u, color(c))
end
function Frame:box(x, y, w, h, fill, border)
    local px, py = self:px(x, y)
    local W, H = w * self.u, h * self.u
    ui.draw_rect(px - 1, py - 1, W + 2, H + 2, color(border or kit.theme.border))
    ui.draw_rect(px, py, W, H, color(fill))
end

function Frame:label(x, y, text, opts)
    opts = opts or {}
    text = tostring(text)
    local size = opts.size or kit.text_size
    local s = text_scale(self.u, size)
    local tw = (ui.text_width and ui.text_width(text, s) or #text * 8) / self.u
    if opts.w and opts.align == "center" then x = x + (opts.w - tw) / 2
    elseif opts.w and opts.align == "right" then x = x + opts.w - tw end
    local px, py = self:px(x, y)
    local c = opts.color or kit.theme.text
    ui.draw_text(text, px, py + size * self.u, s, c[1], c[2], c[3], c[4] or 1)   -- (y is the baseline)
    return tw
end

-- How wide text is, in units (at `size` units tall; default kit.text_size)
function Frame:text_width(text, size)
    local s = text_scale(self.u, size or kit.text_size)
    return (ui.text_width and ui.text_width(tostring(text), s) or #tostring(text) * 8) / self.u
end

-- Text that fits `max_w` units: drawn smaller (down to 70%), then cut short with "..."
-- Returns the text and the size to draw it at.
function Frame:fit_text(text, size, max_w)
    text = tostring(text or "")
    if max_w <= 0 or self:text_width(text, size) <= max_w then return text, size end
    local smaller = size * math.max(0.7, max_w / self:text_width(text, size))
    if self:text_width(text, smaller) <= max_w then return text, smaller end
    local chars = utf8 and utf8.len(text) or #text
    while chars > 1 do
        chars = chars - 1
        local cut = utf8 and text:sub(1, (utf8.offset(text, chars + 1) or (#text + 1)) - 1) or text:sub(1, chars)
        local try = cut:gsub("%s+$", "") .. "..."
        if self:text_width(try, smaller) <= max_w then return try, smaller end
    end
    return "...", smaller
end

local function wrap_lines(self, w, text, opts)
    opts = opts or {}
    local size = opts.size or kit.text_size
    local s = text_scale(self.u, size)
    local line_h = size * (1 + (opts.line_gap or 0.6))
    local limit = w * self.u
    local lines = {}
    for para in (tostring(text) .. "\n"):gmatch("(.-)\n") do
        local line = ""
        for word in para:gmatch("%S+") do
            local try = line == "" and word or (line .. " " .. word)
            if line ~= "" and ui.text_width(try, s) > limit then lines[#lines + 1] = line; line = word
            else line = try end
        end
        lines[#lines + 1] = line
    end
    return lines, line_h, size
end

-- How tall f:text(x, y, w, text, opts) would be, without drawing it (units)
function Frame:measure_text(w, text, opts)
    local lines, line_h = wrap_lines(self, w, text, opts)
    return #lines * line_h
end

function Frame:text(x, y, w, text, opts)
    opts = opts or {}
    local size = opts.size or kit.text_size
    local s = text_scale(self.u, size)
    local line_h = size * (1 + (opts.line_gap or 0.6))
    local limit = w * self.u
    local lines = {}
    for para in (tostring(text) .. "\n"):gmatch("(.-)\n") do
        local line = ""
        for word in para:gmatch("%S+") do
            local try = line == "" and word or (line .. " " .. word)
            if line ~= "" and ui.text_width(try, s) > limit then lines[#lines + 1] = line; line = word
            else line = try end
        end
        lines[#lines + 1] = line
    end
    for i, l in ipairs(lines) do
        self:label(x, y + (i - 1) * line_h, l, { size = size, color = opts.color, w = w, align = opts.align })
    end
    return #lines * line_h
end

-- A picture. Either u0, v0, u1, v1 numbers, or an options table (tint, alpha, rotation, flip_x...)
function Frame:image(x, y, w, h, path, a, b, c, d)
    local px, py = self:px(x, y)
    ui.draw_image(path, px, py, w * self.u, h * self.u, a, b, c, d)
end

function Frame:tooltip(x, y, w, h, text)
    if text and self:hovered(x, y, w, h) then self.host_ref.tip = text end
end

function Frame:button(id, x, y, w, h, label, opts)
    opts = opts or {}
    local hover = not opts.disabled and self:hovered(x, y, w, h)
    if hover then cursor("hand") end
    local state = opts.disabled and "disabled" or (hover and "hover") or (opts.selected and "selected") or "normal"
    local px, py = self:px(x, y)
    if not (ui.draw_button and ui.draw_button(px, py, w * self.u, h * self.u, state, opts.danger)) then
        self:box(x, y, w, h, hover and { 0.42, 0.42, 0.48, 1 } or { 0.30, 0.30, 0.34, 1 })
    end
    local r, g, b = 1, 1, 1
    if ui.get_button_text_color then r, g, b = ui.get_button_text_color(hover) end
    if opts.disabled then r, g, b = r * 0.55, g * 0.55, b * 0.55 end
    -- The label always stays inside the button: smaller if it has to be, then cut short (the
    -- whole text is in the tooltip then)
    local size = opts.size or math.min(kit.text_size, h * 0.42)
    local text, fitted = self:fit_text(label or "", size, w - 0.5)
    self:label(x, y + (h - fitted) / 2, text, { size = fitted, color = { r, g, b }, w = w, align = "center" })
    local tip = opts.tooltip
    if text ~= tostring(label or "") then tip = tip and (tostring(label) .. " - " .. tip) or tostring(label) end
    self:tooltip(x, y, w, h, tip)
    if opts.disabled then return false end
    return self:take_click(x, y, w, h) ~= nil
end

-- The width a button needs for its label (units), for laying out buttons that fit their text
function Frame:button_width(label, h, opts)
    local size = (opts and opts.size) or math.min(kit.text_size, (h or 1) * 0.42)
    return self:text_width(label or "", size) + 1.0
end

function Frame:checkbox(id, x, y, label, value, opts)
    opts = opts or {}
    local s = 0.8
    local tw = (label and label ~= "") and (self:label(x + s + 0.3, y + (s - kit.text_size) / 2, label, { color = opts.color }) + 0.3) or 0
    local hover = self:hovered(x, y, s + tw, s)
    if hover then cursor("hand") end
    self:box(x, y, s, s, hover and kit.theme.field_focus or kit.theme.field, hover and kit.theme.focus_border or kit.theme.field_border)
    if value then self:rect(x + s * 0.22, y + s * 0.22, s * 0.56, s * 0.56, kit.theme.accent) end
    self:tooltip(x, y, s + tw, s, opts.tooltip)
    if self:take_click(x, y, s + tw, s) then value = not value end
    return value and true or false
end

function Frame:slider(id, x, y, w, value, opts)
    opts = opts or {}
    local lo, hi = opts.min or 0, opts.max or 1
    value = math.max(lo, math.min(hi, value or lo))
    local h = 0.8
    local host, sid = self.host_ref, self:sid(id)
    local px = self:px(x, y)
    local function at(mouse_x)
        local t = math.max(0, math.min(1, (mouse_x - px) / (w * self.u)))
        local v = lo + (hi - lo) * t
        if opts.step and opts.step > 0 then v = lo + math.floor((v - lo) / opts.step + 0.5) * opts.step end
        return v
    end
    local c = self:take_click(x, y, w, h)
    if c then value = at(c.x); host.drag = sid end
    if host.drag == sid then
        if mouse_down() then value = at(self.mx) else host.drag = nil end
    end
    local t = (hi > lo) and (value - lo) / (hi - lo) or 0
    self:rect(x, y + h * 0.4, w, h * 0.2, kit.theme.field_border)
    self:rect(x, y + h * 0.4, w * t, h * 0.2, kit.theme.accent)
    local hover = self:hovered(x, y, w, h) or host.drag == sid
    if hover then cursor("hand") end
    self:box(x + w * t - 0.2, y, 0.4, h, hover and { 1, 1, 1, 1 } or { 0.82, 0.82, 0.86, 1 })
    self:tooltip(x, y, w, h, opts.tooltip)
    return value
end

-- A one-line text box -> text, submitted. Ctrl+C copies it, Ctrl+X cuts it, Ctrl+V pastes.
function Frame:field(id, x, y, w, h, text, opts)
    opts = opts or {}
    text = tostring(text or "")
    local host, sid = self.host_ref, self:sid(id)
    local tid = tostring(host) .. "|" .. sid               -- (the caret's id in textedit)
    local mask = opts.password and "*" or nil
    local size = opts.size or math.min(kit.text_size, h * 0.45)
    local s = text_scale(self.u, size)
    local room = (w - 0.5) * self.u
    host.fields[#host.fields + 1] = sid
    if self:hovered(x, y, w, h) then cursor("text") end
    local click = self:take_click(x, y, w, h)
    if click then
        host.focus = sid; host.clicked_field = sid
        if te then                                          -- the caret goes where you click
            local tx = self:px(x + 0.25, y)
            te.focus(tid, text, click.x - tx, s, mask)
        end
    end
    local focused = host.focus == sid
    local submitted = false
    if focused then
        local max = opts.max_length or 256
        local function add(str)
            if not utf8.len(str) then return end          -- not valid UTF-8: ignore it
            local ok = ""
            for _, cp in utf8.codes(str) do
                local ch = char_of(cp)
                if (not opts.numeric or ch:match("^[%d%.%-]$")) and cp >= 32 then ok = ok .. ch end
            end
            if te then text = te.insert(tid, text, ok, max)
            else
                for _, cp in utf8.codes(ok) do
                    local ch = char_of(cp)
                    if #text + #ch <= max then text = text .. ch end
                end
            end
        end
        for _, ch in ipairs(self.input.text) do add(ch) end
        for _, k in ipairs(self.input.keys) do
            if k.press then
                if te and EDIT_KEYS[k.key] then              -- Left / Right / Home / End / Backspace / Delete (Ctrl: words)
                    text = te.key(tid, text, k.key, "press", k.ctrl)
                elseif k.key == K.BACKSPACE then text = k.ctrl and "" or drop_last_char(text)
                elseif (k.key == K.ENTER or k.key == K.KP_ENTER) and k.first then submitted = true; host.focus = nil
                elseif k.key == K.TAB and k.first then host.tab = sid
                elseif k.ctrl and k.first and k.key == K.V and ui.get_clipboard then
                    local clip = ui.get_clipboard()
                    if clip then add((clip:gsub("[\r\n\t]", " "))) end
                elseif k.ctrl and k.first and (k.key == K.C or k.key == K.X) and ui.set_clipboard and not opts.password then
                    ui.set_clipboard(text)
                    if k.key == K.X then text = ""; if te then te.reset(tid) end end
                end
            end
        end
    end
    self:box(x, y, w, h, focused and kit.theme.field_focus or kit.theme.field,
             focused and kit.theme.focus_border or kit.theme.field_border)
    if text == "" and not focused and opts.placeholder then
        self:label(x + 0.25, y + (h - size) / 2, opts.placeholder, { size = size, color = kit.theme.muted })
    elseif focused and te then
        -- scrolled so the caret stays in view, and the caret where it is
        local shown, caret_x = te.view(tid, text, room, s, mask)
        self:label(x + 0.25, y + (h - size) / 2, shown, { size = size })
        if te.visible(tid) then
            self:rect(x + 0.25 + caret_x / self.u, y + (h - size * 1.3) / 2, 0.08, size * 1.3, kit.theme.text)
        end
    else
        local shown = mask and string.rep(mask, utf8.len(text) or #text) or text
        if focused then                                     -- (no textedit: the end, caret after it)
            while shown ~= "" and ui.text_width(shown, s) > room do shown = shown:sub(2) end
        else                                                -- not being typed in: its start
            while shown ~= "" and ui.text_width(shown, s) > room do shown = shown:sub(1, -2) end
        end
        local tw = self:label(x + 0.25, y + (h - size) / 2, shown, { size = size })
        if focused and math.floor((ui.get_time and ui.get_time() or 0) * 2) % 2 == 0 then
            self:rect(x + 0.25 + tw + 0.05, y + (h - size * 1.3) / 2, 0.08, size * 1.3, kit.theme.text)
        end
    end
    self:tooltip(x, y, w, h, opts.tooltip)
    return text, submitted
end

-- The wheel, if the mouse is over this rectangle (and nothing used it yet): dy (> 0 = up)
function Frame:take_scroll(x, y, w, h)
    if self.input.scroll == 0 or self.input.scroll_used or not self:hovered(x, y, w, h) then return 0 end
    self.input.scroll_used = true
    return self.input.scroll
end

function Frame:list(id, x, y, w, h, items, selected, opts)
    opts = opts or {}
    local host, sid = self.host_ref, self:sid(id)
    local rh = opts.row_height or 1
    local visible = math.max(1, math.floor(h / rh))
    local max_scroll = math.max(0, #items - visible)
    host.scrolls[sid] = math.max(0, math.min(max_scroll, (host.scrolls[sid] or 0) - self:take_scroll(x, y, w, h)))
    local first = host.scrolls[sid]
    self:box(x, y, w, h, kit.theme.field, kit.theme.field_border)
    local clicked, opened = false, false
    local size = opts.size or math.min(kit.text_size, rh * 0.45)
    local bar = (#items > visible) and 0.3 or 0
    for row = 1, visible do
        local i = first + row
        local item = items[i]
        if not item then break end
        local ry = y + (row - 1) * rh
        local hover = self:hovered(x, ry, w - bar, rh)
        if i == selected then self:rect(x, ry, w - bar, rh, kit.theme.row_selected)
        elseif hover then self:rect(x, ry, w - bar, rh, kit.theme.row_hover) end
        if hover then cursor("hand") end
        local text = type(item) == "table" and item.text or tostring(item)
        local c = type(item) == "table" and item.color or nil
        self:label(x + 0.25, ry + (rh - size) / 2, text, { size = size, color = c })
        local press = self:take_click(x, ry, w - bar, rh)
        if press then selected = i; clicked = true; opened = (press.clicks or 1) >= 2 end
    end
    if bar > 0 then
        local th = h * visible / #items
        self:rect(x + w - bar, y + (h - th) * first / max_scroll, bar, th, kit.theme.field_border)
    end
    return selected, clicked, opened
end

-- ---------------------------------------------------------------------------
-- Drag & drop
-- ---------------------------------------------------------------------------
-- Makes a rectangle draggable. payload: anything (a table describing what's dragged).
-- opts.ghost(px, py, u): draws what follows the mouse (pixels; default: a faded box).
-- Returns true while THIS item is being dragged (draw it faded, or not at all).
function Frame:draggable(id, x, y, w, h, payload, opts)
    opts = opts or {}
    local host, sid = self.host_ref, self:sid(id)
    local px, py = self:px(x, y)
    local press = self:take_click(x, y, w, h, opts.button or "left")
    if press then
        host.dnd = { id = sid, payload = payload, sx = press.x, sy = press.y, ox = press.x - px, oy = press.y - py,
                     w = w * self.u, h = h * self.u, ghost = opts.ghost, active = false, press = press }
    end
    local d = host.dnd
    if d and d.id == sid then
        if not d.active and (math.abs(self.mx - d.sx) > 4 or math.abs(self.my - d.sy) > 4) then d.active = true end
        if d.active then cursor("move") end
        return d.active
    end
    if self:hovered(x, y, w, h) then cursor("hand") end
    return false
end

-- A place things can be dropped. Returns the payload on the frame something is dropped here.
-- accept(payload) -> false refuses it. While something acceptable is dragged over it, it lights up.
function Frame:drop_target(id, x, y, w, h, accept)
    local host = self.host_ref
    local d = host.dnd
    if not d or not d.active then return nil end
    if accept and not accept(d.payload) then return nil end
    local px, py = self:px(x, y)
    local function over(qx, qy) return inside(qx, qy, px, py, w * self.u, h * self.u) and self:owns(qx, qy) end
    if host.dropped_at then
        if not d.done and over(host.dropped_at.x, host.dropped_at.y) then
            d.done = true
            return d.payload
        end
        return nil
    end
    if over(self.mx, self.my) then self:rect(x, y, w, h, kit.theme.drop_hover) end
    return nil
end

-- Is something being dragged right now? -> payload or nil
function Frame:dragging() local d = self.host_ref.dnd return d and d.active and d.payload or nil end

-- An inventory slot. Returns click, dropped:
--   click   { button = "left"|"right", shift, clicks } on the frame it's clicked (not dragged)
--   dropped the payload of something dropped on this slot (with opts.drop = true)
-- opts: count, tooltip, selected, drag = payload (makes the item draggable), drop = true,
--       accept = function(payload) -> bool
function Frame:item(x, y, size, name, opts)
    opts = opts or {}
    local dragging_me = false
    local draggable = opts.drag ~= nil and name ~= nil and name ~= ""
    if draggable then
        local item_name, count = name, opts.count
        dragging_me = self:draggable(opts.id or ("item@" .. x .. "," .. y), x, y, size, size, opts.drag, {
            ghost = function(px, py, u)
                if ui.draw_block_icon then ui.draw_block_icon(item_name, px + size * u * 0.1, py + size * u * 0.1, size * u * 0.8) end
                if count and count > 1 then ui.draw_text(tostring(count), px + size * u * 0.55, py + size * u * 0.95,
                                                         text_scale(u, size * 0.32), 1, 1, 1) end
            end,
        })
    end
    self:box(x, y, size, size, opts.selected and kit.theme.field_focus or kit.theme.slot,
             opts.selected and kit.theme.focus_border or kit.theme.slot_border)
    if self:hovered(x, y, size, size) then self:rect(x, y, size, size, kit.theme.row_hover) end
    if name and name ~= "" and not dragging_me then
        local px, py = self:px(x + size * 0.1, y + size * 0.1)
        if ui.draw_block_icon then ui.draw_block_icon(name, px, py, size * 0.8 * self.u) end
        if opts.count and opts.count > 1 then
            local cs = size * 0.32
            self:label(x, y + size - cs - 0.06, tostring(opts.count), { size = cs, w = size - 0.08, align = "right" })
        end
        self:tooltip(x, y, size, size, opts.tooltip or name)
    end
    local dropped = opts.drop and self:drop_target((opts.id or "") .. "drop@" .. x .. "," .. y, x, y, size, size, opts.accept) or nil
    -- A plain click (pressed and let go without dragging) on a draggable item counts as a click
    local click = nil
    local d = self.host_ref.dnd
    if draggable and d and d.press and d.id == self:sid(opts.id or ("item@" .. x .. "," .. y)) and self.host_ref.released and not d.active then
        click = { button = d.press.button, shift = d.press.shift, clicks = d.press.clicks }
    elseif not draggable then
        local c = self:take_click(x, y, size, size, "any")
        if c then click = { button = c.button, shift = c.shift, clicks = c.clicks } end
    else
        local c = self:take_click(x, y, size, size, "right")   -- right-click on a draggable item
        if c then click = { button = c.button, shift = c.shift, clicks = c.clicks } end
    end
    return click, dropped
end

-- ---------------------------------------------------------------------------
-- Scroll areas, icons, popups
-- ---------------------------------------------------------------------------
-- A region with its own scrolling (wheel or the bar). fn(sf) draws inside it; sf's (0, 0) is the
-- top of the content. content_h: how tall the content is, in units.
function Frame:scroll_area(id, x, y, w, h, content_h, fn)
    local host, sid = self.host_ref, self:sid(id)
    local max_scroll = math.max(0, (content_h or h) - h)
    local bar = max_scroll > 0 and 0.35 or 0
    local scroll = host.scrolls[sid] or 0
    -- (the wheel is read after the content: a list or scroll area inside gets it first)
    -- the bar: drag it
    if bar > 0 then
        local th = math.max(0.8, h * h / content_h)
        local ty = y + (h - th) * (scroll / max_scroll)
        if self:take_click(x + w - bar, y, bar, h) then host.drag = sid .. "#bar" end
        if host.drag == sid .. "#bar" then
            if mouse_down() then
                local _, py0 = self:px(x, y)
                local t = math.max(0, math.min(1, ((self.my - py0) / self.u - th / 2) / (h - th)))
                scroll = t * max_scroll
            else host.drag = nil end
        end
        scroll = math.max(0, math.min(max_scroll, scroll))
        ty = y + (h - th) * (scroll / max_scroll)
        self:rect(x + w - bar, y, bar, h, { 0, 0, 0, 0.25 })
        self:rect(x + w - bar, ty, bar, th, kit.theme.field_border)
    end
    host.scrolls[sid] = math.max(0, math.min(max_scroll, scroll))
    local px, py = self:px(x, y)
    local child = setmetatable({}, Frame)
    for k, v in pairs(self) do child[k] = v end
    child.cx, child.cy = px, py - host.scrolls[sid] * self.u
    child.prefix = sid .. "/"
    local clip = { px, py, (w - bar) * self.u, h * self.u }
    if self.clip then                         -- inside the parent's visible part only
        local c = self.clip
        local x0, y0 = math.max(clip[1], c[1]), math.max(clip[2], c[2])
        local x1, y1 = math.min(clip[1] + clip[3], c[1] + c[3]), math.min(clip[2] + clip[4], c[2] + c[4])
        clip = { x0, y0, math.max(0, x1 - x0), math.max(0, y1 - y0) }
    end
    child.clip = clip
    if ui.push_clip then ui.push_clip(clip[1], clip[2], clip[3], clip[4]) end
    local ok, err = pcall(fn, child, (host.scrolls[sid] or 0))
    if ui.pop_clip then ui.pop_clip() end
    if not ok then error(err, 0) end
    -- The wheel, if nothing inside used it (and there's something to scroll)
    if max_scroll > 0 then
        local dy = self:take_scroll(x, y, w, h)
        if dy ~= 0 then host.scrolls[sid] = math.max(0, math.min(max_scroll, host.scrolls[sid] - dy * 1.5)) end
    end
end

-- A desktop icon: a picture (or an item) with a label under it. Returns what the player did:
--   "select"  pressed on it (second value: { ctrl, shift } - Ctrl+click usually adds to a selection)
--   "open"    double-clicked
--   "menu"    right-clicked
-- With opts.movable = true it can also be dragged (yours to move: keep its x, y in your state):
--   "drag", dx, dy, mods   being dragged: move it (and any other selected icons) by dx, dy units
--   "drop", mods           let go after a drag (e.g. snap to a grid, save positions)
--   "click", mods          let go without dragging (e.g. now select only this one)
-- opts: selected, item (draw a block/item icon), draw = function(px, py, size_px), tooltip, movable
function Frame:icon(id, x, y, size, picture, label, opts)
    opts = opts or {}
    local host, sid = self.host_ref, self:sid(id)
    local h = size + 0.9
    local hover = self:hovered(x - 0.2, y - 0.1, size + 0.4, h)
    if opts.selected then self:rect(x - 0.2, y - 0.1, size + 0.4, h, kit.theme.row_selected)
    elseif hover then self:rect(x - 0.2, y - 0.1, size + 0.4, h, kit.theme.row_hover) end
    if hover then cursor("hand") end
    if opts.draw then                              -- your own drawing: draw(px, py, size_px)
        local px, py = self:px(x, y)
        opts.draw(px, py, size * self.u)
    elseif opts.item then
        local px, py = self:px(x, y)
        if ui.draw_block_icon then ui.draw_block_icon(picture, px, py, size * self.u) end
    elseif picture then
        self:image(x, y, size, size, picture)
    end
    if label then self:label(x - 0.6, y + size + 0.15, label, { size = 0.32, w = size + 1.2, align = "center" }) end
    self:tooltip(x, y, size, size, opts.tooltip)

    -- Being dragged (movable icons): follow the mouse until it's let go
    local d = host.icondrag
    if d and d.id == sid then
        d.seen = host.frame_no
        if not mouse_down() or host.released then
            host.icondrag = nil
            return d.active and "drop" or "click", d.mods
        end
        if not d.active and (math.abs(self.mx - d.sx) > 4 or math.abs(self.my - d.sy) > 4) then d.active = true end
        if d.active then
            cursor("move")
            local dx, dy = (self.mx - d.lx) / self.u, (self.my - d.ly) / self.u
            d.lx, d.ly = self.mx, self.my
            if dx ~= 0 or dy ~= 0 then return "drag", dx, dy, d.mods end
        end
        return nil
    end
    local c = self:take_click(x - 0.2, y - 0.1, size + 0.4, h, "any")
    if c then
        local mods = { ctrl = c.ctrl and true or false, shift = c.shift and true or false }
        if c.button == "right" then return "menu", mods end
        if (c.clicks or 1) >= 2 then return "open", mods end
        if opts.movable and c.button == "left" then
            host.icondrag = { id = sid, sx = c.x, sy = c.y, lx = c.x, ly = c.y, active = false, mods = mods, seen = host.frame_no }
        end
        return "select", mods
    end
    return nil
end

-- A box selection over an area (like dragging on an empty desktop). Call it AFTER the things
-- in the area (icons, buttons...), so a press on one of them goes to it, not to the box. Press
-- on empty space and drag: a highlighted box follows the mouse. Returns, every frame of the drag:
--   box, done, mods
--   box  = { x, y, w, h } in units (normalised: w, h never negative); nil when not selecting
--   done = true on the frame the mouse is let go; mods = { ctrl, shift } of the press
-- A press that isn't dragged returns a box of size 0 with done = true: "clicked on nothing"
-- (usually: clear the selection). Use kit.overlaps(box, x, y, w, h) to test what's inside.
-- opts.windows = true: windows the box touches become selected too (see f:selected_windows),
-- and a plain click on empty space clears the window selection.
function Frame:marquee(id, x, y, w, h, opts)
    opts = opts or {}
    local host, sid = self.host_ref, self:sid(id)
    local m = host.marquee
    if not (m and m.id == sid) then
        local c = self:take_click(x, y, w, h)
        if not c then return nil end
        local ux, uy = (c.x - self.cx) / self.u, (c.y - self.cy) / self.u
        m = { id = sid, x0 = ux, y0 = uy, x1 = ux, y1 = uy, sx = c.x, sy = c.y, active = false,
              mods = { ctrl = c.ctrl and true or false, shift = c.shift and true or false },
              base = opts.windows and not c.ctrl and {} or nil }
        if opts.windows then
            if not c.ctrl then host.win_selected = {} end
            m.keep = {}
            for wid in pairs(host.win_selected) do m.keep[wid] = true end   -- (Ctrl: add to these)
        end
        host.marquee = m
    end
    m.seen = host.frame_no
    local done = host.released or not mouse_down()
    m.x1 = math.max(x, math.min(x + w, (self.mx - self.cx) / self.u))
    m.y1 = math.max(y, math.min(y + h, (self.my - self.cy) / self.u))
    if not m.active and (math.abs(self.mx - m.sx) > 4 or math.abs(self.my - m.sy) > 4) then m.active = true end
    local box
    if m.active then
        box = { x = math.min(m.x0, m.x1), y = math.min(m.y0, m.y1), w = math.abs(m.x1 - m.x0), h = math.abs(m.y1 - m.y0) }
        -- drawn on top of everything (windows included), like a desktop's selection box
        local f, bx = self, box
        host.late[#host.late + 1] = function()
            local px, py = f:px(bx.x, bx.y)
            local pw, ph = bx.w * f.u, bx.h * f.u
            ui.draw_rect(px, py, pw, ph, color(kit.theme.marquee))
            if ui.draw_rect_outline then ui.draw_rect_outline(px, py, pw, ph, 1, color(kit.theme.marquee_edge))
            else
                ui.draw_rect(px, py, pw, 1, color(kit.theme.marquee_edge)); ui.draw_rect(px, py + ph - 1, pw, 1, color(kit.theme.marquee_edge))
                ui.draw_rect(px, py, 1, ph, color(kit.theme.marquee_edge)); ui.draw_rect(px + pw - 1, py, 1, ph, color(kit.theme.marquee_edge))
            end
        end
        if opts.windows then                     -- windows it touches (as it is now)
            local sel = {}
            for wid in pairs(m.keep or {}) do sel[wid] = true end
            for wid, st in pairs(host.windows) do
                -- (windows shown last frame too: the box is usually drawn before this frame's windows)
                if st.opts and st.opts.frame ~= false and not st.minimized and (st.seen or 0) >= host.frame_no - 1 then
                    local th = TITLE_H
                    if kit.overlaps(box, st.x, st.y, st.w, st.h + th) then sel[wid] = true end
                end
            end
            host.win_selected = sel
        end
    else
        box = { x = m.x0, y = m.y0, w = 0, h = 0 }
    end
    if done then host.marquee = nil end
    return box, done, m.mods
end

-- Does a box ({ x, y, w, h }) touch the rectangle x, y, w, h? (all in the same units)
function kit.overlaps(box, x, y, w, h)
    return box ~= nil and box.x < x + w and x < box.x + box.w and box.y < y + h and y < box.y + box.h
end

-- A set of selected things (icons, files, items...): yours to keep in your state.
--   local sel = kit.selection()
--   sel:set(id, true|false), sel:toggle(id), sel:has(id), sel:clear(), sel:only(id)
--   sel:count(), sel:list() (ids, sorted)
function kit.selection()
    local s = { items = {} }
    function s:has(id) return self.items[id] == true end
    function s:set(id, on) self.items[id] = on and true or nil end
    function s:toggle(id) self:set(id, not self:has(id)) end
    function s:clear() self.items = {} end
    function s:only(id) self.items = { [id] = true } end
    function s:count() local n = 0 for _ in pairs(self.items) do n = n + 1 end return n end
    function s:list()
        local out = {}
        for id in pairs(self.items) do out[#out + 1] = id end
        table.sort(out, function(a, b) return tostring(a) < tostring(b) end)
        return out
    end
    return s
end

-- A desktop's icon grid: like a real desktop, every icon has a cell of its own and icons never
-- overlap. Icons are dragged freely; when they're let go each snaps to the nearest FREE cell
-- (dropped on another icon: the nearest empty cell instead). Positions stay in your state as
-- positions[id] = { x = ..., y = ... } (the icon's top-left, in units), as f:icon wants them.
--   local grid = kit.icon_grid{ cell_w = 3, cell_h = 3 }      -- spacing (units)
--   grid:set_area(x, y, w, h)          every frame: the part of the desktop icons may use
--   grid:add(positions, id)            a new icon: the first free cell (down, then across)
--   grid:arrange(positions, moving)    after a drop: `moving` = the ids just dropped (a list);
--                                      also tidies icons left off the grid or outside the area
--   grid:preview(positions, moving)    where those would land now: { [id] = { x, y } } (to draw
--                                      outlines while dragging)
--   grid:keep_in(positions, skip)      icons outside a shrunken area move back in (skip: ids
--                                      being dragged; call every frame)
function kit.icon_grid(opts)
    opts = opts or {}
    local g = { cell_w = opts.cell_w or 3, cell_h = opts.cell_h or 3,
                x = 0, y = 0, cols = 1, rows = 1, pad_x = opts.pad_x or 1, pad_y = opts.pad_y or 0.8 }
    function g:set_area(x, y, w, h)
        self.x, self.y = x + self.pad_x, y + self.pad_y
        self.cols = math.max(1, math.floor((w - self.pad_x) / self.cell_w))
        self.rows = math.max(1, math.floor((h - self.pad_y) / self.cell_h))
    end
    function g:xy(col, row) return self.x + col * self.cell_w, self.y + row * self.cell_h end
    function g:cell(x, y)                     -- the cell nearest a position (in the grid)
        local col = math.floor((x - self.x) / self.cell_w + 0.5)
        local row = math.floor((y - self.y) / self.cell_h + 0.5)
        return math.max(0, math.min(self.cols - 1, col)), math.max(0, math.min(self.rows - 1, row))
    end
    local function key(c, r) return c .. ":" .. r end
    local function on_grid(self, p)           -- exactly in a cell, inside the area?
        local c, r = self:cell(p.x, p.y)
        local gx, gy = self:xy(c, r)
        return math.abs(gx - p.x) < 0.01 and math.abs(gy - p.y) < 0.01, c, r
    end
    -- The free cell nearest (col, row): rings outwards, nearest first
    function g:free_near(col, row, taken)
        local best, bd
        local maxr = math.max(self.cols, self.rows)
        for r = 0, maxr do
            for dc = -r, r do for dr = -r, r do
                if math.max(math.abs(dc), math.abs(dr)) == r then
                    local c, rr = col + dc, row + dr
                    if c >= 0 and c < self.cols and rr >= 0 and rr < self.rows and not taken[key(c, rr)] then
                        local d = (dc * self.cell_w) ^ 2 + (dr * self.cell_h) ^ 2
                        if not bd or d < bd then best, bd = { c, rr }, d end
                    end
                end
            end end
            if best and r * math.min(self.cell_w, self.cell_h) > math.sqrt(bd) then break end
        end
        return best and best[1], best and best[2]
    end
    local function sorted_ids(t)
        local ids = {}
        for id in pairs(t) do ids[#ids + 1] = id end
        table.sort(ids, function(a, b) return tostring(a) < tostring(b) end)
        return ids
    end
    -- The work: icons not moving keep their cells; the moving ones (and any off the grid) go to
    -- the free cell nearest where they are. Returns { [id] = { x, y } } for those.
    local function solve(self, positions, moving, skip)
        local is_moving, taken, out, pending = {}, {}, {}, {}
        for _, id in ipairs(moving or {}) do is_moving[id] = true end
        for _, id in ipairs(sorted_ids(positions)) do
            if not is_moving[id] and not (skip and skip[id]) then
                local ok, c, r = on_grid(self, positions[id])
                if ok and not taken[key(c, r)] then taken[key(c, r)] = id
                else pending[#pending + 1] = id end
            end
        end
        -- the moving ones first (in reading order: the one at the top-left gets its cell first)
        local order = {}
        for _, id in ipairs(moving or {}) do if positions[id] then order[#order + 1] = id end end
        table.sort(order, function(a, b)
            local pa, pb = positions[a], positions[b]
            if math.abs(pa.y - pb.y) > 0.01 then return pa.y < pb.y end
            return pa.x < pb.x
        end)
        for _, id in ipairs(pending) do order[#order + 1] = id end
        for _, id in ipairs(order) do
            local p = positions[id]
            local c, r = self:cell(p.x, p.y)
            c, r = self:free_near(c, r, taken)
            if c then
                taken[key(c, r)] = id
                local x, y = self:xy(c, r)
                out[id] = { x = x, y = y }
            end
        end
        return out
    end
    function g:arrange(positions, moving)
        for id, p in pairs(solve(self, positions, moving)) do positions[id].x, positions[id].y = p.x, p.y end
    end
    function g:preview(positions, moving) return solve(self, positions, moving) end
    function g:keep_in(positions, skip)
        local skipset = {}
        for _, id in ipairs(skip or {}) do skipset[id] = true end
        for id, p in pairs(solve(self, positions, {}, skipset)) do positions[id].x, positions[id].y = p.x, p.y end
    end
    function g:add(positions, id)
        local taken = {}
        for other, p in pairs(positions) do
            if other ~= id then local ok, c, r = on_grid(self, p) if ok then taken[key(c, r)] = true end end
        end
        for c = 0, self.cols - 1 do                -- down the first column, then the next
            for r = 0, self.rows - 1 do
                if not taken[key(c, r)] then
                    local x, y = self:xy(c, r)
                    positions[id] = { x = x, y = y }
                    return positions[id]
                end
            end
        end
        local x, y = self:xy(0, 0)                  -- (no room: on top of the first, as a last resort)
        positions[id] = { x = x, y = y }
        return positions[id]
    end
    return g
end

-- Windows that are selected (a box from f:marquee{ windows = true }, or Ctrl+click on a title
-- bar). Dragging the title bar of one of them moves them all together.
function Frame:selected_windows()
    local out = {}
    for id in pairs(self.host_ref.win_selected) do if self.host_ref.windows[id] then out[#out + 1] = id end end
    table.sort(out, function(a, b) return tostring(a) < tostring(b) end)
    return out
end
function Frame:select_window(id, on) self.host_ref.win_selected[id] = on ~= false and true or nil end
function Frame:clear_window_selection() self.host_ref.win_selected = {} end

-- Opens a menu at x, y (units): f:open_popup(id, x, y). Then every frame:
--   local choice = f:popup(id, { "Open", "Rename", "-", "Delete" })
-- returns the chosen entry's index (and closes), or nil. "-" is a separator. A click outside closes it.
function Frame:open_popup(id, x, y) self.host_ref.popups[self:sid(id)] = { x = x, y = y, cx = self.cx, cy = self.cy, u = self.u } end
function Frame:close_popup(id) self.host_ref.popups[self:sid(id)] = nil end
function Frame:popup(id, entries, opts)
    opts = opts or {}
    local host, sid = self.host_ref, self:sid(id)
    local p = host.popups[sid]
    if not p then return nil end
    local w = opts.width or 5
    local rows = #entries
    local h = rows * 0.9 + 0.2
    local choice = nil
    -- drawn on top (after windows), so it isn't hidden by them
    host.late[#host.late + 1] = function(top)
        local f = setmetatable({}, Frame)
        for k, v in pairs(top) do f[k] = v end
        f.cx, f.cy, f.u, f.owner, f.clip, f.prefix = p.cx, p.cy, p.u, "popup:" .. sid, nil, sid .. "/"
        local px, py = f:px(p.x, p.y)
        host.new_popup_rects[#host.new_popup_rects + 1] = { px, py, w * f.u, h * f.u, "popup:" .. sid }
        f:box(p.x, p.y, w, h, kit.theme.window, kit.theme.border)
        for i, e in ipairs(entries) do
            local ry = p.y + 0.1 + (i - 1) * 0.9
            if e == "-" then
                f:rect(p.x + 0.2, ry + 0.42, w - 0.4, 0.06, kit.theme.field_border)
            else
                if f:hovered(p.x, ry, w, 0.9) then f:rect(p.x, ry, w, 0.9, kit.theme.row_selected); cursor("hand") end
                f:label(p.x + 0.3, ry + 0.24, type(e) == "table" and e.text or e, { size = 0.36 })
                if f:take_click(p.x, ry, w, 0.9) then choice = i end
            end
        end
    end
    local result = host.popup_results[sid]
    host.popup_results[sid] = nil
    if result then host.popups[sid] = nil end
    host.popup_pending[#host.popup_pending + 1] = { sid = sid, get = function() return choice end }
    return result
end

-- ---------------------------------------------------------------------------
-- Windows
-- ---------------------------------------------------------------------------
-- f:window(id, opts, draw) -> still_open
--   opts: title, x, y, w, h (where it starts, units), min_w, min_h, resizable (true), closable
--   (true), minimizable (true), maximizable (true), frame (true: title bar and border; false for
--   taskbars and panels), movable (true), topmost (false: above every normal window),
--   background (colour)
--   draw(wf, win): wf is a frame inside the window; win = { w, h (content size, units), focused }
-- Returns false on the frame its close button is clicked: stop calling it to close the window.
function Frame:window(id, opts, draw)
    assert(not self.in_window, "windows can't be inside windows")
    local host = self.host_ref
    local st = host.windows[id]
    if not st then
        host.z = host.z + 1
        st = { x = opts.x or 1, y = opts.y or 1, w = opts.w or 8, h = opts.h or 6, z = host.z }
        host.windows[id] = st
        if opts.frame ~= false then host.focus_window = id end
    end
    st.seen = host.frame_no
    st.opts, st.draw = opts, draw
    if host.pending_max and host.pending_max[id] then st.maximized = true; st.minimized = false; host.pending_max[id] = nil end
    host.window_calls[#host.window_calls + 1] = id
    if st.closed_now then          -- its close button was clicked (last frame)
        st.closed_now = false
        return false
    end
    return true
end

-- Which windows exist, for a taskbar: { { id, title, minimized, focused }, ... } (oldest first)
function Frame:window_list()
    local host, out = self.host_ref, {}
    for id, st in pairs(host.windows) do
        if st.seen and st.seen >= host.frame_no - 1 and not (st.opts and st.opts.frame == false) then
            out[#out + 1] = { id = id, title = st.opts and st.opts.title or id, minimized = st.minimized,
                              focused = host.focus_window == id and not st.minimized, z = st.z }
        end
    end
    table.sort(out, function(a, b) return tostring(a.id) < tostring(b.id) end)
    return out
end
function Frame:focus_window(id)
    local host, st = self.host_ref, self.host_ref.windows[id]
    if not st then return end
    st.minimized = false
    host.z = host.z + 1
    st.z = host.z
    host.focus_window = id
end
function Frame:minimize_window(id)
    local host = self.host_ref
    local st = host.windows[id]
    if st then st.minimized = true; if host.focus_window == id then host.focus_window = nil end end
end
-- Where a window is and how it is: { x, y, w, h, maximized, minimized, focused } (units), or nil
-- if it isn't open. For saving a layout, or noticing what the player did with a window.
function Frame:window_state(id)
    local host = self.host_ref
    local st = host.windows[id]
    if not st then return nil end
    return { x = st.x, y = st.y, w = st.w, h = st.h, maximized = st.maximized == true,
             minimized = st.minimized == true, focused = host.focus_window == id }
end

-- Maximizes (or with on = false, restores) a window. Works on the frame you open it in, too.
function Frame:maximize_window(id, on)
    local host = self.host_ref
    if on == false then
        local st = host.windows[id]
        if st then st.maximized = false end
        host.pending_max = host.pending_max or {}
        host.pending_max[id] = nil
        return
    end
    host.pending_max = host.pending_max or {}
    host.pending_max[id] = true           -- (applied as soon as the window exists)
end
function Frame:move_window(id, x, y, w, h)
    local st = self.host_ref.windows[id]
    if st then st.x, st.y = x or st.x, y or st.y; st.w, st.h = w or st.w, h or st.h end
end
-- Where maximized windows go (units of this frame); e.g. leave room for a taskbar
function Frame:set_work_area(x, y, w, h) self.host_ref.work = { x, y, w, h } end

local TITLE = 1.0
local EDGE = 0.35

-- Pixel rectangle of a window (with its title bar)
local function window_rect(host, st, base)
    local x, y, w, h = st.x, st.y, st.w, st.h
    if st.maximized then
        local wa = host.work or { 0, 0, base.w_units, base.h_units }
        x, y, w = wa[1], wa[2], wa[3]
        h = wa[4] - ((st.opts and st.opts.frame == false) and 0 or TITLE)
    end
    local framed = not (st.opts and st.opts.frame == false)
    local th = framed and TITLE or 0
    return base.cx + x * base.u, base.cy + y * base.u, w * base.u, (h + th) * base.u, x, y, w, h, th
end

local function draw_windows(host, base)
    -- in stacking order: normal windows by z, then topmost ones by z
    local list = {}
    for _, id in ipairs(host.window_calls) do list[#list + 1] = id end
    table.sort(list, function(a, b)
        local A_, B_ = host.windows[a], host.windows[b]
        local ta, tb = A_.opts.topmost and 1 or 0, B_.opts.topmost and 1 or 0
        if ta ~= tb then return ta < tb end
        return A_.z < B_.z
    end)
    local rects = {}
    local u = base.u
    for _, id in ipairs(list) do
        local st = host.windows[id]
        local o = st.opts
        if not st.minimized then
            local px, py, pw, ph, x, y, w, h, th = window_rect(host, st, base)
            local framed = o.frame ~= false
            rects[#rects + 1] = { px, py, pw, ph, "win:" .. id }
            local wf = setmetatable({}, Frame)
            for k, v in pairs(base) do wf[k] = v end
            wf.owner, wf.prefix, wf.in_window = "win:" .. id, tostring(id) .. "/", true
            wf.cx, wf.cy = px, py + th * u
            wf.clip = { px, py + th * u, pw, ph - th * u }
            local focused = host.focus_window == id
            -- any click in the window brings it to the front
            local press = base.input.press
            if press and not press.used and host:owner_at(press.x, press.y) == "win:" .. id and st.raised_for ~= press then
                st.raised_for = press
                if not o.topmost then host.z = host.z + 1; st.z = host.z end
                if framed then host.focus_window = id end    -- panels (taskbars) don't take the focus
            end
            -- frame: shadow, border, title bar, buttons
            if framed then
                ui.draw_rect(px + 4, py + 4, pw, ph, color(kit.theme.window_shadow))
                if host.win_selected[id] then        -- selected: a bright edge (f:marquee, Ctrl+click)
                    ui.draw_rect(px - 3, py - 3, pw + 6, ph + 6, color(kit.theme.selected_edge))
                end
                ui.draw_rect(px - 1, py - 1, pw + 2, ph + 2, color(kit.theme.border))
                ui.draw_rect(px, py, pw, th * u, color(focused and kit.theme.window_title or kit.theme.window_title_idle))
                local tf = setmetatable({}, Frame)
                for k, v in pairs(wf) do tf[k] = v end
                tf.cx, tf.cy, tf.clip = px, py, { px, py, pw, th * u }
                local bx = w
                local function title_button(kind)
                    bx = bx - 0.9
                    local hover = tf:hovered(bx, 0.1, 0.8, 0.8)
                    if hover then tf:rect(bx, 0.1, 0.8, 0.8, kind == "close" and { 0.8, 0.25, 0.25, 1 } or { 1, 1, 1, 0.15 }); cursor("hand") end
                    local cx, cy = tf:px(bx + 0.4, 0.5)
                    local r = 0.2 * u
                    if kind == "close" then
                        ui.draw_line(cx - r, cy - r, cx + r, cy + r, math.max(1.5, u * 0.06), 1, 1, 1, 1)
                        ui.draw_line(cx - r, cy + r, cx + r, cy - r, math.max(1.5, u * 0.06), 1, 1, 1, 1)
                    elseif kind == "max" then
                        if ui.draw_rect_outline then ui.draw_rect_outline(cx - r, cy - r, r * 2, r * 2, math.max(1.5, u * 0.05), 1, 1, 1, 1) end
                    else
                        ui.draw_rect(cx - r, cy + r * 0.6, r * 2, math.max(1.5, u * 0.06), 1, 1, 1, 1)
                    end
                    return tf:take_click(bx, 0.1, 0.8, 0.8) ~= nil
                end
                if o.closable ~= false and title_button("close") then st.closed_now = true end
                if o.maximizable ~= false and title_button("max") then st.maximized = not st.maximized end
                if o.minimizable ~= false and title_button("min") then st.minimized = true; if host.focus_window == id then host.focus_window = nil end end
                if o.title then                                      -- (cut off before the buttons)
                    if ui.push_clip then ui.push_clip(px, py, math.max(0, (bx - 0.1) * u), th * u) end
                    tf:label(0.35, 0.3, o.title, { size = 0.4, color = { 1, 1, 1 } })
                    if ui.pop_clip then ui.pop_clip() end
                end
                -- dragging by the title bar (double-click: maximize)
                local tp = tf:take_click(0, 0, bx, TITLE)
                if tp then
                    if tp.ctrl then                  -- Ctrl+click: add to / take out of the selection
                        host.win_selected[id] = not host.win_selected[id] or nil
                    elseif (tp.clicks or 1) >= 2 and o.maximizable ~= false then st.maximized = not st.maximized
                    elseif o.movable ~= false and not st.maximized then
                        -- One of several selected windows: they all move. Another window: the
                        -- selection is let go and this one moves alone.
                        if not host.win_selected[id] then host.win_selected = {} end
                        local group = {}
                        for gid in pairs(host.win_selected) do
                            local g = host.windows[gid]
                            if gid ~= id and g and not g.maximized and not (g.opts and g.opts.movable == false) then
                                group[gid] = { x = g.x, y = g.y }
                            end
                        end
                        host.wdrag = { id = id, mode = "move", ox = tp.x - px, oy = tp.y - py,
                                       sx0 = st.x, sy0 = st.y, group = group }
                    end
                end
                if tf:hovered(0, 0, bx, TITLE) and o.movable ~= false and not st.maximized then cursor("move") end
            end
            -- resizing from the right edge, bottom edge or corner
            if framed and o.resizable ~= false and not st.maximized then
                local ef = setmetatable({}, Frame)
                for k, v in pairs(wf) do ef[k] = v end
                ef.cx, ef.cy, ef.clip = px, py, nil
                local W_, H_ = w, h + th
                local corner = ef:hovered(W_ - EDGE, H_ - EDGE, EDGE + 0.1, EDGE + 0.1)
                local right = ef:hovered(W_ - EDGE, TITLE, EDGE + 0.1, H_ - TITLE)
                local bottom = ef:hovered(0, H_ - EDGE, W_, EDGE + 0.1)
                if corner then cursor("resize_nwse") elseif right then cursor("resize_h") elseif bottom then cursor("resize_v") end
                local mode = corner and "corner" or right and "right" or bottom and "bottom" or nil
                if mode and ef:take_click(0, 0, W_ + 0.1, H_ + 0.1) then
                    host.wdrag = { id = id, mode = mode, sx = base.mx, sy = base.my, sw = st.w, sh = st.h }
                end
            end
            -- the window's contents
            ui.draw_rect(px, py + th * u, pw, ph - th * u, color(o.background or kit.theme.window))
            if ui.push_clip then ui.push_clip(wf.clip[1], wf.clip[2], wf.clip[3], wf.clip[4]) end
            local ok, err = pcall(st.draw, wf, { w = w, h = h, focused = focused, id = id })
            if ui.pop_clip then ui.pop_clip() end
            if not ok then print("[ui_kit] window " .. tostring(id) .. ": " .. tostring(err)) end
        end
    end
    -- moving / resizing in progress
    local wd = host.wdrag
    if wd then
        local st = host.windows[wd.id]
        if not st or not mouse_down() then host.wdrag = nil
        else
            local o = st.opts
            if wd.mode == "move" then
                cursor("move")
                st.x = (base.mx - wd.ox - base.cx) / u
                st.y = (base.my - wd.oy - base.cy) / u
                -- keep the title bar reachable
                st.x = math.max(-st.w + 2, math.min(base.w_units - 2, st.x))
                st.y = math.max(0, math.min(base.h_units - 1, st.y))
                -- the other selected windows keep their places relative to this one
                local dx, dy = st.x - wd.sx0, st.y - wd.sy0
                for gid, start in pairs(wd.group or {}) do
                    local g = host.windows[gid]
                    if g then
                        g.x = math.max(-g.w + 2, math.min(base.w_units - 2, start.x + dx))
                        g.y = math.max(0, math.min(base.h_units - 1, start.y + dy))
                    end
                end
            else
                local dx, dy = (base.mx - wd.sx) / u, (base.my - wd.sy) / u
                if wd.mode == "right" or wd.mode == "corner" then st.w = math.max(o.min_w or 4, wd.sw + dx) end
                if wd.mode == "bottom" or wd.mode == "corner" then st.h = math.max(o.min_h or 2, wd.sh + dy) end
                cursor(wd.mode == "right" and "resize_h" or wd.mode == "bottom" and "resize_v" or "resize_nwse")
            end
        end
    end
    host.window_rects = rects
end

-- ---------------------------------------------------------------------------
-- Hosts: a form (the mod-screen slot) or an embed (inside any screen)
-- ---------------------------------------------------------------------------
local Host = {}
Host.__index = Host

local function new_host(fields)
    local h = setmetatable(fields or {}, Host)
    h.scrolls, h.windows, h.popups, h.popup_results = {}, {}, {}, {}
    h.z, h.frame_no = 0, 0
    h.window_rects, h.popup_rects = {}, {}
    h.win_selected = {}            -- windows selected (f:marquee, Ctrl+click on a title): move together
    return h
end

-- What's on top at this pixel: a popup, a window ("win:<id>"), or "base"
function Host:owner_at(px, py)
    for i = #self.popup_rects, 1, -1 do
        local r = self.popup_rects[i]
        if inside(px, py, r[1], r[2], r[3], r[4]) then return r[5] end
    end
    for i = #self.window_rects, 1, -1 do
        local r = self.window_rects[i]
        if inside(px, py, r[1], r[2], r[3], r[4]) then return r[5] end
    end
    return "base"
end

-- One frame of a host, drawn in the pixel rectangle x, y, w, h. units_w/h: the content size in
-- units (nil: use `unit` pixels per unit)
function Host:run(x, y, w, h, units_w, units_h, unit, draw, state, has_input)
    self.frame_no = self.frame_no + 1
    local u = unit
    if units_w and units_h then u = math.max(6, math.min(64, math.floor(math.min(w / units_w, h / units_h)))) end
    u = u or 32
    local mx, my = ui.get_mouse()
    local input = gather_input(has_input, self)
    local f = setmetatable({
        host_ref = self, u = u, cx = x, cy = y, mx = mx, my = my, input = input, owner = "base", prefix = "",
        clip = nil, state = state, w_units = w / u, h_units = h / u, W = w, H = h,
    }, Frame)
    self.fields, self.window_calls, self.late, self.popup_pending = {}, {}, {}, {}
    self.tip, self.clicked_field, self.tab = nil, nil, nil
    self.released = false
    for _, r in ipairs(input.releases) do if r.button == "left" then self.released = true; self.dropped_at = r end end
    if not queues() and self.dnd and not mouse_down() then self.released = true; self.dropped_at = { x = mx, y = my } end
    if not self.released then self.dropped_at = nil end
    -- the popups' rectangles are rebuilt as they're drawn; a press outside every popup closes them
    local press = input.press
    if press and next(self.popups) and self:owner_at(press.x, press.y):sub(1, 6) ~= "popup:" then self.popups = {} end
    self.new_popup_rects = {}       -- (last frame's stay in use until this frame is drawn)

    if kit._clear_clips then kit._clear_clips() end
    local ok, err = pcall(draw, f, state)
    -- A dragged icon or a selection box that wasn't drawn this frame (it went away): forget it
    if self.icondrag and self.icondrag.seen ~= self.frame_no then self.icondrag = nil end
    if self.marquee and self.marquee.seen ~= self.frame_no then self.marquee = nil end
    if not ok then
        f:label(0, 0, "This screen has an error - see the console", { color = { 1, 0.5, 0.5 } })
        print("[ui_kit] " .. tostring(self.id) .. ": " .. tostring(err))
    end
    -- windows that are no longer asked for disappear
    for id, st in pairs(self.windows) do if st.seen ~= self.frame_no then self.windows[id] = nil end end
    if #self.window_calls > 0 then draw_windows(self, f) else self.window_rects = {} end
    for _, fn in ipairs(self.late) do fn(f) end                 -- popups, on top of windows
    self.popup_rects = self.new_popup_rects
    for _, p in ipairs(self.popup_pending) do
        local c = p.get()
        if c then self.popup_results[p.sid] = c end
    end

    -- drag & drop: the ghost follows the mouse; a release ends it (dropped or not)
    local d = self.dnd
    if d then
        if d.active then
            local gx, gy = mx - d.ox, my - d.oy
            if d.ghost then d.ghost(gx, gy, u) else ui.draw_rect(gx, gy, d.w, d.h, 1, 1, 1, 0.35) end
        end
        if self.released or (not queues() and not mouse_down()) then self.dnd = nil end
    end
    -- focus: Tab -> the next field; a click elsewhere leaves the field
    if self.tab and #self.fields > 0 then
        for i, id in ipairs(self.fields) do if id == self.tab then self.focus = self.fields[i % #self.fields + 1] break end end
    end
    if press and self.clicked_field == nil then self.focus = nil end
    -- tooltip, last
    if self.tip then
        local s = text_scale(u, 0.36)
        local tw = (ui.text_width and ui.text_width(self.tip, s) or 0) + 12
        local th = 0.36 * u + 12
        local tx, ty = math.min(mx + 14, x + w - tw - 2), math.min(my + 18, y + h - th - 2)
        ui.draw_rect(tx, ty, tw, th, color(kit.theme.tooltip))
        ui.draw_text(self.tip, tx + 6, ty + 6 + 0.36 * u, s, 1, 1, 1, 1)
    end
    return f
end

-- ---------------------------------------------------------------------------
-- Forms (the engine's mod-screen slot: frees the mouse, E / Escape close it)
-- ---------------------------------------------------------------------------
local Form = {}
Form.__index = Form

-- kit.form{ id, width, height, title, draw, on_open, on_close, dim, padding,
--           close_on_click_outside, fullscreen }
--   fullscreen = true: the form is the whole window (a desktop) - no panel, no title
function kit.form(def)
    assert(type(def) == "table" and type(def.draw) == "function", "kit.form needs { draw = function(f, state) ... }")
    local form = setmetatable({
        id = def.id or tostring(def), width = def.width or 10, height = def.height or 6, title = def.title,
        draw = def.draw, on_open = def.on_open, on_close = def.on_close, dim = def.dim ~= false,
        padding = def.padding or 0.5, close_on_click_outside = def.close_on_click_outside,
        fullscreen = def.fullscreen, unit = def.unit, state = {},
    }, Form)
    form.host = new_host({ id = form.id, form = form })
    return form
end

function Form:is_open() return current == self end

function Form:open(state)
    if current and current ~= self then current:close() end
    self.state = state or self.state or {}
    self.host = new_host({ id = self.id, form = self })
    legacy = { presses = {}, scroll = 0, chars = {}, keys = {} }
    current = self
    if engine and engine.set_inventory_open then engine.set_inventory_open(true) end
    if self.on_open then self.on_open(self.state) end
    return self
end

function Form:close()
    if current ~= self then return end
    current = nil
    if engine and engine.set_inventory_open and (not engine.is_inventory_open or engine.is_inventory_open()) then
        engine.set_inventory_open(false)
    end
    if self.on_close then self.on_close(self.state) end
end

local function render_form(form, W, H)
    local host = form.host
    if form.fullscreen then
        if form.dim then ui.draw_rect(0, 0, W, H, color(kit.theme.dim)) end
        local u = form.unit or math.max(16, math.min(48, math.floor(math.min(W / 40, H / 22.5))))
        host:run(0, 0, W, H, nil, nil, u, form.draw, form.state, true)
        return
    end
    local pad = form.padding
    local title_h = form.title and 1.1 or 0
    local total_w, total_h = form.width + pad * 2, form.height + pad * 2 + title_h
    local u = math.floor(math.min(W * 0.92 / total_w, H * 0.88 / total_h))
    u = math.max(8, math.min(u, 64))
    local panel_x = math.floor((W - total_w * u) / 2)
    local panel_y = math.floor((H - total_h * u) / 2)
    if form.dim then ui.draw_rect(0, 0, W, H, color(kit.theme.dim)) end
    ui.draw_rect(panel_x - 2, panel_y - 2, total_w * u + 4, total_h * u + 4, color(kit.theme.border))
    ui.draw_rect(panel_x, panel_y, total_w * u, total_h * u, color(kit.theme.panel))
    local cx, cy = panel_x + pad * u, panel_y + (pad + title_h) * u
    if form.title then
        local s = text_scale(u, 0.55)
        ui.draw_text(form.title, cx, cy - title_h * u + 0.15 * u + 0.55 * u, s, color(kit.theme.title))
    end
    -- a press outside the panel
    local f = host:run(cx, cy, form.width * u, form.height * u, nil, nil, u, form.draw, form.state, true)
    local p = f.input.press
    if current == form and p and not p.used and form.close_on_click_outside and
       not inside(p.x, p.y, panel_x, panel_y, total_w * u, total_h * u) then
        form:close()
    end
end

-- ---------------------------------------------------------------------------
-- Embeds: kit widgets inside ANY screen (HUD, creative inventory, a menu)
-- ---------------------------------------------------------------------------
-- local panel = kit.embed{ id = "mymod:panel", width = 10, height = 6, draw = function(f, state) ... end }
-- Then in any render function:  panel:draw(x, y, w, h)  (pixels)
-- width/height (units) are fitted into the rectangle; leave them out and give `unit` (pixels per
-- unit) instead. It reads input only when `input` isn't false and no kit form is open.
function kit.embed(def)
    assert(type(def) == "table" and type(def.draw) == "function", "kit.embed needs { draw = function(f, state) ... }")
    local e = { id = def.id or tostring(def), width = def.width, height = def.height, unit = def.unit,
                draw_fn = def.draw, state = def.state or {}, input = def.input ~= false,
                host = new_host({ id = def.id, embed = true }) }
    function e:draw(x, y, w, h)
        local has_input = self.input and current == nil
        return self.host:run(x, y, w, h, self.width, self.height, self.unit, self.draw_fn, self.state, has_input)
    end
    -- Is a text field in it being typed in? (Then the screen around it shouldn't use the keys:
    -- E shouldn't close it, letters shouldn't go to its own search box.)
    function e:is_typing() return self.host.focus ~= nil end
    -- Leave the text field (e.g. on Escape) and close any open menu
    function e:stop_typing() self.host.focus = nil; self.host.popups = {} end
    -- Forget windows, scroll positions and focus (e.g. when the screen it's in closes)
    function e:reset() self.host = new_host({ id = self.id, embed = true }) end
    return e
end

-- ---------------------------------------------------------------------------
-- Engine hooks (installed once; each passes on to what was there before)
-- ---------------------------------------------------------------------------
if not kit._installed then
    kit._installed = true

    local previous_render = render_inventory
    function render_inventory(W, H, ...)
        if current then
            if engine.is_inventory_open and not engine.is_inventory_open() then
                local f = current
                current = nil
                if f.on_close then f.on_close(f.state) end
            else
                render_form(current, W, H)
                return
            end
        end
        if previous_render then return previous_render(W, H, ...) end
    end

    local previous_click = handle_inventory_click
    function handle_inventory_click(mx, my, W, H, button, shift, ...)
        if current then
            if not queues() then
                legacy.presses[#legacy.presses + 1] = { x = mx, y = my, button = button or "left", shift = shift and true or false, clicks = 1 }
            end
            return true
        end
        if previous_click then return previous_click(mx, my, W, H, button, shift, ...) end
        return false
    end

    local previous_key = handle_inventory_key
    function handle_inventory_key(key, action, mods, ...)
        if current then
            if current.host.focus then
                if key == K.ESCAPE then
                    if action == A.PRESS then current.host.focus = nil end
                    return true
                end
                if not queues() then legacy.keys[#legacy.keys + 1] = { key = key, action = action, mods = mods } end
                return true
            end
            return false
        end
        if previous_key then return previous_key(key, action, mods, ...) end
        return false
    end

    local previous_scroll = handle_inventory_scroll
    function handle_inventory_scroll(dy, ...)
        if current then
            if not queues() then legacy.scroll = legacy.scroll + dy end
            return
        end
        if previous_scroll then return previous_scroll(dy, ...) end
    end

    local previous_toggle = toggle_main_inventory
    function toggle_main_inventory(...)
        if current then current:close() return false end
        if previous_toggle then return previous_toggle(...) end
        return false
    end

    local previous_char = on_char_input
    function on_char_input(codepoint, ...)
        if current and current.host.focus then
            if not queues() then legacy.chars[#legacy.chars + 1] = char_of(codepoint) end
            return
        end
        if previous_char then return previous_char(codepoint, ...) end
    end
end

function kit.current() return current end

return kit
