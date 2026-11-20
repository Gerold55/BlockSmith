-- UI Kit Showcase: everything lualib/ui_kit.lua can do, in one mod you can read and copy from.
--
--   Open it: /uikit, F7, right-click with the UI Kit Remote (an item), right-click a Showcase
--   Laptop (a block), or the buttons on the "UI Kit" creative tab. It opens a desktop with a Tour
--   window (a checklist of things to try, ticked off as you do them) and an app for each feature:
--     Widgets     labels, wrapped text, buttons, checkboxes, sliders, text fields, lists, tooltips
--     Inventory   drag and drop between a chest and your real hotbar: stacking, splitting,
--                 Shift+click, a trash can that only appears while you drag
--     Shapes      the engine's drawing: circles, rings, pies, lines, polygons, gradients,
--                 rotating and tinted pictures, clipping
--     Scrolling   a scroll area full of widgets
--     Notes       text fields with copy and paste (Ctrl+C / Ctrl+X / Ctrl+V)
--   ...in windows you can move, resize, stack, minimize, maximize and close, with desktop icons,
--   right-click menus, a taskbar with a Start menu and a clock.
--
--   In the creative inventory, two tabs of its own in the sidebar:
--     "UI Showcase"   this mod's blocks (creative.register_tab with nothing else: the mod's items)
--     "UI Kit"        a whole page drawn with ui_kit (creative.register_tab{ draw = ... })
--
-- See docs/MODDING.md, sections 14.5 and 14.6.

local kit = require("ui_kit")
local MOD = "ui_showcase"

-- ===========================================================================
-- Blocks (so the creative tab has something of its own to show)
-- ===========================================================================

engine.register_block("showcase_laptop", 1, 1, 1, "ui_showcase_laptop.png", nil, { hardness = 1 })
engine.register_block("showcase_crate", 1, 1, 1, "ui_showcase_crate.png", nil, { hardness = 2, tool = "axe" })
-- An item (a flat picture, never placed): right-click with it anywhere to open the showcase
engine.register_block("showcase_remote", 1, 1, 1, "ui_showcase_remote.png", nil,
                      { inventory_image = "ui_showcase_remote.png" })

-- The tour: things to try, ticked off as the player does them (see the Tour app)
local tour = {
    { id = "move",     text = "Move a window: drag its title bar" },
    { id = "resize",   text = "Resize a window: drag its right edge, bottom edge or corner" },
    { id = "maximize", text = "Maximize a window: the square button, or double-click the title" },
    { id = "restore",  text = "Minimize a window (_), then bring it back from the taskbar" },
    { id = "front",    text = "Open two apps and click the back one to bring it to the front" },
    { id = "close",    text = "Close a window with its x button" },
    { id = "icon",     text = "Double-click a desktop icon to open an app" },
    { id = "menu",     text = "Right-click a desktop icon for its menu" },
    { id = "drag",     text = "Drag an item to another slot in the Inventory app" },
    { id = "scroll",   text = "Scroll the Scrolling app (mouse wheel, or drag its bar)" },
    { id = "type",     text = "Type in a text field (Widgets or Notes) and press Enter" },
    { id = "box",      text = "Drag a box on the empty desktop to select several icons (Ctrl+click adds one)" },
    { id = "icons",    text = "Drag a selected icon: all the selected icons move together" },
    { id = "windows",  text = "Select two windows (box, or Ctrl+click their titles) and drag one: both move" },
}
local done = {}
local function did(what) done[what] = true end

-- ===========================================================================
-- Small helpers
-- ===========================================================================

local function pretty(name)
    return (name:gsub("_", " "):gsub("^%l", string.upper))
end

