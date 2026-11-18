-- Survival inventory, drawn on the game's inventory picture (textures/survival_inventory.png):
--   8 equipment slots   pet, head, left glove, chest, right glove, feet, legs, backpack
--   2x2 crafting grid   with its output slot (recipes from survival.register_craft)
--   bin                 click to throw away what you're holding
--   27 storage slots and the hotbar
-- Opened with the inventory key (E) in survival; creative keeps the creative menu.
--
--   left click   pick up / put down a stack (same items merge, different ones swap)
--   right click  pick up half / put down one
--   hold + drag  with a stack on the pointer: left spreads it evenly over the slots you drag
--                across, right puts one in each (like Minecraft)
--   shift+click  move a stack between the hotbar and storage (on the output: craft all you can)
--
-- Items dug when the hotbar is full go into storage (engine hook on_hotbar_full).
--
-- For other mods:
--   survival.inventory.add(name, count)  -> how many didn't fit
--   survival.inventory.get(i)            -> name, count   (i = 1..size)
--   survival.inventory.set(i, name, count)
--   survival.inventory.size(), clear(), is_open()
--   survival.inventory.get_equipment(slot) -> name or nil   (slot = "head", "chest"...)
--   survival.inventory.pick(name)        -> true if it was put in the hand (pick block rules)
--   survival.inventory.equipment_slots()  -> { "pet", "head", ... } in screen order
--   survival.inventory.set_equipment(slot, name) -> false if it doesn't go there ("" / nil empties)
--   survival.inventory.fits_equipment(slot, name), stack_max(name), is_ready()
--   (the creative inventory's Inventory tab uses these)
--
--   survival.register_equipment(item, slot, { armor = points })  -- what each slot accepts
--   survival.on_equip(function(slot, item_or_nil) end)
--   survival.register_craft({ output = "planks", count = 4, recipe = { { "log_oak" } } })
--   survival.register_craft({ output = "stick", count = 4, recipe = { { "planks" }, { "planks" } } })
--   survival.register_craft({ type = "shapeless", output = "x", recipe = { "a", "b" } })
--   (recipes up to 2x2 work in the inventory; up to 3x3 at a workbench)
--   survival.register_workbench("my_table")   -- right-clicking that block opens the 3x3 grid
--   survival.inventory.open_workbench(), survival.inventory.get_screen() -> "inventory"|"workbench"|nil

local inv = {}
survival.inventory = inv

local SIZE = 27
local MAX_STACK = 64
local slots = {}            -- storage [1..27] = { name, count } or nil
local equipment = {}        -- [slot name] = { name, count = 1 }
local grid = {}             -- crafting [1..4] (2x2, row by row)
local held = nil            -- stack on the mouse pointer
local open = false

-- ---------------------------------------------------------------------------
-- The picture and where its slots are (in the picture's own pixels)
-- ---------------------------------------------------------------------------
local PIC_W, PIC_H = 178, 166
local SLOT = 16

local EQUIPMENT_SLOTS = {   -- id, x, y   (ids as used by register_equipment)
    { "pet", 9, 17 },   { "head", 27, 17 },
    { "glove_left", 9, 35 }, { "chest", 27, 35 }, { "glove_right", 45, 35 },
    { "feet", 9, 53 },  { "legs", 27, 53 },  { "backpack", 45, 53 },
}
local BIN = { x = 144, y = 60 }

-- Two screens on the same storage and hotbar: the inventory (E) with its 2x2 crafting grid,
-- and the workbench (right-click a workbench block) with a 3x3 grid
local SCREENS = {
    inventory = {
        pictures = { "inventory_survival.png", "survival_inventory.png" },
        grid_w = 2, grid_x = 72, grid_y = 26,
        output = { x = 140, y = 31, size = 24 },
        equipment = true, bin = true,
    },
    workbench = {
        pictures = { "inventory_workbench.png", "survival_workbench.png" },
        grid_w = 3, grid_x = 30, grid_y = 17,
        output = { x = 124, y = 32, size = 24 },
        title = "Crafting",
    },
}
local screen = "inventory"
local STORAGE = { x = 9, y = 81 }      -- 9 x 3, 18 pixels apart
local HOTBAR = { x = 9, y = 142 }
local STEP = 18
local SLOT_FILL = { 87 / 255, 87 / 255, 85 / 255 }   -- covers the outline icons when a slot is filled

do
    local resolve = survival.resolve_texture or function(n) return n end
    for id, sc in pairs(SCREENS) do
        for _, name in ipairs(sc.pictures) do
            local found = engine.find_texture and engine.find_texture(name)
            if found then sc.picture = resolve(name); break end
        end
        if not sc.picture then print("[Survival] No " .. id .. " picture found (" .. table.concat(sc.pictures, ", ") .. "): drawing a plain one") end
    end
end
local function grid_w() return SCREENS[screen].grid_w end

local function stack_max(name)
    local def = engine.get_block_def and engine.get_block_def(name)
    if def and def.tool_type and def.tool_type ~= "" then return 1 end   -- tools don't stack
    return MAX_STACK
end

-- ---------------------------------------------------------------------------
-- Equipment
-- ---------------------------------------------------------------------------
local equip_defs = {}      -- [item] = { slot = "head", armor = 2 }
local equip_callbacks = {}

function survival.register_equipment(item, slot, def)
    def = def or {}
    equip_defs[item] = { slot = slot, armor = def.armor or 0 }
end
function survival.on_equip(fn) equip_callbacks[#equip_callbacks + 1] = fn end
function inv.get_equipment(slot) local e = equipment[slot] return e and e.name or nil end

local function update_armor()
    local points = 0
    for _, e in pairs(equipment) do
        local d = equip_defs[e.name]
        if d then points = points + (d.armor or 0) end
    end
    if survival.set_armor then survival.set_armor(points) end
end

local function fits_equipment(slot, item)
    local d = equip_defs[item]
    return d ~= nil and d.slot == slot
end

-- ---------------------------------------------------------------------------
-- Crafting
-- ---------------------------------------------------------------------------
local recipes = {}

-- recipe = rows of item names ("" or nil = empty), up to 2x2 here; or a list for shapeless
function survival.register_craft(def)
    if not def or not def.output or not def.recipe then return end
    local r = { output = def.output, count = def.count or 1, shapeless = def.type == "shapeless" }
    if r.shapeless then
        r.items = {}
        for _, it in ipairs(def.recipe) do r.items[#r.items + 1] = it end
        table.sort(r.items)
    else
        r.rows = def.recipe
    end
    recipes[#recipes + 1] = r
end

-- The grid trimmed to its filled part: rows of names, and the sorted list of names
local function grid_shape()
    local w = grid_w()
    local cells = {}
    for i = 1, w * w do cells[i] = grid[i] and grid[i].name or "" end
    local minr, maxr, minc, maxc = w + 1, 0, w + 1, 0
    for r = 1, w do for c = 1, w do
        if cells[(r - 1) * w + c] ~= "" then
            minr, maxr = math.min(minr, r), math.max(maxr, r)
            minc, maxc = math.min(minc, c), math.max(maxc, c)
        end
    end end
    if maxr == 0 then return nil end
    local rows, list = {}, {}
    for r = minr, maxr do
        local row = {}
        for c = minc, maxc do
            local n = cells[(r - 1) * w + c]
            row[#row + 1] = n
            if n ~= "" then list[#list + 1] = n end
        end
        rows[#rows + 1] = row
    end
    table.sort(list)
    return rows, list
end

local function shaped_match(rows, want, mirror)
    if #rows ~= #want then return false end
    for r = 1, #want do
        local wr = want[r]
        if type(wr) ~= "table" then return false end
        if #rows[r] ~= #wr then return false end
        for c = 1, #wr do
            local w = wr[mirror and (#wr - c + 1) or c] or ""
            if (rows[r][c] or "") ~= w then return false end
        end
    end
    return true
end

local function current_recipe()
    local rows, list = grid_shape()
    if not rows then return nil end
    for _, r in ipairs(recipes) do
        if r.shapeless then
            if #r.items == #list then
                local same = true
                for i = 1, #list do if list[i] ~= r.items[i] then same = false break end end
                if same then return r end
            end
        elseif shaped_match(rows, r.rows, false) or shaped_match(rows, r.rows, true) then
            return r
        end
    end
    return nil
end

local function use_up_grid()
    for i = 1, 9 do
        local s = grid[i]
        if s then
            s.count = s.count - 1
            if s.count <= 0 then grid[i] = nil end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Saving (storage and equipment; the crafting grid empties when the screen closes)
-- ---------------------------------------------------------------------------
local function save()
    local parts = {}
    for i = 1, SIZE do
        local s = slots[i]
        parts[#parts + 1] = s and (s.name .. "*" .. s.count) or ""
    end
    engine.set_storage("survival:inventory", table.concat(parts, ";"))
    local eq = {}
    for id, e in pairs(equipment) do eq[#eq + 1] = id .. "=" .. e.name end
    engine.set_storage("survival:equipment", table.concat(eq, ";"))
end

local ready = false                 -- loaded for this world (before that, nothing may be saved)
function inv.is_ready() return ready end

function inv.load()
    slots, equipment, grid = {}, {}, {}
    held = nil
    local data = engine.get_storage("survival:inventory")
    if data then
        local i = 0
        for part in (data .. ";"):gmatch("([^;]*);") do
            i = i + 1
            local name, count = part:match("^(.+)%*(%d+)$")
            if name and i <= SIZE then slots[i] = { name = name, count = tonumber(count) } end
        end
    end
    local eq = engine.get_storage("survival:equipment")
    if eq then
        for id, name in eq:gmatch("([%w_]+)=([^;]+)") do equipment[id] = { name = name, count = 1 } end
    end
    update_armor()
    ready = true
end

function inv.size() return SIZE end
function inv.get(i) local s = slots[i] if s then return s.name, s.count end return "", 0 end
function inv.set(i, name, count)
    if i < 1 or i > SIZE then return end
    slots[i] = (name and name ~= "" and (count or 0) > 0) and { name = name, count = count } or nil
    save()
end
function inv.clear() slots = {}; equipment = {}; grid = {}; held = nil; update_armor(); save() end
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

-- Into the hotbar first, then storage. Returns what didn't fit.
local function give(name, count)
    for i = 1, hotbar.get_slot_count() do
        local hn, hc = hotbar.get_slot(i)
        if count > 0 and (hn == "" or hc <= 0 or (hn == name and hc < stack_max(name))) then
            local have = (hn == name) and hc or 0
            local n = math.min(count, stack_max(name) - have)
            hotbar.set_slot(i, name, have + n)
            count = count - n
        end
    end
    if count > 0 then count = inv.add(name, count) end
    return count
end

-- For other screens (the creative inventory's Inventory tab)
function inv.equipment_slots()
    local out = {}
    for i, e in ipairs(EQUIPMENT_SLOTS) do out[i] = e[1] end
    return out
end
function inv.fits_equipment(slot, item) return fits_equipment(slot, item) end
function inv.stack_max(name) return stack_max(name) end

-- ---------------------------------------------------------------------------
-- Slots: storage 1..27, hotbar "h1".., equipment "e:<slot>", crafting "g1".."g4"
-- ---------------------------------------------------------------------------
local function get_slot(id)
    if type(id) == "number" then return slots[id] end
    if id:sub(1, 2) == "e:" then return equipment[id:sub(3)] end
    if id:sub(1, 1) == "g" then return grid[tonumber(id:sub(2))] end
    local name, count = hotbar.get_slot(tonumber(id:sub(2)))
    if name == "" or count <= 0 then return nil end
    return { name = name, count = count }
end

local function set_slot(id, s)
    if s and s.count <= 0 then s = nil end
    if type(id) == "number" then slots[id] = s; save()
    elseif id:sub(1, 2) == "e:" then
        local slot = id:sub(3)
        equipment[slot] = s
        update_armor()
        save()
        for _, fn in ipairs(equip_callbacks) do pcall(fn, slot, s and s.name or nil) end
    elseif id:sub(1, 1) == "g" then grid[tonumber(id:sub(2))] = s
    else hotbar.set_slot(tonumber(id:sub(2)), s and s.name or "", s and s.count or 0) end
end

-- survival.inventory.set_equipment(slot, name): put an item in an equipment slot ("" or nil
-- empties it). False if that item doesn't go there. Armour and on_equip follow, as when the
-- player does it.
function inv.set_equipment(slot, name)
    if name and name ~= "" and not fits_equipment(slot, name) then return false end
    set_slot("e:" .. slot, (name and name ~= "") and { name = name, count = 1 } or nil)
    return true
end

local function max_in(id, name)
    if type(id) == "string" and id:sub(1, 2) == "e:" then return 1 end   -- one item per equipment slot
    return stack_max(name)
end

-- May this stack go into this slot?
local function accepts(id, s)
    if not s then return true end
    if type(id) == "string" and id:sub(1, 2) == "e:" then return fits_equipment(id:sub(3), s.name) end
    return true
end

local function click_slot(id, button, shift)
    local s = get_slot(id)
    if shift and button == "left" then
        if not s then return end
        if type(id) == "number" then
            -- storage -> equipment it belongs in, else the hotbar
            local d = equip_defs[s.name]
            if d and not equipment[d.slot] then
                set_slot("e:" .. d.slot, { name = s.name, count = 1 })
                s.count = s.count - 1
                set_slot(id, s.count > 0 and s or nil)
                return
            end
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
            -- hotbar / equipment / crafting -> storage
            local left = inv.add(s.name, s.count)
            set_slot(id, left > 0 and { name = s.name, count = left } or nil)
        end
        return
    end
    if button == "left" then
        if not held then
            if s then held = s; set_slot(id, nil) end
        elseif not s then
            if not accepts(id, held) then return end
            local n = math.min(held.count, max_in(id, held.name))
            set_slot(id, { name = held.name, count = n })
            held.count = held.count - n
            if held.count <= 0 then held = nil end
        elseif s.name == held.name then
            local n = math.min(held.count, max_in(id, s.name) - s.count)
            if n <= 0 then return end
            s.count, held.count = s.count + n, held.count - n
            set_slot(id, s)
            if held.count <= 0 then held = nil end
        else
            if not accepts(id, held) or held.count > max_in(id, held.name) then return end
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
            if not accepts(id, held) then return end
            set_slot(id, { name = held.name, count = 1 })
            held.count = held.count - 1
            if held.count <= 0 then held = nil end
        elseif s.name == held.name and s.count < max_in(id, s.name) then
            s.count = s.count + 1
            set_slot(id, s)
            held.count = held.count - 1
            if held.count <= 0 then held = nil end
        end
    end
end

-- The output slot: take what the grid makes
local function click_output(button, shift)
    local r = current_recipe()
    if not r then return end
    if shift then
        -- as many as the grid allows, straight into the inventory
        for _ = 1, 64 do
            if current_recipe() ~= r then break end
            local left = give(r.output, r.count)
            use_up_grid()
            if left > 0 then held = { name = r.output, count = left }; break end
        end
        return
    end
    if held and (held.name ~= r.output or held.count + r.count > stack_max(r.output)) then return end
    held = { name = r.output, count = (held and held.count or 0) + r.count }
    use_up_grid()
end

-- The bin: left click throws away what you hold, right click one of it
local function click_bin(button)
    if not held then return end
    if button == "right" then held.count = held.count - 1 else held.count = 0 end
    if held.count <= 0 then held = nil end
end

-- Whatever is on the pointer, and in the crafting grid, goes back when the screen closes
local drag = nil   -- { button = "left"|"right", slots = { id, ... }, set = { [id] = true } }

local function put_back()
    for i = 1, 9 do
        local s = grid[i]
        if s then
            local left = give(s.name, s.count)
            if left > 0 then print("[Survival] No room for " .. left .. " " .. s.name .. " from the crafting grid") end
            grid[i] = nil
        end
    end
    if held then
        local left = give(held.name, held.count)
        if left > 0 then print("[Survival] No room for " .. left .. " " .. held.name) end
        held = nil
    end
end

function inv.set_open(want)
    if want == open then return open end
    open = want
    if not open then
        put_back()
        drag = nil
        screen = "inventory"
    end
    return open
end

-- survival.inventory.open_from_creative(): the creative inventory's Inventory tab. Closes the
-- creative inventory and opens this one; in creative mode a "Creative" button goes back.
function inv.open_from_creative()
    if hotbar.set_creative_open then hotbar.set_creative_open(false) end
    screen = "inventory"
    inv.set_open(true)
    if engine.set_inventory_open then engine.set_inventory_open(true) end
    return true
end

-- The "Creative" button above the screen (creative mode only): x, y, w, h or nil
local function creative_button(L)
    if not (engine.is_creative and engine.is_creative()) or screen ~= "inventory" then return nil end
    if not hotbar.set_creative_open then return nil end
    local h = math.max(18, 12 * L.s)
    local w = math.max(70, 52 * L.s)
    local y = L.y - h - 2 * L.s
    if y < 0 then y = 0 end
    return L.x, y, w, h
end

-- The workbench screen (3x3 crafting). Called when a workbench block is right-clicked.
function inv.open_workbench()
    if open then inv.set_open(false) end
    screen = "workbench"
    open = true
    if engine.set_inventory_open then engine.set_inventory_open(true) end
    return true
end
function inv.get_screen() return open and screen or nil end

-- ---------------------------------------------------------------------------
-- Dragging a stack across slots
-- ---------------------------------------------------------------------------

local function can_take(id, item)
    local s = get_slot(id)
    if not accepts(id, { name = item, count = 1 }) then return false end
    return not s or (s.name == item and s.count < max_in(id, item))
end

local function drag_add(id)
    if not drag or not held or drag.set[id] then return end
    if not can_take(id, held.name) then return end
    drag.set[id] = true
    drag.slots[#drag.slots + 1] = id
end

-- How many each dragged slot would get right now (also shown while dragging)
local function drag_shares(d)
    local shares = {}
    if not d or not held or #d.slots == 0 then return shares end
    local n = #d.slots
    if d.button == "right" then
        for i = 1, math.min(n, held.count) do shares[d.slots[i]] = 1 end
    else
        local per = math.floor(held.count / n)
        if per == 0 then
            for i = 1, held.count do shares[d.slots[i]] = 1 end        -- fewer items than slots
        else
            for _, id in ipairs(d.slots) do shares[id] = per end
        end
    end
    for id, count in pairs(shares) do                                    -- never past a full stack
        local s = get_slot(id)
        shares[id] = math.min(count, max_in(id, held.name) - (s and s.count or 0))
    end
    return shares
end

local function drag_finish()
    local d = drag
    drag = nil
    if not d or not held then return end
    -- Only one slot (or none that could take it): an ordinary click where it started
    if #d.slots < 2 then
        click_slot(d.start, d.button, false)
        return
    end
    local item = held.name
    for id, n in pairs(drag_shares(d)) do
        if n > 0 and held then
            local s = get_slot(id)
            set_slot(id, { name = item, count = (s and s.count or 0) + n })
            held.count = held.count - n
            if held.count <= 0 then held = nil end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Screen
-- ---------------------------------------------------------------------------
-- The picture at a whole-number scale (crisp pixels), centred
local function layout(W, H)
    local s = math.max(1, math.floor(math.min(W * 0.9 / PIC_W, H * 0.85 / PIC_H)))
    local L = { s = s, w = PIC_W * s, h = PIC_H * s, rects = {} }
    L.x = math.floor((W - L.w) / 2)
    L.y = math.floor((H - L.h) / 2)
    local function add(id, px, py, size, kind)
        L.rects[#L.rects + 1] = { id = id, x = L.x + px * s, y = L.y + py * s, size = (size or SLOT) * s, kind = kind or "slot" }
    end
    local sc = SCREENS[screen]
    if sc.equipment then
        for _, e in ipairs(EQUIPMENT_SLOTS) do add("e:" .. e[1], e[2], e[3], SLOT, "equipment") end
    end
    for r = 0, sc.grid_w - 1 do
        for c = 0, sc.grid_w - 1 do add("g" .. (r * sc.grid_w + c + 1), sc.grid_x + c * STEP, sc.grid_y + r * STEP) end
    end
    add("output", sc.output.x, sc.output.y, sc.output.size, "output")
    if sc.bin then add("bin", BIN.x, BIN.y, SLOT, "bin") end
    for r = 0, 2 do
        for c = 0, 8 do add(r * 9 + c + 1, STORAGE.x + c * STEP, STORAGE.y + r * STEP) end
    end
    for c = 0, math.min(9, hotbar.get_slot_count()) - 1 do add("h" .. (c + 1), HOTBAR.x + c * STEP, HOTBAR.y) end
    return L
end

local function draw_stack(s, x, y, size)
    if not s then return end
    ui.draw_block_icon(s.name, x + size * 0.06, y + size * 0.06, size * 0.88)
    if s.count > 1 then
        local t = tostring(s.count)
        local scale = size / 40
        local tx, ty = x + size - ui.text_width(t, scale) - 1, y + size - ui.text_height(scale)
        ui.draw_text(t, tx + 1, ty + 1, scale, 0.15, 0.15, 0.15)   -- shadow
        ui.draw_text(t, tx, ty, scale, 1, 1, 1)
    end
end

local function draw_plain_panel(L)
    -- No picture: a plain panel with the same slots
    ui.draw_rect(L.x, L.y, L.w, L.h, 0.45, 0.44, 0.44, 0.97)
    ui.draw_text("Inventory", L.x + 70 * L.s, L.y + 3 * L.s, L.s * 0.45, 0.1, 0.1, 0.1)
    for _, r in ipairs(L.rects) do
        ui.draw_rect(r.x - L.s, r.y - L.s, r.size + 2 * L.s, r.size + 2 * L.s, 0.6, 0.58, 0.58, 1)
        ui.draw_rect(r.x, r.y, r.size, r.size, SLOT_FILL[1], SLOT_FILL[2], SLOT_FILL[3], 1)
    end
end

local previous_render_inventory = render_inventory   -- another mod's screen (chained)
function render_inventory(W, H)
    if not open then
        if previous_render_inventory then return previous_render_inventory(W, H) end
        return
    end
    local L = layout(W, H)
    ui.draw_rect(0, 0, W, H, 0, 0, 0, 0.45)                          -- dim the world
    local sc = SCREENS[screen]
    if sc.picture then ui.draw_image(sc.picture, L.x, L.y, L.w, L.h) else draw_plain_panel(L) end
    if sc.title then ui.draw_text(sc.title, L.x + 8 * L.s, L.y + 5 * L.s, L.s * 0.3, 0.25, 0.25, 0.25) end

    local mx, my, left_down, right_down = ui.get_mouse()
    do  -- creative mode: a button back to the creative inventory
        local bx, by, bw, bh = creative_button(L)
        if bx then
            local hov = mx >= bx and mx < bx + bw and my >= by and my < by + bh
            local label, ts = "< Creative", L.s * 0.28
            if not (ui.draw_button and ui.draw_button(bx, by, bw, bh, hov and "hover" or "normal")) then
                ui.draw_rect(bx, by, bw, bh, 0.2, 0.2, 0.2, 0.9)
                ui.draw_rect(bx + 1, by + 1, bw - 2, bh - 2, hov and 0.45 or 0.35, hov and 0.45 or 0.35, hov and 0.5 or 0.38, 1)
            end
            local r, g, b = 1, 1, 1
            if ui.get_button_text_color then r, g, b = ui.get_button_text_color(hov) end
            local tw = ui.text_width and ui.text_width(label, ts) or #label * 8 * ts
            local th = ui.text_height and ui.text_height(ts) or 10 * ts
            ui.draw_text(label, bx + (bw - tw) / 2, by + (bh + th) / 2, ts, r, g, b)
        end
    end
    local hover = nil
    local result = current_recipe()

    -- A drag in progress: add the slots the pointer passes over; let go to share the stack out
    if drag then
        local still = (drag.button == "left") and left_down or right_down
        if still then
            for _, r in ipairs(L.rects) do
                if (r.kind == "slot" or r.kind == "equipment") and mx >= r.x and mx < r.x + r.size and my >= r.y and my < r.y + r.size then
                    drag_add(r.id)
                end
            end
        else
            drag_finish()
        end
    end
    local shares = drag and #drag.slots >= 2 and drag_shares(drag) or {}
    for _, r in ipairs(L.rects) do
        local over = mx >= r.x and mx < r.x + r.size and my >= r.y and my < r.y + r.size
        if over then hover = r end
        local s
        if r.kind == "output" then
            s = result and { name = result.output, count = result.count } or nil
        elseif r.kind ~= "bin" then
            s = get_slot(r.id)
        end
        -- a filled slot hides the picture's outline icon (equipment, output)
        if s and (r.kind == "equipment" or r.kind == "output") then
            ui.draw_rect(r.x, r.y, r.size, r.size, SLOT_FILL[1], SLOT_FILL[2], SLOT_FILL[3], 1)
        end
        -- while dragging, a slot shows what it would have after letting go
        if shares[r.id] and held then
            s = { name = held.name, count = (s and s.count or 0) + shares[r.id] }
        end
        draw_stack(s, r.x, r.y, r.size)
        if shares[r.id] then ui.draw_rect(r.x, r.y, r.size, r.size, 1, 1, 1, 0.18)
        elseif over then ui.draw_rect(r.x, r.y, r.size, r.size, 1, 1, 1, 0.25) end   -- highlight
    end

    -- Name of what's under the pointer (or what the slot is for)
    if hover and not held then
        local label
        if hover.kind == "output" then label = result and result.output
        elseif hover.kind == "bin" then label = nil
        else
            local s = get_slot(hover.id)
            label = s and s.name or (hover.kind == "equipment" and hover.id:sub(3))
        end
        if label then
            label = label:gsub("_", " ")
            local scale = L.s * 0.3
            local w = ui.text_width(label, scale) + 12
            ui.draw_rect(mx + 14, my - 8, w, ui.text_height(scale) + 12, 0.1, 0.05, 0.15, 0.92)
            ui.draw_text(label, mx + 20, my - 2, scale, 1, 1, 1)
        end
    end
    if held then
        local left = held.count
        for _, n in pairs(shares) do left = left - n end
        if left > 0 then draw_stack({ name = held.name, count = left }, mx - 8 * L.s, my - 8 * L.s, SLOT * L.s) end
    end
end

local previous_inventory_click = handle_inventory_click
function handle_inventory_click(mx, my, W, H, button, shift)
    if not open then
        if previous_inventory_click then return previous_inventory_click(mx, my, W, H, button, shift) end
        return false
    end
    local L = layout(W, H)
    button = button or "left"
    do  -- back to the creative inventory (creative mode)
        local bx, by, bw, bh = creative_button(L)
        if bx and mx >= bx and mx < bx + bw and my >= by and my < by + bh then
            inv.set_open(false)                       -- what's on the cursor goes back first
            if engine.set_inventory_open then engine.set_inventory_open(false) end
            hotbar.set_creative_open(true)
            return true
        end
    end
    for _, r in ipairs(L.rects) do
        if mx >= r.x and mx < r.x + r.size and my >= r.y and my < r.y + r.size then
            if r.kind == "output" then click_output(button, shift)
            elseif r.kind == "bin" then click_bin(button)
            elseif held and not shift then
                -- holding a stack: wait to see whether this is a click or a drag (render_inventory
                -- adds slots while the button stays down, and shares the stack out on release)
                drag = { button = button, start = r.id, slots = {}, set = {} }
                drag_add(r.id)                     -- only if this slot can take the stack
            else click_slot(r.id, button, shift) end
            return true
        end
    end
    return true
end

-- E in survival: open/close (the engine frees the mouse while it's open)
local previous_toggle = toggle_main_inventory
function toggle_main_inventory()
    -- E or Esc with a screen of ours open (the inventory or a workbench, in any mode): close it
    if open then return inv.set_open(false) end
    if survival.is_dead() then return false end
    if survival.get_mode() ~= "survival" then
        if previous_toggle then return previous_toggle() end
        return false
    end
    return inv.set_open(true)
end

-- ---------------------------------------------------------------------------
-- Workbenches: right-click one to craft on a 3x3 grid (sneak + right-click with a block in
-- hand places it against the workbench instead, like Minecraft)
-- ---------------------------------------------------------------------------
local workbenches = {}
function survival.register_workbench(block) workbenches[block] = true end
survival.register_workbench("workbench")

local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if workbenches[block] and not survival.is_dead() then
        local holding_something = held and held ~= "" and held ~= "air"
        if not (sneaking and holding_something) then
            if not repeat_click then inv.open_workbench() end
            return true
        end
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end

-- Pick block (middle mouse) in survival, like Minecraft: select the block if it's in the
-- hotbar; otherwise move it from storage into the hotbar (the selected slot if empty, else the
-- next empty one, else swap with the selected slot) and select it. Not in your inventory: nothing.
-- In creative the engine's own pick block runs instead (it gives you the block).
function inv.pick(name)
    local n = hotbar.get_slot_count()
    for i = 1, n do
        if hotbar.get_slot(i) == name then hotbar.set_active_slot(i) return true end
    end
    local from = nil
    for i = 1, SIZE do
        if slots[i] and slots[i].name == name then from = i break end
    end
    if not from then return false end

    local active = hotbar.get_active_slot()
    local to = active
    if hotbar.get_slot(active) ~= "" then
        for k = 1, n - 1 do
            local i = (active - 1 + k) % n + 1
            if hotbar.get_slot(i) == "" then to = i break end
        end
    end
    local old_name, old_count = hotbar.get_slot(to)
    local s = slots[from]
    hotbar.set_slot(to, s.name, s.count)
    slots[from] = (old_name ~= "" and old_count > 0) and { name = old_name, count = old_count } or nil
    hotbar.set_active_slot(to)
    save()
    return true
end

local previous_pick = on_pick_block
function on_pick_block(x, y, z, item, meta, ...)
    if survival.get_mode() == "survival" then
        if not survival.is_dead() and not open then inv.pick(item) end
        return true                -- survival never conjures blocks, found or not
    end
    if previous_pick then return previous_pick(x, y, z, item, meta, ...) end
    return false
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

-- ---------------------------------------------------------------------------
-- Starting recipes, only for items this game has (games add their own with register_craft)
-- ---------------------------------------------------------------------------
local function has(name) return engine.get_block_id and engine.get_block_id(name) ~= 0 end
local woods = { "oak", "birch", "spruce", "jungle", "acacia", "dark_oak", "cherry" }
local planks_any = nil
for _, wood in ipairs(woods) do
    -- 1 log -> 4 planks of the same wood ("log_oak" or "oak_log" -> "planks_oak" or "oak_planks")
    local log = has("log_" .. wood) and ("log_" .. wood) or (has(wood .. "_log") and (wood .. "_log"))
    local planks = has("planks_" .. wood) and ("planks_" .. wood) or (has(wood .. "_planks") and (wood .. "_planks"))
    if log and planks then
        survival.register_craft({ output = planks, count = 4, recipe = { { log } } })
    end
    planks_any = planks_any or planks
    -- 4 planks in a square -> a workbench (fits the inventory's 2x2 grid, like Minecraft)
    if planks and has("workbench") then
        survival.register_craft({ output = "workbench", count = 1, recipe = { { planks, planks }, { planks, planks } } })
    end
    -- 2 planks stacked -> 4 sticks
    if planks and has("stick") then
        survival.register_craft({ output = "stick", count = 4, recipe = { { planks }, { planks } } })
    end
end
