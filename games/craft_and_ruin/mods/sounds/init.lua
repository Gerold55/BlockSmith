-- Default sounds: footsteps, digging, breaking and placing for every block, by material.
--
-- Blocks registered with their own `sounds = {...}` keep them. Everything else gets a set
-- chosen from its name (stone, dirt, wood, sand, leaves). The sounds in sounds/ are simple
-- placeholders: drop in your own files with the same names (.ogg or .wav; add .1, .2, .3
-- variants and one is picked at random each time) and they're used instead.
--
-- Other mods:
--   default_sounds.sets.stone / dirt / wood / sand / leaves   - the sound tables, to reuse
--   default_sounds.material(name) -> "stone" | ... | nil        - what this mod thinks a block is
--   default_sounds.rules  - { { pattern, set }, ... } checked in order; add your own first

sounds = default_sounds or {}

local function set(kind, gains)
    gains = gains or {}
    return {
        footstep = { name = "default_" .. kind .. "_footstep", gain = gains.footstep or 0.5 },
        dig      = { name = "default_" .. kind .. "_dig",      gain = gains.dig or 0.6 },
        dug      = { name = "default_" .. kind .. "_dug",      gain = gains.dug or 0.9 },
        place    = { name = "default_place",                   gain = 0.8 },
    }
end

default_sounds.sets = {
    stone  = set("stone"),
    dirt   = set("dirt"),
    wood   = set("wood"),
    sand   = set("sand"),
    leaves = { footstep = { name = "default_leaves_footstep", gain = 0.4 },
               dig = { name = "default_leaves_footstep", gain = 0.5 },
               dug = { name = "default_leaves_dug", gain = 0.8 },
               place = { name = "default_place", gain = 0.6 } },
}

-- First match wins (names are matched as plain text, lower case)
default_sounds.rules = {
    { "leaves", "leaves" }, { "sapling", "leaves" }, { "flower", "leaves" }, { "short_grass", "leaves" },
    { "grass_block", "dirt" },
    { "sandstone", "stone" },
    { "sand", "sand" }, { "snow", "sand" },
    { "log", "wood" }, { "plank", "wood" }, { "wood", "wood" }, { "door", "wood" }, { "fence", "wood" },
    { "chest", "wood" }, { "table", "wood" }, { "bookshelf", "wood" },
    { "dirt", "dirt" }, { "grass", "dirt" }, { "farmland", "dirt" }, { "clay", "dirt" },
    { "gravel", "dirt" }, { "mud", "dirt" },
    { "stone", "stone" }, { "cobble", "stone" }, { "brick", "stone" }, { "ore", "stone" },
    { "obsidian", "stone" }, { "glass", "stone" }, { "granite", "stone" }, { "andesite", "stone" },
    { "diorite", "stone" }, { "deepslate", "stone" }, { "bedrock", "stone" }, { "ice", "stone" },
}

function default_sounds.material(name)
    local n = name:lower()
    for _, rule in ipairs(default_sounds.rules) do
        if n:find(rule[1], 1, true) then return rule[2] end
    end
    return nil
end

-- After every mod has registered its blocks: give the silent ones sounds
local function apply()
    local given = 0
    for _, name in ipairs(engine.list_blocks()) do
        local def = engine.get_block_def(name)
        if def and not def.has_sounds and not def.item then
            local mat = default_sounds.material(name)
            if mat and default_sounds.sets[mat] then
                engine.override_block(name, { sounds = default_sounds.sets[mat] })
                given = given + 1
            end
        end
    end
    print("[Sounds] Gave " .. given .. " blocks their default sounds")
end

-- Water: a splash when you fall or jump in
local was_in_fluid, applied = true, false
local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end
    if engine.is_server and engine.is_server() then return end
    if not applied then applied = true; apply() end
    local p = engine.get_player_state and engine.get_player_state()
    if not p then return end
    local in_fluid = p.feet_fluid == "water"
    if in_fluid and not was_in_fluid and p.vy < -2 then
        sound.play("default_water_splash", { pos = { x = p.x, y = p.y, z = p.z },
                                             gain = math.min(1, -p.vy / 12), pitch = 0.9 + math.random() * 0.2 })
    end
    was_in_fluid = in_fluid
end
