-- mirrors.lua: mirrors any game can have - hung on a wall (or stood on the floor), showing the
-- room reflected, the player too. Part of the base mod. The engine does the reflecting (blocks
-- registered with `mirror`); this file adds ready-made frames, the item and placing.
--
--   mirrors.register("mymod:mirror", {
--       description = "Mirror",
--       frame = "wood",                -- "thin" (default), "wood", "gold", "none" (frameless),
--                                      -- a frame from mirrors.register_frame, or your own picture
--                                      -- ("myframe.png": see-through pixels are the glass)
--       inventory_image = "my.png",    -- the item (default: the frame's own item picture)
--       -- optional: tint = "#e8c89a" (the reflection coloured: bronze, smoked glass),
--       --           clarity = 0.6 (1 clear .. 0 fogged over: misted or old glass),
--       --           glass = "#9fb0c0" (the glass where nothing is reflected),
--       --           stand = false (only on walls), hardness, tool, sounds, light, walkable
--   })
--
--   mirrors.register_frame("marble", { picture = "marble_frame.png", item = "marble_mirror.png" })
--
-- Placing: on the side of a block it hangs on that wall, facing out; on a floor or ceiling it
-- stands facing the player (unless stand = false). Mirrors side by side on one wall make one big
-- mirror, and their frames join up.
--
-- Functions:
--   mirrors.place(x, y, z, name [, facing])  facing "north", "east", "south" (default), "west":
--       the way the glass looks. Where the world runs (world generation, structures, commands).
--   mirrors.get_facing(x, y, z) -> "north".."west", or nil if there's no mirror there
--   mirrors.is_mirror(x, y, z) -> bool   (any mirror, also ones registered with register_block)
--   mirrors.get(name) -> the definition given to mirrors.register, or nil
--   mirrors.registered -> { name = def, ... },  mirrors.frames -> { id = { picture, item }, ... }

mirrors = mirrors or {}
local M = mirrors
M.registered = M.registered or {}
M.frames = M.frames or {
    none = { picture = "mirror_frame_none.png", item = "mirror_item_none.png" },
    thin = { picture = "mirror_frame_thin.png", item = "mirror_item_thin.png" },
    wood = { picture = "mirror_frame_wood.png", item = "mirror_item_wood.png" },
    gold = { picture = "mirror_frame_gold.png", item = "mirror_item_gold.png" },
}

local FACINGS = { north = 0, east = 1, south = 2, west = 3 }
local NAMES = { [0] = "north", "east", "south", "west" }

function M.register_frame(id, def)
    assert(type(id) == "string" and id ~= "", "mirrors.register_frame: an id")
    assert(type(def) == "table" and type(def.picture) == "string", "mirrors.register_frame(id, { picture = \"...png\" })")
    M.frames[id] = { picture = def.picture, item = def.item or def.picture }
end

function M.get(name) return M.registered[name] end

function M.is_mirror(x, y, z)
    local st = engine.get_block_state and engine.get_block_state(x, y, z)
    return st ~= nil and st.mirror == true
end

function M.get_facing(x, y, z)
    local st = engine.get_block_state and engine.get_block_state(x, y, z)
    if st and st.mirror then return st.facing end
    return nil
end

function M.place(x, y, z, name, facing)
    local f = facing or "south"
    if type(f) == "string" then f = FACINGS[f] end
    assert(f ~= nil and f >= 0 and f <= 3, "mirrors.place: facing is \"north\", \"east\", \"south\" or \"west\"")
    return engine.place_block(x, y, z, name, { facing = NAMES[f] })
end

function M.register(name, def)
    assert(type(name) == "string" and name ~= "", "mirrors.register: a name")
    def = def or {}
    def.name = name
    local frame = def.frame or "thin"
    local known = M.frames[frame]
    local picture = known and known.picture or frame          -- a named frame, or a picture of the mod's own
    local item = def.inventory_image or (known and known.item) or M.frames.thin.item
    local props = {
        mirror = { tint = def.tint, clarity = def.clarity, glass = def.glass },
        description = def.description or "Mirror",
        inventory_image = item,
        hardness = def.hardness or 0.5,
        tool = def.tool,
        sounds = def.sounds,
        light = def.light,
        walkable = def.walkable,
        in_creative = def.in_creative,
    }
    engine.register_block(name, 1, 1, 1, picture, nil, props)
    M.registered[name] = def

    -- Placing: on a wall (facing out of it), or on a floor or ceiling (facing the player)
    engine.register_item_use(name, function(pointed)
        local under, above = pointed.under, pointed.above
        if not under or not above then return false end
        if engine.is_replaceable and not engine.is_replaceable(above.x, above.y, above.z) then return true end
        local dx, dy, dz = above.x - under.x, above.y - under.y, above.z - under.z
        local facing
        if dy == 0 then
            facing = (dz < 0 and 0) or (dx > 0 and 1) or (dz > 0 and 2) or 3
        else
            if def.stand == false then return true end
            -- the glass toward the player (yaw 0 looks along +x, 90 along +z)
            local yaw = engine.get_player_look and engine.get_player_look() or 0
            local lx, lz = math.cos(math.rad(yaw)), math.sin(math.rad(yaw))
            local look = math.abs(lx) > math.abs(lz) and (lx > 0 and 1 or 3) or (lz > 0 and 2 or 0)
            facing = (look + 2) % 4
        end
        engine.set_block_meta(above.x, above.y, above.z, name, facing)
        if not (engine.is_creative and engine.is_creative()) and pointed.slot and hotbar and hotbar.set_slot then
            if (pointed.count or 1) > 1 then hotbar.set_slot(pointed.slot, name, pointed.count - 1)
            else hotbar.set_slot(pointed.slot, "", 0) end
        end
        return true
    end)
    return def
end
