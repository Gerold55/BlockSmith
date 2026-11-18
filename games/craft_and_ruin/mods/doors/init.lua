-- Doors: two blocks tall, open and close with right-click, swing toward their hinge.
-- The engine draws a door as a thin panel and knows its collision (pattern = "door", see
-- DoorShape.h); this mod decides how doors are placed, opened and broken.
--
--   doors.register("door_oak", {
--       top = "cw_oak_door_top.png",          -- upper half picture
--       bottom = "cw_oak_door_bottom.png",    -- lower half picture
--       inventory_image = "door_oak_item.png",
--       hardness = 3, tool = "axe",           -- survival digging (optional)
--       sound_open = "default_wood_dig",      -- optional
--       sound_close = "default_wood_dig",
--   })
--   doors.is_door(name), doors.is_open(x, y, z), doors.set_open(x, y, z, open)
--
-- Metadata of each half (DoorShape.h): bits 0-1 facing, 4 open, 8 upper half, 16 hinge right.

doors = doors or {}
local registered = {}

local FACING, OPEN, UPPER, HINGE_RIGHT = 3, 4, 8, 16
local function has(meta, bit) return math.floor(meta / bit) % 2 == 1 end
local function with(meta, bit, on)
    if has(meta, bit) == on then return meta end
    return on and meta + bit or meta - bit
end

function doors.is_door(name) return registered[name] ~= nil end

function doors.register(name, def)
    def = def or {}
    registered[name] = def
    engine.register_block(name, 1.0, 1.0, 1.0, {
        top = def.top, bottom = def.bottom, sides = def.bottom,
    }, nil, {
        pattern = "door",
        walkable = true,
        selectable = true,
        inventory_image = def.inventory_image,   -- a flat picture in the hotbar, never placed as a cube
        hardness = def.hardness or 3,
        tool = def.tool or "axe",
        in_creative = def.in_creative ~= false,
    })
end

-- Facing from where the player is looking: 0 north (-Z), 1 east (+X), 2 south (+Z), 3 west (-X)
local function player_facing()
    local p = engine.get_player_state and engine.get_player_state()
    local yaw = p and p.yaw or 0
    local fx, fz = math.cos(math.rad(yaw)), math.sin(math.rad(yaw))
    if math.abs(fx) > math.abs(fz) then return fx > 0 and 1 or 3 end
    return fz > 0 and 2 or 0
end

local DIRS = { [0] = { 0, -1 }, [1] = { 1, 0 }, [2] = { 0, 1 }, [3] = { -1, 0 } }

local function free(x, y, z)
    if engine.is_replaceable then return engine.is_replaceable(x, y, z) end
    return engine.get_block_name(x, y, z) == "air"
end

local function play(def, which, x, y, z)
    local name = def["sound_" .. which] or "default_wood_dig"
    if sound and sound.play then
        sound.play(name, { pos = { x = x + 0.5, y = y + 0.5, z = z + 0.5 }, gain = 0.8,
                           pitch = which == "open" and 1.0 or 0.85 })
    end
end

-- The other half of the door at (x, y, z), or nil
local function partner(x, y, z, name, meta)
    local py = has(meta, UPPER) and y - 1 or y + 1
    if engine.get_block_name(x, py, z) == name then return py end
    return nil
end

function doors.is_open(x, y, z)
    return has(engine.get_metadata(x, y, z), OPEN)
end

function doors.set_open(x, y, z, open)
    local name = engine.get_block_name(x, y, z)
    local def = registered[name]
    if not def then return false end
    local meta = engine.get_metadata(x, y, z)
    if has(meta, OPEN) == open then return true end
    engine.set_block_meta(x, y, z, name, with(meta, OPEN, open))
    local py = partner(x, y, z, name, meta)
    if py then engine.set_block_meta(x, py, z, name, with(engine.get_metadata(x, py, z), OPEN, open)) end
    play(def, open and "open" or "close", x, y, z)
    return true
end

-- Place a door with its lower half at (x, y, z)
local function place(name, x, y, z)
    if not free(x, y, z) or not free(x, y + 1, z) then return false end
    local facing = player_facing()
    -- Double doors: if there's a left-hinged door on our left, hinge this one on the right
    local d = DIRS[facing]
    local lx, lz = x + d[2], z - d[1]          -- the block to the placer's left
    local meta = facing
    local left_name = engine.get_block_name(lx, y, lz)
    if registered[left_name] then
        local lm = engine.get_metadata(lx, y, lz)
        if lm % 4 == facing and not has(lm, HINGE_RIGHT) then meta = meta + HINGE_RIGHT end
    end
    engine.set_block_meta(x, y, z, name, meta)
    engine.set_block_meta(x, y + 1, z, name, meta + UPPER)
    play(registered[name], "close", x, y, z)
    return true
end

-- Right-click: open/close a door, or place one from the hotbar.
-- Like Minecraft: sneaking with a block in hand places it against the door instead of opening
-- it, and holding the button doesn't make the door flap open and shut.
local previous_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if registered[block] then
        local holding_something = held and held ~= "" and held ~= "air"
        if not (sneaking and holding_something) then
            if not repeat_click then doors.set_open(x, y, z, not doors.is_open(x, y, z)) end
            return true
        end
    end
    if registered[held] then
        -- On the floor: on top of the block you clicked (or in place of a plant you clicked)
        local by = free(x, y, z) and y or y + 1
        if place(held, x, by, z) then
            if not (engine.is_creative and engine.is_creative()) then
                local slot = hotbar.get_active_slot()
                local item, count = hotbar.get_slot(slot)
                if item == held then hotbar.set_slot(slot, count > 1 and held or "", count - 1) end
            end
        end
        return true
    end
    if previous_click then return previous_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end

-- Breaking one half removes the other (only the half you broke drops the door)
local previous_broken = on_block_broken
function on_block_broken(x, y, z, block, meta)
    if registered[block] then
        local py = has(meta, UPPER) and y - 1 or y + 1
        if engine.get_block_name(x, py, z) == block then engine.set_block(x, py, z, "air") end
    end
    if previous_broken then return previous_broken(x, y, z, block, meta) end
end

-- ---------------------------------------------------------------------------
-- Craft & Ruin's doors
-- ---------------------------------------------------------------------------
doors.register("door_oak", {
    top = "cw_oak_door_top.png",
    bottom = "cw_oak_door_bottom.png",
    inventory_image = "door_oak_item.png",
})

-- 6 oak planks at a workbench -> 3 doors. Added on the first update, once every mod has
-- loaded (the survival mod, which does crafting, loads after this one).
local recipes_added = false
local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end
    if recipes_added then return end
    recipes_added = true
    if survival and survival.register_craft and engine.get_block_id("planks_oak") ~= 0 then
        -- 6 oak planks, 2 wide and 3 tall, at a workbench -> 3 doors (like Minecraft)
        survival.register_craft({ output = "door_oak", count = 3,
                                  recipe = { { "planks_oak", "planks_oak" },
                                             { "planks_oak", "planks_oak" },
                                             { "planks_oak", "planks_oak" } } })
    end
end

print("[Doors] Ready")
