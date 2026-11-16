-- Survival inventory: 27 storage slots + the hotbar, like Minecraft. Opened with the
-- inventory key (E) in survival; creative keeps the creative menu.
--
--   left click   pick up / put down a stack (same items merge, different ones swap)
--   right click  pick up half / put down one
--   shift+click  move a stack between the hotbar and storage
--
-- Items dug when the hotbar is full go into storage (engine hook on_hotbar_full).
--
-- For other mods:
--   survival.inventory.add(name, count)  -> how many didn't fit
--   survival.inventory.get(i)            -> name, count   (i = 1..size)
--   survival.inventory.set(i, name, count)
--   survival.inventory.size()
--   survival.inventory.clear()
--   survival.inventory.is_open()

local inv = {}
survival.inventory = inv

local SIZE = 27
local MAX_STACK = 64
local slots = {}            -- [i] = { name, count } or nil
local held = nil            -- stack on the mouse pointer
local open = false

local function stack_max(name)
    local def = engine.get_block_def and engine.get_block_def(name)
    if def and def.tool_type and def.tool_type ~= "" then return 1 end   -- tools don't stack
    return MAX_STACK
end

-- Saved with the world
local function save()
    local parts = {}
    for i = 1, SIZE do
        local s = slots[i]
        parts[#parts + 1] = s and (s.name .. "*" .. s.count) or ""
    end
    engine.set_storage("survival:inventory", table.concat(parts, ";"))
end

function inv.load()
    slots = {}
    held = nil
    local data = engine.get_storage("survival:inventory")
    if not data then return end
    local i = 0
    for part in (data .. ";"):gmatch("([^;]*);") do
        i = i + 1
        local name, count = part:match("^(.+)%*(%d+)$")
        if name and i <= SIZE then slots[i] = { name = name, count = tonumber(count) } end
    end
end

function inv.size() return SIZE end
function inv.get(i) local s = slots[i] if s then return s.name, s.count end return "", 0 end
function inv.set(i, name, count)
    if i < 1 or i > SIZE then return end
    slots[i] = (name and name ~= "" and (count or 0) > 0) and { name = name, count = count } or nil
    save()
end
function inv.clear() slots = {}; held = nil; save() end
function inv.is_open() return open end

-- Into storage: onto matching stacks first, then empty slots. Returns what didn't fit.
function inv.add(name, count)
    local max = stack_max(name)
    for i = 1, SIZE do
        local s = slots[i]
        if count > 0 and s and s.name == name and s.count < max then
            local n = math.min(count, max - s.count)
            s.count, count = s.count + n, count - n
        end
    end
    for i = 1, SIZE do
        if count > 0 and not slots[i] then
            local n = math.min(count, max)
            slots[i], count = { name = name, count = n }, count - n
        end
    end
    save()
    return count
end

-- ---------------------------------------------------------------------------
-- Slots: storage 1..27 and hotbar "h1".."h9" share one interface
-- ---------------------------------------------------------------------------
local function get_slot(id)
    if type(id) == "number" then return slots[id] end
    local name, count = hotbar.get_slot(tonumber(id:sub(2)))
    if name == "" or count <= 0 then return nil end
    return { name = name, count = count }
end

local function set_slot(id, s)
    if s and s.count <= 0 then s = nil end
    if type(id) == "number" then slots[id] = s; save()
    else hotbar.set_slot(tonumber(id:sub(2)), s and s.name or "", s and s.count or 0) end
end

local function click_slot(id, button, shift)
    local s = get_slot(id)
    if shift and button == "left" then
        -- move the whole stack across (hotbar <-> storage)
        if not s then return end
        if type(id) == "number" then
            for i = 1, hotbar.get_slot_count() do
                local h = get_slot("h" .. i)
                if not h then set_slot("h" .. i, s); set_slot(id, nil); return end
                if h.name == s.name and h.count < stack_max(s.name) then
                    local n = math.min(s.count, stack_max(s.name) - h.count)
                    h.count, s.count = h.count + n, s.count - n
                    set_slot("h" .. i, h)
                    if s.count <= 0 then set_slot(id, nil); return end
                end
            end
            set_slot(id, s)
        else
            local left = inv.add(s.name, s.count)
            set_slot(id, left > 0 and { name = s.name, count = left } or nil)
        end
        return
    end
    if button == "left" then
        if not held then
            if s then held = s; set_slot(id, nil) end
        elseif not s then
            set_slot(id, held); held = nil
        elseif s.name == held.name then
            local n = math.min(held.count, stack_max(s.name) - s.count)
            s.count, held.count = s.count + n, held.count - n
            set_slot(id, s)
            if held.count <= 0 then held = nil end
        else
            set_slot(id, held); held = s     -- swap
        end
    else -- right
        if not held then
            if s then
                local take = math.ceil(s.count / 2)
                held = { name = s.name, count = take }
                s.count = s.count - take
                set_slot(id, s)
            end
        elseif not s then
            set_slot(id, { name = held.name, count = 1 })
            held.count = held.count - 1
            if held.count <= 0 then held = nil end
        elseif s.name == held.name and s.count < stack_max(s.name) then
            s.count = s.count + 1
            set_slot(id, s)
            held.count = held.count - 1
            if held.count <= 0 then held = nil end
        end
    end
end

-- Whatever is on the pointer goes back when the screen closes
local function put_back_held()
    if not held then return end
    local left = held.count
    for i = 1, hotbar.get_slot_count() do
        local h = get_slot("h" .. i)
        if left > 0 and (not h or (h.name == held.name and h.count < stack_max(h.name))) then
            local have = h and h.count or 0
            local n = math.min(left, stack_max(held.name) - have)
            set_slot("h" .. i, { name = held.name, count = have + n })
            left = left - n
        end
    end
    if left > 0 then inv.add(held.name, left) end
    held = nil
end

function inv.set_open(want)
    if want == open then return open end
    open = want
    if not open then put_back_held() end
    return open
end

-- ---------------------------------------------------------------------------
-- Screen
-- ---------------------------------------------------------------------------
local function layout(W, H)
    local slot = math.floor(math.max(32, H / 14))
    local gap = math.floor(slot * 0.1)
    local cols = 9
    local pw = cols * (slot + gap) + gap * 3
    local ph = (slot + gap) * 4 + slot * 1.6 + gap * 4
    local px, py = math.floor((W - pw) / 2), math.floor((H - ph) / 2)
    local L = { slot = slot, gap = gap, px = px, py = py, pw = pw, ph = ph, rects = {} }
    local x0 = px + gap * 2
    local y0 = py + slot * 0.9
    for r = 0, 2 do
        for c = 0, cols - 1 do
            L.rects[#L.rects + 1] = { id = r * cols + c + 1, x = x0 + c * (slot + gap), y = y0 + r * (slot + gap) }
        end
    end
    local yh = y0 + 3 * (slot + gap) + slot * 0.4
    for c = 0, math.min(cols, hotbar.get_slot_count()) - 1 do
        L.rects[#L.rects + 1] = { id = "h" .. (c + 1), x = x0 + c * (slot + gap), y = yh }
    end
    return L
end

local function draw_stack(s, x, y, size)
    if not s then return end
    ui.draw_block_icon(s.name, x + size * 0.12, y + size * 0.12, size * 0.76)
    if s.count > 1 then
        local t = tostring(s.count)
        local scale = size / 56
        ui.draw_text(t, x + size - ui.text_width(t, scale) - 3, y + size - ui.text_height(scale) - 3, scale, 1, 1, 1)
    end
end

function render_inventory(W, H)
    if not open then return end
    local L = layout(W, H)
    ui.draw_rect(0, 0, W, H, 0, 0, 0, 0.45)                          -- dim the world
    ui.draw_rect(L.px, L.py, L.pw, L.ph, 0.78, 0.78, 0.78, 0.97)      -- panel
    local title_scale = L.slot / 50
    ui.draw_text("Inventory", L.px + L.gap * 2, L.py + L.slot * 0.25, title_scale, 0.25, 0.25, 0.25)
    local mx, my = ui.get_mouse()
    local hover = nil
    for _, r in ipairs(L.rects) do
        local over = mx >= r.x and mx < r.x + L.slot and my >= r.y and my < r.y + L.slot
        if over then hover = r end
        ui.draw_rect(r.x, r.y, L.slot, L.slot, 0.55, 0.55, 0.55, 1)
        ui.draw_rect(r.x + 2, r.y + 2, L.slot - 4, L.slot - 4, over and 0.62 or 0.45, over and 0.62 or 0.45, over and 0.62 or 0.45, 1)
        draw_stack(get_slot(r.id), r.x, r.y, L.slot)
    end
    if hover and not held then
        local s = get_slot(hover.id)
        if s then
            local scale = L.slot / 60
            local label = s.name:gsub("_", " ")
            local w = ui.text_width(label, scale) + 12
            ui.draw_rect(mx + 14, my - 8, w, ui.text_height(scale) + 12, 0.1, 0.05, 0.15, 0.92)
            ui.draw_text(label, mx + 20, my - 2, scale, 1, 1, 1)
        end
    end
    if held then draw_stack(held, mx - L.slot / 2, my - L.slot / 2, L.slot) end
end

function handle_inventory_click(mx, my, W, H, button, shift)
    if not open then return false end
    local L = layout(W, H)
    for _, r in ipairs(L.rects) do
        if mx >= r.x and mx < r.x + L.slot and my >= r.y and my < r.y + L.slot then
            click_slot(r.id, button or "left", shift)
            return true
        end
    end
    return true
end

-- E in survival: open/close (the engine frees the mouse while it's open)
local previous_toggle = toggle_main_inventory
function toggle_main_inventory()
    if survival.is_dead() then return false end
    if survival.get_mode() ~= "survival" then
        if previous_toggle then return previous_toggle() end
        return false
    end
    return inv.set_open(not open)
end

-- Dug items that don't fit in the hotbar
local previous_full = on_hotbar_full
function on_hotbar_full(name, count)
    if survival.get_mode() == "survival" then
        local left = inv.add(name, count)
        if left < count then return true end
    end
    if previous_full then return previous_full(name, count) end
    return false
end
