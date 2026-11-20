-- textedit: a text box's caret, for any screen that draws its own text fields.
--
--   local te = require("textedit")
--
-- Each box has an id (any string). The text itself stays wherever your screen keeps it; this
-- module keeps where the caret is (and how far long text is scrolled), and does the editing:
--
--   te.focus(id, text [, rel_x, scale, mask])   the box was clicked: caret at the click (rel_x =
--                                               pixels from where the text starts), or at the end
--   text, used = te.key(id, text, key, action, mods [, opts])
--        Left / Right, Home / End, Backspace / Delete; with Ctrl: by whole words. `mods` is the
--        number key hooks get (or true/false for Ctrl). used = false: not an editing key.
--   text = te.insert(id, text, str [, max_chars])  typed characters, at the caret
--   shown, caret_x = te.view(id, text, width_px, scale [, mask])
--        the part of the text to draw in a box `width_px` wide (long text scrolls so the caret
--        stays in view), and the caret's x from where `shown` starts. mask = "*" for passwords.
--   te.draw_caret(id, x, y, h [, r, g, b, a])    the blinking caret (after the screen's text)
--   te.reset(id)                                 forget the box (caret back to the end)
--
-- Works in characters, not bytes, so accented letters and other UTF-8 text edit correctly.

local te = {}
local boxes = {}            -- id -> { caret = chars from the start, first = first char shown, t = last change }

local function len(s) return utf8.len(s) or #s end
local function sub(s, i, j)                     -- characters i+1 .. j (0-based i, like a caret)
    local n = len(s)
    j = math.min(j or n, n)
    if i >= j then return "" end
    local a = utf8.offset(s, i + 1) or (#s + 1)
    local b = (utf8.offset(s, j + 1) or (#s + 1)) - 1
    return s:sub(a, b)
end
local function width(s, scale)
    if ui and ui.text_width then return ui.text_width(s, scale) end
    return len(s) * 8 * scale
end
local function now() return ui and ui.get_time and ui.get_time() or 0 end

local function box(id, text)
    local b = boxes[id]
    if not b then
        b = { caret = len(text), first = 0, t = now() }
        boxes[id] = b
    end
    b.caret = math.max(0, math.min(b.caret, len(text)))
    return b
end

function te.reset(id) boxes[id] = nil end
function te.caret(id, text) return box(id, text).caret end
function te.set_caret(id, text, pos) local b = box(id, text) b.caret = math.max(0, math.min(pos, len(text))) b.t = now() end

local function shown_text(text, mask)
    if mask then return string.rep(mask, len(text)) end
    return text
end

-- A click: the caret goes to the character boundary nearest the click (or the end)
function te.focus(id, text, rel_x, scale, mask)
    local b = box(id, text)
    b.t = now()
    if not rel_x then b.caret = len(text) return end
    local s = shown_text(text, mask)
    local n = len(s)
    local best, best_d = n, math.huge
    for c = b.first, n do
        local d = math.abs(width(sub(s, b.first, c), scale or 1) - rel_x)
        if d < best_d then best, best_d = c, d end
    end
    b.caret = best
end

local function is_space(ch) return ch == " " or ch == "\t" end
local function word_left(text, c)
    while c > 0 and is_space(sub(text, c - 1, c)) do c = c - 1 end
    while c > 0 and not is_space(sub(text, c - 1, c)) do c = c - 1 end
    return c
end
local function word_right(text, c)
    local n = len(text)
    while c < n and is_space(sub(text, c, c + 1)) do c = c + 1 end
    while c < n and not is_space(sub(text, c, c + 1)) do c = c + 1 end
    return c
end

-- An editing key. Returns the (maybe changed) text and whether the key was used.
function te.key(id, text, key, action, mods)
    local K = ui and ui.keys or {}
    local A = ui and ui.actions or { PRESS = 1, REPEAT = 2 }
    if action ~= nil and action ~= A.PRESS and action ~= A.REPEAT and action ~= "press" and action ~= "repeat" then
        return text, false
    end
    local ctrl = mods == true or (type(mods) == "number" and math.floor(mods / 2) % 2 == 1)
    local b = box(id, text)
    local c, n = b.caret, len(text)
    if key == K.LEFT then c = ctrl and word_left(text, c) or math.max(0, c - 1)
    elseif key == K.RIGHT then c = ctrl and word_right(text, c) or math.min(n, c + 1)
    elseif key == K.HOME then c = 0
    elseif key == K.END then c = n
    elseif key == K.BACKSPACE then
        if c > 0 then
            local from = ctrl and word_left(text, c) or c - 1
            text = sub(text, 0, from) .. sub(text, c)
            c = from
        end
    elseif key == K.DELETE then
        if c < n then
            local to = ctrl and word_right(text, c) or c + 1
            text = sub(text, 0, c) .. sub(text, to)
        end
    else
        return text, false
    end
    b.caret = c
    b.t = now()
    return text, true
end

-- Typed text, at the caret. max_chars: the most the box may hold (characters).
function te.insert(id, text, str, max_chars)
    if not str or str == "" then return text end
    local b = box(id, text)
    if max_chars then
        local room = max_chars - len(text)
        if room <= 0 then return text end
        if len(str) > room then str = sub(str, 0, room) end
    end
    text = sub(text, 0, b.caret) .. str .. sub(text, b.caret)
    b.caret = b.caret + len(str)
    b.t = now()
    return text
end

-- What to draw: the visible part of the text, and the caret's x within it
function te.view(id, text, width_px, scale, mask)
    local b = box(id, text)
    local s = shown_text(text, mask)
    local n = len(s)
    scale = scale or 1
    if b.first > b.caret then b.first = b.caret end
    -- scroll right until the caret fits
    while b.first < b.caret and width(sub(s, b.first, b.caret), scale) > width_px do b.first = b.first + 1 end
    -- ...and back left while there's room (after deleting, or for short text)
    while b.first > 0 and width(sub(s, b.first - 1, n), scale) <= width_px do b.first = b.first - 1 end
    local last = n
    while last > b.caret and width(sub(s, b.first, last), scale) > width_px do last = last - 1 end
    return sub(s, b.first, last), width(sub(s, b.first, b.caret), scale)
end

-- Is the caret showing right now? (blinks; always on just after typing or moving it)
function te.visible(id)
    local b = boxes[id]
    local t = now()
    if b and t - b.t < 0.5 then return true end
    return (t % 1.0) < 0.5
end

-- Draws the caret: a thin bar from y, h pixels tall
function te.draw_caret(id, x, y, h, r, g, b, a)
    if not te.visible(id) then return end
    local w = math.max(1, math.floor(h / 12 + 0.5))
    ui.draw_rect(x, y, w, h, r or 1, g or 1, b or 1, a or 1)
end

return te
