-- grass: grass spreads onto dirt and dies when covered - Minecraft's way. It's a random tick
-- (engine.register_random_tick): every game tick a few random blocks near players are picked,
-- and when one is a grass block this runs. So grass creeps outwards smoothly and steadily,
-- like Minecraft's, not in jumps. How fast: the randomTickSpeed game rule (3, as in Minecraft).
-- It runs only where the world is decided (singleplayer, hosting, the server); everyone else
-- sees the blocks change.

-- Something on top that smothers grass: a solid block light can't get through, or water / lava.
-- Leaves, glass, plants, flowers and the like don't.
local function smothered(x, y, z)
    if engine.is_opaque and engine.is_opaque(x, y, z) then return true end
    local above = engine.get_block_name(x, y, z)
    return above:find("water", 1, true) ~= nil or above:find("lava", 1, true) ~= nil
end

local function light(x, y, z)
    if not engine.get_light_level then return 15 end
    return engine.get_light_level(x, y, z)
end

engine.register_random_tick({
    label = "grass spreads and dies",
    nodenames = { "grass" },
    action = function(x, y, z)
        -- Covered: back to dirt
        if smothered(x, y + 1, z) then
            engine.set_block(x, y, z, "dirt")
            return
        end
        -- Lit enough (9 or more above it): try 4 random spots nearby - from 3 blocks below to 1
        -- above, one block each way - and turn dirt there into grass if its top is open and lit
        if light(x, y + 1, z) < 9 then return end
        for _ = 1, 4 do
            local tx, ty, tz = x + math.random(-1, 1), y + math.random(-3, 1), z + math.random(-1, 1)
            if engine.get_block_name(tx, ty, tz) == "dirt" and not smothered(tx, ty + 1, tz)
               and light(tx, ty + 1, tz) >= 4 then
                engine.set_block(tx, ty, tz, "grass")
            end
        end
    end,
})