-- Some real items of this game, for the chest (whatever game it's played in)
local function some_items(n)
    local out = {}
    for _, name in ipairs(ui.get_all_blocks and ui.get_all_blocks() or {}) do
        if not name:find("water") and not name:find("lava") and name ~= "air" then
            out[#out + 1] = name
            if #out >= n then break end
        end
    end
    return out
end

-- Little pictures for the desktop icons, drawn with shapes (px, py: top-left; s: size, pixels)
local icon_art = {
    widgets = function(px, py, s)
        ui.draw_rounded_rect(px, py + s * 0.15, s, s * 0.7, s * 0.12, 0.25, 0.55, 0.95, 1)
        ui.draw_rounded_rect(px + s * 0.12, py + s * 0.32, s * 0.45, s * 0.14, s * 0.05, 1, 1, 1, 1)
        ui.draw_circle(px + s * 0.75, py + s * 0.5, s * 0.12, 1, 0.85, 0.3)
        ui.draw_rect(px + s * 0.12, py + s * 0.56, s * 0.3, s * 0.08, 1, 1, 1, 0.7)
    end,
    inventory = function(px, py, s)
        ui.draw_rounded_rect(px + s * 0.05, py + s * 0.25, s * 0.9, s * 0.65, s * 0.08, 0.55, 0.38, 0.2, 1)
        ui.draw_rect(px + s * 0.05, py + s * 0.45, s * 0.9, s * 0.06, 0.3, 0.2, 0.1, 1)
        ui.draw_rounded_rect(px + s * 0.4, py + s * 0.38, s * 0.2, s * 0.2, s * 0.04, 1, 0.85, 0.3, 1)
    end,
    shapes = function(px, py, s)
        ui.draw_circle(px + s * 0.3, py + s * 0.35, s * 0.25, 0.95, 0.35, 0.35)
        ui.draw_triangle(px + s * 0.7, py + s * 0.1, px + s, py + s * 0.6, px + s * 0.4, py + s * 0.6, 0.35, 0.85, 0.45)
        ui.draw_rounded_rect(px + s * 0.15, py + s * 0.62, s * 0.7, s * 0.3, s * 0.08, 0.35, 0.55, 0.95, 1)
    end,
    scrolling = function(px, py, s)
        ui.draw_rounded_rect(px + s * 0.1, py, s * 0.8, s, s * 0.08, 0.92, 0.92, 0.95, 1)
        for i = 0, 3 do ui.draw_rect(px + s * 0.2, py + s * (0.15 + i * 0.2), s * 0.45, s * 0.07, 0.4, 0.4, 0.45, 1) end
        ui.draw_rect(px + s * 0.75, py + s * 0.12, s * 0.07, s * 0.35, 0.25, 0.55, 0.95, 1)
    end,
    notes = function(px, py, s)
        ui.draw_rect(px + s * 0.15, py, s * 0.7, s, 1, 0.92, 0.5, 1)
        for i = 0, 3 do ui.draw_line(px + s * 0.25, py + s * (0.25 + i * 0.18), px + s * 0.75, py + s * (0.25 + i * 0.18), math.max(1, s * 0.04), 0.5, 0.4, 0.2, 1) end
    end,
    tour = function(px, py, s)
        ui.draw_rounded_rect(px + s * 0.1, py, s * 0.8, s, s * 0.1, 0.3, 0.75, 0.45, 1)
        for i = 0, 2 do
            local y = py + s * (0.22 + i * 0.27)
            ui.draw_line(px + s * 0.22, y, px + s * 0.3, y + s * 0.07, math.max(1.5, s * 0.06), 1, 1, 1, 1)
            ui.draw_line(px + s * 0.3, y + s * 0.07, px + s * 0.42, y - s * 0.07, math.max(1.5, s * 0.06), 1, 1, 1, 1)
            ui.draw_rect(px + s * 0.5, y - s * 0.02, s * 0.3, s * 0.07, 1, 1, 1, 0.8)
        end
    end,
    about = function(px, py, s)
        ui.draw_circle(px + s / 2, py + s / 2, s * 0.45, 0.25, 0.55, 0.95)
        ui.draw_rect(px + s * 0.45, py + s * 0.42, s * 0.1, s * 0.33, 1, 1, 1, 1)
        ui.draw_circle(px + s / 2, py + s * 0.28, s * 0.07, 1, 1, 1)
    end,
}

-- ===========================================================================
-- The apps. Each draws inside its window: wf is the window's frame, (0, 0) its top-left;
-- win.w, win.h its size in units (the player can resize it); s the app's own saved state.
-- ===========================================================================

local apps = {
    -- w, h: the window's size when it opens. content_h: how tall the app's contents are - when
    -- the window is made shorter, they scroll instead of squashing. min_w: narrower than this,
    -- the layout wouldn't fit (the window can't be made narrower)
    { id = "tour",      name = "Tour",      w = 13, h = 10.5, min_w = 7,  min_h = 7, content_h = 7 },
    { id = "widgets",   name = "Widgets",   w = 14, h = 11,   min_w = 12, content_h = 10.8 },
    { id = "inventory", name = "Inventory", w = 12, h = 7.4,  min_w = 12, content_h = 7.2 },
    { id = "shapes",    name = "Shapes",    w = 13, h = 9,    min_w = 13, content_h = 9 },
    { id = "scrolling", name = "Scrolling", w = 9,  h = 8,    min_w = 9,  content_h = 4 },
    { id = "notes",     name = "Notes",     w = 10, h = 7,    min_w = 6,  content_h = 4 },
    { id = "about",     name = "About",     w = 10, h = 5.5,  min_w = 6,  content_h = 5 },
}
local draw_app = {}

-- ---- Widgets: every basic widget ----
function draw_app.widgets(wf, win, s)
    s.clicks = s.clicks or 0
    s.volume = s.volume or 0.6
    s.picked = s.picked or 1

    wf:label(0.4, 0.3, "Buttons", { color = kit.theme.title })
    if wf:button("count", 0.4, 0.9, 4, 1, "Clicked " .. s.clicks .. "x", { tooltip = "Click me!" }) then
        s.clicks = s.clicks + 1
    end
    if wf:button("reset", 4.6, 0.9, 3, 1, "Reset", { danger = true, disabled = s.clicks == 0,
                                                       tooltip = "Disabled until you've clicked" }) then
        s.clicks = 0
    end

    wf:label(0.4, 2.3, "Checkbox and slider", { color = kit.theme.title })
    s.music = wf:checkbox("music", 0.4, 2.9, "Music", s.music)
    s.volume = wf:slider("volume", 3.6, 3, 3, s.volume, { min = 0, max = 1, tooltip = "Drag, or click anywhere on it" })
    wf:label(6.8, 3.05, math.floor(s.volume * 100) .. "%")   -- (ends before the list at 8.2)

    wf:label(0.4, 4.3, "Text fields", { color = kit.theme.title })
    local submitted
    s.name, submitted = wf:field("name", 0.4, 4.9, 7.2, 1, s.name, { placeholder = "Your name, then Enter", max_length = 20 })
    if submitted and (s.name or "") ~= "" then did("type") s.greeting = "Hello, " .. s.name .. "!" end
    s.secret = wf:field("secret", 0.4, 6.1, 7.2, 1, s.secret, { placeholder = "A password field", password = true })
    s.age = wf:field("age", 0.4, 7.3, 3, 1, s.age, { placeholder = "Numbers", numeric = true, max_length = 3 })
    if s.greeting then wf:label(3.7, 7.55, s.greeting, { color = kit.theme.accent }) end

    -- A list on the right; double-click a row to "open" it
    local fruit = { "Apple", "Banana", "Cherry", "Dragon fruit", "Elderberry", "Fig", "Grape",
                    "Honeydew", "Kiwi", "Lemon", "Mango", "Nectarine" }
    wf:label(8.2, 0.3, "List", { color = kit.theme.title })
    wf:label(9.6, 0.38, "(wheel scrolls)", { size = 0.3, color = kit.theme.muted })
    local picked, _, opened = wf:list("fruit", 8.2, 0.9, math.max(3, win.w - 8.6), math.max(2, win.h - 3.6), fruit, s.picked)
    s.picked = picked
    if opened then s.greeting = "You opened " .. fruit[s.picked] end

    -- Text that wraps to the window's width (resize the window and watch)
    wf:text(0.4, win.h - 2.3, win.w - 0.8,
            "This paragraph wraps to the window: drag the window's right edge to see it reflow.",
            { color = kit.theme.muted, size = 0.34 })
end

-- ---- Inventory: drag and drop between a chest and the real hotbar ----
local chest = nil       -- 27 slots of { name, count }
local function chest_init()
    if chest then return end
    chest = {}
    for i, name in ipairs(some_items(10)) do chest[i * 2 - 1] = { name = name, count = 8 * i } end
end
local function slot_get(where, i)
    if where == "chest" then return chest[i] end
    local name, count = hotbar.get_slot(i)
    if not name or name == "" then return nil end
    return { name = name, count = count }
end
local function slot_put(where, i, st)
    if where == "chest" then chest[i] = st
    else hotbar.set_slot(i, st and st.name or "air", st and st.count or 0) end
end
local function first_empty(where)
    local n = where == "chest" and 27 or hotbar.get_slot_count()
    for j = 1, n do if not slot_get(where, j) then return j end end
end
local function move(from, to)                          -- same item stacks (to 64), else swap
    if from.where == to.where and from.i == to.i then return end
    local a, b = slot_get(from.where, from.i), slot_get(to.where, to.i)
    if a and b and a.name == b.name then
        local n = math.min(64 - b.count, a.count)
        b.count, a.count = b.count + n, a.count - n
        slot_put(to.where, to.i, b)
        slot_put(from.where, from.i, a.count > 0 and a or nil)
    else
        slot_put(to.where, to.i, a)
        slot_put(from.where, from.i, b)
    end
end

function draw_app.inventory(wf, win, s)
    chest_init()
    local function slot(where, i, x, y)
        local st = slot_get(where, i)
        local here = { where = where, i = i }
        local click, dropped = wf:item(x, y, 1, st and st.name, {
            id = where .. i, count = st and st.count,
            tooltip = st and (pretty(st.name) .. " x" .. st.count),
            drag = here, drop = true,                        -- pick up from here, put down here
        })
        if dropped then
            if dropped.where ~= where or dropped.i ~= i then did("drag") end
            move(dropped, here)
        end
        if click and st then
            if click.shift then                              -- Shift+click: to the other side
                local other = where == "chest" and "hotbar" or "chest"
                local j = first_empty(other)
                if j then move(here, { where = other, i = j }) end
            elseif click.button == "right" and st.count > 1 then   -- right-click: split in half
                local j = first_empty(where)
                if j then
                    local half = math.floor(st.count / 2)
                    slot_put(where, i, { name = st.name, count = st.count - half })
                    slot_put(where, j, { name = st.name, count = half })
                end
            end
        end
    end
    wf:label(0.4, 0.25, "Chest", { color = kit.theme.title })
    for i = 1, 27 do slot("chest", i, 0.4 + ((i - 1) % 9) * 1.1, 0.8 + ((i - 1) // 9) * 1.1) end
    wf:label(0.4, 4.25, "Your hotbar (the real one)", { color = kit.theme.title })
    for i = 1, math.min(9, hotbar.get_slot_count()) do slot("hotbar", i, 0.4 + (i - 1) * 1.1, 4.8) end

    -- A trash can that only shows while something is being dragged
    if wf:dragging() then
        wf:box(10.4, 2.0, 1.2, 1.4, { 0.45, 0.12, 0.12, 1 }, { 0.9, 0.3, 0.3, 1 })
        wf:label(10.4, 3.5, "Trash", { size = 0.3, w = 1.2, align = "center" })
        local dropped = wf:drop_target("trash", 10.4, 2.0, 1.2, 1.4)
        if dropped then slot_put(dropped.where, dropped.i, nil) end
    end
    wf:text(0.4, 6.2, win.w - 0.8, "Drag to move or stack; Shift+click sends across; right-click splits.",
            { size = 0.3, color = kit.theme.muted })
end

-- ---- Shapes: the engine's drawing functions, animated ----
function draw_app.shapes(wf, win, s)
    local u, t = wf.u, ui.get_time()
    local ox, oy = wf:px(0, 0)
    local function P(x, y) return ox + x * u, oy + y * u end

    -- circles, rings and a pie that fills up (a cooldown dial)
    local cx, cy = P(1.8, 1.8)
    ui.draw_circle(cx, cy, 1.2 * u, 0.2, 0.22, 0.28)
    ui.draw_pie(cx, cy, 1.2 * u, -90, -90 + 360 * ((t / 3) % 1), 0.3, 0.75, 0.45)
    ui.draw_ring(cx, cy, 1.2 * u, math.max(2, u * 0.12), 1, 1, 1, 0.9)

    -- a clock hand: ui.draw_line at any angle
    local hx, hy = P(5, 1.8)
    ui.draw_circle(hx, hy, 1.2 * u, 0.95, 0.95, 0.97)
    local a = t * 1.5
    ui.draw_line(hx, hy, hx + math.cos(a) * u, hy + math.sin(a) * u, math.max(2, u * 0.1), 0.85, 0.2, 0.2)
    ui.draw_circle(hx, hy, u * 0.12, 0.1, 0.1, 0.1)

    -- a hexagon (convex polygon) and a triangle
    local px, py = P(8.2, 1.8)
    local pts = {}
    for k = 0, 5 do
        local ang = math.rad(k * 60 + t * 20)
        pts[#pts + 1] = px + math.cos(ang) * 1.1 * u
        pts[#pts + 1] = py + math.sin(ang) * 1.1 * u
    end
    ui.draw_polygon(pts, 0.95, 0.75, 0.25)
    local x1, y1 = P(10.3, 2.9)
    local x2, y2 = P(11.4, 0.7)
    local x3, y3 = P(12.5, 2.9)
    ui.draw_triangle(x1, y1, x2, y2, x3, y3, 0.6, 0.4, 0.95)

    -- gradients, rounded rectangles, outlines
    local gx, gy = P(0.6, 3.6)
    ui.draw_gradient(gx, gy, 5 * u, 1.2 * u, { 0.95, 0.3, 0.3, 1 }, { 0.3, 0.4, 0.95, 1 }, "horizontal")
    ui.draw_gradient(gx, gy + 1.5 * u, 5 * u, 1.2 * u, { 1, 1, 1, 1 }, { 0.1, 0.1, 0.1, 1 })
    local rx, ry = P(6.2, 3.6)
    ui.draw_rounded_rect(rx, ry, 2.6 * u, 2.7 * u, 0.5 * u, 0.25, 0.55, 0.95, 1)
    ui.draw_rect_outline(rx + 3 * u, ry, 2.6 * u, 2.7 * u, math.max(2, u * 0.08), 1, 0.85, 0.3)

    -- a picture: rotating, tinted, flipped
    local ix, iy = P(0.6, 6.7)
    ui.draw_image("ui_showcase_crate.png", ix, iy, 1.6 * u, 1.6 * u, { rotation = t * 45 })
    ui.draw_image("ui_showcase_crate.png", ix + 2 * u, iy, 1.6 * u, 1.6 * u, { tint = { 0.5, 1, 0.6 } })
    ui.draw_image("ui_showcase_crate.png", ix + 4 * u, iy, 1.6 * u, 1.6 * u, { alpha = 0.4 + 0.4 * math.sin(t * 3) })

    -- clipping: a big circle that only shows inside a box
    local bx, by = P(6.2, 6.7)
    ui.draw_rect_outline(bx, by, 5.6 * u, 1.6 * u, 1, 1, 1, 1, 0.5)
    ui.push_clip(bx, by, 5.6 * u, 1.6 * u)
    ui.draw_circle(bx + (2.8 + math.sin(t) * 2.5) * u, by + 0.8 * u, 1.6 * u, 0.3, 0.85, 0.9)
    ui.pop_clip()
    wf:label(6.2, 8.4, "push_clip: only inside the box", { size = 0.28, color = kit.theme.muted })
end

-- ---- Scrolling: a scroll area with widgets in it ----
function draw_app.scrolling(wf, win, s)
    s.done = s.done or {}
    local rows = 40
    wf:label(0.4, 0.25, "Chores (wheel or drag the bar)", { color = kit.theme.title })
    wf:scroll_area("chores", 0.4, 0.9, win.w - 0.8, win.h - 1.3, rows * 1.1, function(sf, offset)
        if offset > 0 then did("scroll") end
        for i = 1, rows do
            s.done[i] = sf:checkbox("c" .. i, 0, (i - 1) * 1.1, "Chore number " .. i, s.done[i])
            -- (right-aligned, but never over the label: the window's min_w leaves room for both)
            if sf:button("b" .. i, math.max(5.6, win.w - 3.6), (i - 1) * 1.1, 2.4, 0.9, "Done") then s.done[i] = true end
        end
    end)
end

-- ---- Notes: fields with copy and paste ----
function draw_app.notes(wf, win, s)
    s.notes = s.notes or { "Ctrl+C copies a field, Ctrl+V pastes.", "Double-click a note to edit it." }
    local sel, _, opened = wf:list("notes", 0.3, 0.3, win.w - 0.6, win.h - 2, s.notes, s.sel)
    s.sel = sel
    if opened and s.sel then s.draft = table.remove(s.notes, s.sel); s.sel = nil end
    local submitted
    s.draft, submitted = wf:field("new", 0.3, win.h - 1.4, win.w - 0.6, 1, s.draft,
                                  { placeholder = "Type a note, press Enter", max_length = 120 })
    if submitted and (s.draft or "") ~= "" then
        did("type")
        s.notes[#s.notes + 1] = s.draft
        s.draft = ""
    end
end

-- ---- Tour: what to try, ticked off as you do it ----
local open_everything      -- (defined with the desktop)
function draw_app.tour(wf, win, s)
    local n = 0
    for _, item in ipairs(tour) do if done[item.id] then n = n + 1 end end
    local w = win.w - 0.8
    wf:label(0.4, 0.3, "Try these (" .. n .. " of " .. #tour .. " done)", { color = kit.theme.title })
    -- a progress bar
    wf:rect(0.4, 0.95, w, 0.25, kit.theme.field)
    wf:rect(0.4, 0.95, w * n / #tour, 0.25, kit.theme.accent)

    -- The buttons: side by side when there's room, else one above the other
    local side_by_side = w >= 8.8
    local buttons_h = side_by_side and 1.4 or 2.6

    -- The list wraps each line to the window's width, so making the window narrower (or wider)
    -- reflows it; when it's taller than the room, it scrolls
    local text_w = w - 1.1
    local heights, total = {}, 0
    for i, item in ipairs(tour) do
        heights[i] = math.max(0.75, wf:measure_text(text_w, item.text, { size = 0.32 }) + 0.2)
        total = total + heights[i]
    end
    wf:scroll_area("list", 0.4, 1.5, w, win.h - 1.7 - buttons_h, total, function(sf)
        local y = 0
        for i, item in ipairs(tour) do
            local ok = done[item.id]
            sf:box(0, y + 0.1, 0.5, 0.5, ok and kit.theme.accent or kit.theme.field, kit.theme.field_border)
            if ok then
                local cx, cy = sf:px(0, y + 0.1)
                local u = sf.u
                ui.draw_line(cx + 0.1 * u, cy + 0.27 * u, cx + 0.22 * u, cy + 0.4 * u, math.max(1.5, u * 0.07), 1, 1, 1, 1)
                ui.draw_line(cx + 0.22 * u, cy + 0.4 * u, cx + 0.42 * u, cy + 0.1 * u, math.max(1.5, u * 0.07), 1, 1, 1, 1)
            end
            sf:text(0.75, y + 0.18, text_w, item.text, { size = 0.32, color = ok and kit.theme.muted or kit.theme.text })
            y = y + heights[i]
        end
    end)

    local by = win.h - buttons_h
    local bw = side_by_side and 5 or math.min(w, 6)
    if wf:button("all", 0.4, by, bw, 1, "Open every app", { tooltip = "Opens all the apps, side by side" }) then
        open_everything(wf)
    end
    local rx, ry = side_by_side and 5.6 or 0.4, side_by_side and by or by + 1.2
    if wf:button("reset", rx, ry, side_by_side and 3.4 or bw, 1, "Start over", { disabled = n == 0 }) then
        for k in pairs(done) do done[k] = nil end
    end
end

-- ---- About ----
function draw_app.about(wf, win)
    wf:text(0.4, 0.4, win.w - 0.8,
            "UI Kit Showcase\n\nEvery window here is plain Lua using lualib/ui_kit.lua. " ..
            "Read mods/ui_showcase/init.lua to see how each part is made, and the modding " ..
            "guide (sections 14.5 and 14.6) for the full reference.")
end

-- ===========================================================================
-- The desktop
-- ===========================================================================

local function open_app(f, s, id)
    s.open[id] = true
    f:focus_window(id)
end

-- Opens every app and lays the windows out in a grid (f:move_window), so all of it is on screen
local session = { open = { tour = true }, apps = {},       -- kept while the game runs
                  icon_pos = {},                          -- where each desktop icon is (they can be dragged)
                  icon_sel = kit.selection(),             -- which icons are selected
                  icon_grid = kit.icon_grid{ cell_w = 3, cell_h = 3 } }   -- one cell each: no overlapping
function open_everything(f)
    local W, H = f.w_units, f.h_units - 1.2
    local cols = 3
    local rows = math.ceil(#apps / cols)
    local cw, ch = (W - 6) / cols, H / rows
    for i, app in ipairs(apps) do
        session.open[app.id] = true
        local c, r = (i - 1) % cols, (i - 1) // cols
        f:move_window(app.id, 5.5 + c * cw, r * ch + 0.2, cw - 0.4, ch - 1.4)
        f:maximize_window(app.id, false)
    end
    session.arrange = true      -- (windows that didn't exist yet get placed next frame)
end

-- Notices what the player did with the windows, for the tour: compares each window with how it
-- was last frame (f:window_state)
local last = {}
local function watch_windows(f)
    for _, app in ipairs(apps) do
        local now = f:window_state(app.id)
        local before = last[app.id]
        if now and before then
            if now.maximized and not before.maximized then did("maximize") end
            if before.minimized and not now.minimized then did("restore") end
            if not now.maximized and not before.maximized and not session.arrange then
                if now.w ~= before.w or now.h ~= before.h then did("resize")
                elseif now.x ~= before.x or now.y ~= before.y then did("move") end
            end
            if now.focused and not before.focused and session.focus_by_click then did("front") end
            if (now.x ~= before.x or now.y ~= before.y) and not session.arrange and #f:selected_windows() >= 2 then
                did("windows")
            end
        end
        last[app.id] = now
    end
    session.focus_by_click = false
end

local desktop = kit.form{
    id = MOD .. ":desktop", fullscreen = true,
    draw = function(f, s)
        local W, H = f.w_units, f.h_units
        local t = ui.get_time()

        -- wallpaper: a gradient with slowly drifting circles
        local x0, y0 = f:px(0, 0)
        ui.draw_gradient(x0, y0, f:pw(W), f:pw(H), { 0.08, 0.20, 0.38, 1 }, { 0.26, 0.08, 0.30, 1 })
        for k = 1, 5 do
            local cx = x0 + f:pw((k * 7.3 + math.sin(t * 0.2 + k) * 2) % W)
            local cy = y0 + f:pw((k * 4.1 + math.cos(t * 0.15 + k) * 2) % H)
            ui.draw_circle(cx, cy, f:pw(2 + k * 0.6), 1, 1, 1, 0.04)
        end
        f:label(W - 13, H - 2.6, "UI Kit Showcase - double-click an icon", { size = 0.35, color = { 1, 1, 1, 0.6 } })
        f:set_work_area(0, 0, W, H - 1.2)              -- maximized windows stop above the taskbar

        -- Desktop icons. Click selects one (Ctrl+click adds or removes it), double-click opens,
        -- right-click shows a menu, dragging moves it - with every other selected icon
        -- Like a real desktop, icons sit in a grid, one per cell: dragged ones follow the mouse,
        -- an outline shows where each will land, and when let go each snaps to the nearest free
        -- cell - never on top of another icon.
        local sel = s.icon_sel
        local ICON = 1.6
        local grid = s.icon_grid
        grid:set_area(0, 0, W, H - 2.4)
        for _, app in ipairs(apps) do
            if not s.icon_pos[app.id] then grid:add(s.icon_pos, app.id) end
        end
        grid:keep_in(s.icon_pos, s.icon_dragging and sel:list() or nil)   -- (the screen got smaller)
        if s.icon_dragging then
            for _, p in pairs(grid:preview(s.icon_pos, sel:list())) do
                local px, py = f:px(p.x - 0.2, p.y - 0.1)
                local pw, ph = f:pw(ICON + 0.4), f:pw(ICON + 0.9)
                ui.draw_rect(px, py, pw, ph, 1, 1, 1, 0.08)
                if ui.draw_rect_outline then ui.draw_rect_outline(px, py, pw, ph, 1, 1, 1, 1, 0.45) end
            end
        end
        for i, app in ipairs(apps) do
            local pos = s.icon_pos[app.id]
            local what, a, b = f:icon(app.id, pos.x, pos.y, ICON, nil, app.name,
                                      { selected = sel:has(app.id), draw = icon_art[app.id], movable = true,
                                        tooltip = "Double-click to open. Drag to move (with the others selected)" })
            if what == "select" then
                if a.ctrl then sel:toggle(app.id)                  -- Ctrl+click: add / take out
                elseif not sel:has(app.id) then sel:only(app.id) end   -- (already selected: wait - it may be dragged)
            elseif what == "click" and not a.ctrl then
                sel:only(app.id)                                   -- let go without dragging: just this one
                s.icon_dragging = false
            elseif what == "drag" then
                if not sel:has(app.id) then sel:only(app.id) end
                s.icon_dragging = true
                for _, id in ipairs(sel:list()) do                 -- every selected icon moves along
                    local p = s.icon_pos[id]
                    p.x = math.max(0, math.min(W - ICON, p.x + a))
                    p.y = math.max(0, math.min(H - 2.4 - ICON, p.y + b))
                end
                if sel:count() >= 2 then did("icons") end
            elseif what == "drop" then
                grid:arrange(s.icon_pos, sel:list())              -- each to the nearest free cell
                s.icon_dragging = false
            elseif what == "open" then open_app(f, s, app.id); did("icon")
            elseif what == "menu" then
                if not sel:has(app.id) then sel:only(app.id) end
                s.menu_app = app; f:open_popup("icon_menu", pos.x + 1, pos.y + 1); did("menu")
            end
        end

        -- A box on the empty desktop selects the icons (and windows) it touches. Called after
        -- the icons, so pressing on an icon doesn't start a box.
        local box, done, mods = f:marquee("select", 0, 0, W, H - 1.2, { windows = true })
        if box then
            if box.w == 0 and box.h == 0 then
                if done and not mods.ctrl then sel:clear() end        -- a click on nothing: deselect
            else
                if not s.box_base then                                -- (Ctrl: add to what was selected)
                    s.box_base = {}
                    if mods.ctrl then for _, id in ipairs(sel:list()) do s.box_base[id] = true end end
                end
                sel:clear()
                for id in pairs(s.box_base) do sel:set(id, true) end
                for id, p in pairs(s.icon_pos) do
                    if kit.overlaps(box, p.x - 0.2, p.y - 0.1, ICON + 0.4, ICON + 0.9) then sel:set(id, true) end
                end
                if done and sel:count() + #f:selected_windows() >= 2 then did("box") end
            end
            if done then s.box_base = nil end
        end

        -- Ctrl+A: select every icon
        for _, k in ipairs(ui.get_key_events and ui.get_key_events() or {}) do
            if k.key == ui.keys.A and k.ctrl and k.action == "press" then
                for _, app in ipairs(apps) do sel:set(app.id, true) end
            end
        end
        local choice = f:popup("icon_menu", { "Open", "Open maximized", "-", "About" })
        if choice == 1 then open_app(f, s, s.menu_app.id)
        elseif choice == 2 then open_app(f, s, s.menu_app.id); f:maximize_window(s.menu_app.id)
        elseif choice == 4 then open_app(f, s, "about") end

        -- (a click on a window that isn't in front brings it forward: noticed for the tour)
        for _, e in ipairs(ui.get_mouse_events and ui.get_mouse_events() or {}) do
            if e.pressed then session.focus_by_click = true end
        end

        -- one window per open app
        for i, app in ipairs(apps) do
            if s.open[app.id] then
                local still_open = f:window(app.id, { title = app.name, x = 7 + i * 1.2, y = 1 + i * 0.8,
                                                      w = app.w, h = app.h, min_w = app.min_w or 6, min_h = app.min_h or 4 }, function(wf, win)
                    s.apps[app.id] = s.apps[app.id] or {}
                    -- The app's contents in a scroll area: when the window is shorter than
                    -- they are, they scroll (like a real desktop) instead of overlapping
                    local need = app.content_h or win.h
                    local scrolls = win.h < need
                    wf:scroll_area("content", 0, 0, win.w, win.h, need, function(sf)
                        draw_app[app.id](sf, { w = win.w - (scrolls and 0.35 or 0), h = math.max(win.h, need),
                                              focused = win.focused, id = win.id }, s.apps[app.id])
                    end)
                end)
                if not still_open then s.open[app.id] = false; did("close") end
            end
        end
        if s.arrange then                              -- "Open every app": place the new ones too
            s.arrange_frames = (s.arrange_frames or 0) + 1
            if s.arrange_frames == 2 then open_everything(f) end
            if s.arrange_frames >= 3 then s.arrange, s.arrange_frames = nil, nil end
        end
        watch_windows(f)

        -- the taskbar: a frameless window that's always on top
        f:window("taskbar", { frame = false, topmost = true, x = 0, y = H - 1.2, w = W, h = 1.2,
                              background = { 0.06, 0.06, 0.08, 0.95 } }, function(tb)
            if tb:button("start", 0.1, 0.1, 3, 1, "Start") then tb:open_popup("start", 0.1, -(#apps + 2) * 0.9 - 0.3) end
            local entries = {}
            for i, app in ipairs(apps) do entries[i] = app.name end
            entries[#entries + 1] = "-"
            entries[#entries + 1] = "Close the showcase"
            local pick = tb:popup("start", entries, { width = 6 })
            if pick == #entries then f:close()
            elseif pick then open_app(f, s, apps[pick].id) end

            for i, w in ipairs(f:window_list()) do     -- a button per window
                if tb:button("task_" .. w.id, 3.3 + (i - 1) * 4.1, 0.1, 4, 1, w.title,
                             { selected = w.focused, tooltip = w.minimized and "Minimized - click to restore" or nil }) then
                    if w.focused then f:minimize_window(w.id) else f:focus_window(w.id) end
                end
            end
            local minutes = math.floor(((sky and sky.get_time and sky.get_time() or 0.75) - 0.25) * 24 * 60) % (24 * 60)
            tb:label(W - 2.6, 0.38, string.format("%02d:%02d", minutes // 60, minutes % 60))
        end)
    end,
}

local function open_showcase()
    if desktop:is_open() then return end
    -- The creative inventory closes first (it would otherwise stay open on top of the desktop)
    if hotbar.is_creative_open and hotbar.is_creative_open() and hotbar.set_creative_open then
        hotbar.set_creative_open(false)
    end
    -- Another mod's screen (a chest, the survival inventory) is open: don't take it over
    if engine.is_inventory_open and engine.is_inventory_open() and not kit.current() then return end
    session.open.tour = true
    desktop:open(session)
end
showcase = { open = open_showcase }                       -- for other mods / the console

-- ===========================================================================
-- Ways to open it: /uikit, F7, the UI Kit Remote, right-clicking a Showcase Laptop
-- ===========================================================================

chat.register_command("uikit", "/uikit", "Open the UI Kit Showcase")
local previous_command = on_chat_command
function on_chat_command(text, ...)
    if text == "uikit" then open_showcase() return true end
    if previous_command then return previous_command(text, ...) end
    return false
end

local previous_update = on_update
function on_update(dt, ...)
    for _, k in ipairs(ui.get_key_events and ui.get_key_events() or {}) do
        if k.key == ui.keys.F7 and k.action == "press" then
            if desktop:is_open() then desktop:close() else open_showcase() end
        end
    end
    if previous_update then return previous_update(dt, ...) end
end

if engine.register_item_use then
    engine.register_item_use("showcase_remote", function(pointed)
        if not pointed.repeat_click then open_showcase() end
        return true                                      -- (never placed)
    end)
end

local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "showcase_laptop" and not sneaking then
        if not repeat_click then open_showcase() end
        return true
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end

-- ===========================================================================
-- Creative inventory tabs (creative mode only: that's where the creative inventory is)
-- ===========================================================================

if creative and creative.register_tab then
    -- 1. This mod's blocks get a tab of their own. With no list or filter, it's every block and
    --    item this mod registers (and they leave the built-in categories).
    creative.register_tab{ name = "UI Showcase", icon = "showcase_laptop" }

    -- 2. A whole page drawn with ui_kit: a launcher for the showcase, and a few widgets
    local favourites = nil
    creative.register_tab{
        id = MOD .. ":page", name = "UI Kit", icon = "showcase_remote",
        draw = function(f, st)
            local W = f.w_units
            f:label(0, 0, "UI Kit Showcase", { size = 0.6, color = kit.theme.title })
            f:text(0, 0.9, W, "Movable, resizable windows, a taskbar, drag and drop, menus, scrolling, every widget " ..
                   "and the engine's drawing - all made with lualib/ui_kit.lua.", { size = 0.36, color = kit.theme.muted })
            if f:button("open", 0, 2.4, math.min(W, 12), 1.4, "Open the showcase", { tooltip = "Also: /uikit or F7" }) then
                open_showcase()
            end
            -- Two buttons sized to their labels: side by side if they fit, else one above the other
            local remote_label, laptop_label = "Take the UI Kit Remote", "Take a Laptop block"
            local bw1 = math.min(W, f:button_width(remote_label, 1.1))
            local bw2 = math.min(W, f:button_width(laptop_label, 1.1))
            local side_by_side = bw1 + 0.4 + bw2 <= W
            local y2 = side_by_side and 4.1 or 5.4
            if f:button("remote", 0, 4.1, bw1, 1.1, remote_label,
                        { tooltip = "Hold it and right-click anywhere to open the showcase" }) then
                creative.hold("showcase_remote", 1)
            end
            if f:button("laptop", side_by_side and bw1 + 0.4 or 0, y2, bw2, 1.1, laptop_label,
                        { tooltip = "Place it and right-click it to open the showcase" }) then
                creative.hold("showcase_laptop", 1)
            end
            local y = y2 + 1.5
            f:label(0, y, "Then click a hotbar slot below to put it there.", { size = 0.32, color = kit.theme.muted })

            -- A small taste of drag and drop, right here in the creative inventory
            favourites = favourites or some_items(6)
            f:label(0, y + 1.0, "Drag these to rearrange them; click one to pick up a stack:", { size = 0.32 })
            for i = 1, #favourites do
                local click, dropped = f:item((i - 1) * 1.6, y + 1.6, 1.4, favourites[i], {
                    id = "fav" .. i, tooltip = favourites[i] and pretty(favourites[i]),
                    drag = { i = i }, drop = true,
                })
                if dropped then favourites[i], favourites[dropped.i] = favourites[dropped.i], favourites[i] end
                if click and favourites[i] then creative.hold(favourites[i], 64) end
            end
        end,
    }
else
    print("[ui_showcase] no creative inventory with tabs here (needs the inventory mod's creative.register_tab)")
end

print("[ui_showcase] loaded: /uikit, F7 or the UI Kit Remote opens it")
