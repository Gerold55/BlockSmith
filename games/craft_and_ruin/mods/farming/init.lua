-- Craft & Ruin farming: Minecraft's farming, plus a scythe that replants.
--
--   Hoes (wooden, stone): right-click grass or dirt with air above it -> farmland
--   Farmland: wet with water within 4 blocks (darker), dries out without; bare dry farmland goes
--     back to dirt; jumping or falling onto it tramples it back to dirt
--   Seeds: right-click farmland to plant. You find them by breaking tall grass (1 in 8)
--   Crops grow on random ticks (engine.register_random_tick) the way Minecraft's do: faster on
--     wet farmland and in neat rows, only with light 9 or more, and they pop off if the farmland
--     under them goes
--   Harvest: break a ripe crop for its harvest (wheat: 1 wheat and 0-3 seeds); an unripe one
--     only gives its seed back. A SCYTHE reaps every ripe crop in the 3x3 patch you click and
--     replants them (one seed each goes back in the ground) - on anything else it's a normal tool
--   Bread: 3 wheat in a row
--
-- Other mods add crops with farming.register_crop (below).

farming = farming or {}

local function in_creative() return engine.is_creative and engine.is_creative() end
local function authority() return not engine.is_authority or engine.is_authority() end

-- Items into the world: where the world is decided (singleplayer, hosting, the server) we make
-- them; joined to a server, engine.drop_item asks the server to. Returns true if the item was
-- delivered, by each path's own answer: entities.drop_item gives the new item's id (0 or nil =
-- none was made, e.g. the world is at its entity limit); engine.drop_item gives true / false.
-- Nothing to drop counts as delivered.
local function drop(item, count, x, y, z)
    if not item or count <= 0 then return true end
    if authority() and entities and entities.drop_item then
        local id = entities.drop_item(item, count, x + 0.5, y + 0.25, z + 0.5)
        return id ~= nil and id ~= 0
    elseif engine.drop_item then
        return engine.drop_item(item, count, x + 0.5, y + 0.25, z + 0.5) == true
    end
    return false
end

-- Harvest rewards that haven't reached the world yet: { item, count, x, y, z }. A batch is
-- delivered in one go; whatever its drop path doesn't deliver waits here and is retried (once a
-- second, in on_update) until it is. Each entry leaves the list only once it's delivered, so
-- nothing is given twice. Entries for the same item at the same spot are merged, up to a stack.
local undelivered = {}
local STACK = 64

local function keep_undelivered(item, count, x, y, z)
    for _, u in ipairs(undelivered) do
        if u[1] == item and u[3] == x and u[4] == y and u[5] == z and u[2] < STACK then
            local add = math.min(count, STACK - u[2])
            u[2] = u[2] + add
            count = count - add
            if count <= 0 then return end
        end
    end
    while count > 0 do
        local n = math.min(count, STACK)
        undelivered[#undelivered + 1] = { item, n, x, y, z }
        count = count - n
    end
end

-- Delivers a batch of rewards ({ item, count, x, y, z } each); the ones that don't make it are kept
local function deliver(batch)
    for _, r in ipairs(batch) do
        if not drop(r[1], r[2], r[3], r[4], r[5]) then keep_undelivered(r[1], r[2], r[3], r[4], r[5]) end
    end
end

-- Tries the waiting rewards again; keeps only the ones still not delivered
local function retry_undelivered()
    if #undelivered == 0 then return end
    local waiting = undelivered
    undelivered = {}
    for _, r in ipairs(waiting) do
        if not drop(r[1], r[2], r[3], r[4], r[5]) then undelivered[#undelivered + 1] = r end
    end
end
farming.undelivered_count = function() return #undelivered end

local function is_farmland(name) return name == "farmland_dry" or name == "farmland_wet" end

-- ---------------------------------------------------------------------------
-- Farmland
-- ---------------------------------------------------------------------------
engine.register_block("farmland_dry", 1, 1, 1, { top = "cw_farming_soil.png", sides = "cw_dirt.png", bottom = "cw_dirt.png" }, nil, {
    walkable = true, pattern = "cube", in_creative = true, pick = "dirt", drops = "dirt", hardness = 0.6, tool = "shovel",
})
engine.register_block("farmland_wet", 1, 1, 1, { top = "cw_farming_soil_wet.png", sides = "cw_dirt.png", bottom = "cw_dirt.png" }, nil, {
    walkable = true, pattern = "cube", in_creative = true, pick = "dirt", drops = "dirt", hardness = 0.6, tool = "shovel",
})

-- ---------------------------------------------------------------------------
-- Items: tools, seeds, harvest (items have an inventory picture, so they're never placed as
-- blocks - the old scythe and seeds were plant blocks, which is why they could be put down)
-- ---------------------------------------------------------------------------
local function item(name, picture, extra)
    local opts = { inventory_image = picture }
    for k, v in pairs(extra or {}) do opts[k] = v end
    engine.register_block(name, 1, 1, 1, picture, nil, opts)
end
item("wooden_hoe", "farming_hoe_wood.png", { tool_type = "hoe" })
item("stone_hoe", "farming_hoe_stone.png", { tool_type = "hoe" })
item("scythe", "farming_scythe.png", { tool_type = "scythe" })
item("wheat_seeds", "cw_farming_seed_wheat.png")
item("wheat", "farming_wheat.png")
item("bread", "farming_bread.png")

-- ---------------------------------------------------------------------------
-- Crops
-- ---------------------------------------------------------------------------
-- farming.register_crop{
--     name = "wheat",
--     stages = { "wheat_stage_0", "wheat_stage_1", "wheat_stage_2", "wheat_ripe" },   -- block names
--     textures = { "a.png", "b.png", ... },     -- one per stage (registers the stage blocks)
--     seed = "wheat_seeds",                     -- what plants it (and what an unripe one gives back)
--     harvest = { { "wheat", 1, 1 }, { "wheat_seeds", 0, 3 } },   -- a ripe one: item, fewest, most
--     scythe_replants = true,                   -- a scythe reaps it and plants it again
-- }
local crops = {}          -- stage block name -> { def = crop, stage = n }
local pop_crop            -- (below)

-- ---------------------------------------------------------------------------
-- Growing: Minecraft's random-tick growth. Each crop gets "growth points": 1, plus the farmland
-- under it (1 dry, 3 wet), plus a quarter of that for each farmland block around it. The chance
-- of growing a stage on a random tick is 1 in (25 / points + 1) - doubled here, as our crops have
-- half Minecraft's 8 stages. Wet farmland in a field: about 1 in 2; one crop on dry: 1 in 13.
-- ---------------------------------------------------------------------------
local function growth_points(x, y, z)
    local points = 1
    for dx = -1, 1 do for dz = -1, 1 do
        local n = engine.get_block_name(x + dx, y - 1, z + dz)
        local g = (n == "farmland_wet") and 3 or (n == "farmland_dry") and 1 or 0
        if dx ~= 0 or dz ~= 0 then g = g / 4 end
        points = points + g
    end end
    return points
end

local function grow_tick(x, y, z, name)
    local c = crops[name]
    if not c then return end
    if not is_farmland(engine.get_block_name(x, y - 1, z)) then pop_crop(x, y, z, name) return end
    if c.stage >= #c.def.stages then return end                       -- ripe
    if engine.get_light_level and engine.get_light_level(x, y, z) < 9 then return end
    local chance = 2 / (math.floor(25 / growth_points(x, y, z)) + 1)
    if math.random() < chance then engine.set_block(x, y, z, c.def.stages[c.stage + 1]) end
end

farming.crops = {}
function farming.register_crop(def)
    assert(def.name and def.stages and def.seed, "farming.register_crop needs name, stages and seed")
    farming.crops[def.name] = def
    for i, stage in ipairs(def.stages) do
        if def.textures and def.textures[i] then
            engine.register_block(stage, 1, 1, 1, def.textures[i], nil, {
                transparent = true, walkable = false, selectable = true, pattern = "plantlike",
                in_creative = false, pick = def.seed, hardness = 0, drops = "",
            })
        end
        crops[stage] = { def = def, stage = i }
    end
    -- it grows on random ticks (Minecraft's way; see grow_tick)
    if engine.register_random_tick then
        engine.register_random_tick({ label = def.name .. " grows", nodenames = def.stages, action = grow_tick })
    end
    -- seeds plant it on farmland (right-click; the game's - a server has no item uses)
    if engine.register_item_use then engine.register_item_use(def.seed, function(pointed)
        local u = pointed.under
        if not u or not is_farmland(u.name) then return true end   -- seeds only go in farmland
        if engine.get_block_name(u.x, u.y + 1, u.z) ~= "air" then return true end
        engine.set_block(u.x, u.y + 1, u.z, def.stages[1])
        if not in_creative() then
            if pointed.count > 1 then hotbar.set_slot(pointed.slot, def.seed, pointed.count - 1)
            else hotbar.set_slot(pointed.slot, "", 0) end
        end
        return true
    end) end
end
function farming.is_crop(name) return crops[name] ~= nil end
function farming.is_ripe(name) local c = crops[name] return c ~= nil and c.stage == #c.def.stages end

farming.register_crop({
    name = "wheat",
    stages = { "wheat_stage_0", "wheat_stage_1", "wheat_stage_2", "wheat_ripe" },   -- (the names old worlds use)
    textures = { "cw_farming_wheat_1.png", "cw_farming_wheat_2.png", "cw_farming_wheat_3.png", "cw_farming_wheat_4.png" },
    seed = "wheat_seeds",
    harvest = { { "wheat", 1, 1 }, { "wheat_seeds", 0, 3 } },
    scythe_replants = true,
})

-- What a crop gives: ripe -> its harvest; not ripe -> its seed back. `minus_seed` = one seed is
-- kept back (it goes straight back in the ground - the scythe)
local function crop_drops(name, minus_seed)
    local c = crops[name]
    if not c then return {} end
    local out = {}
    if c.stage < #c.def.stages then
        if not minus_seed then out[#out + 1] = { c.def.seed, 1 } end
        return out
    end
    local seed_kept = not minus_seed
    for _, h in ipairs(c.def.harvest or {}) do
        local n = h[2] + math.random(0, math.max(0, (h[3] or h[2]) - h[2]))
        if minus_seed and h[1] == c.def.seed and not seed_kept then n = n - 1; seed_kept = true end
        if n > 0 then out[#out + 1] = { h[1], n } end
    end
    return out
end

function pop_crop(x, y, z, name)              -- a crop that can't stay: it breaks (and drops as if broken)
    engine.set_block(x, y, z, "air")
    for _, d in ipairs(crop_drops(name, false)) do drop(d[1], d[2], x, y, z) end
end

-- ---------------------------------------------------------------------------
-- Farmland: wet near water, dries out, bare dry farmland returns to dirt (random ticks too)
-- ---------------------------------------------------------------------------
local function water_near(x, y, z)
    for dx = -4, 4 do for dz = -4, 4 do for dy = 0, 1 do
        local n = engine.get_block_name(x + dx, y + dy, z + dz)
        if n == "water" or n == "flowing_water" then return true end
    end end end
    return false
end
check_water_nearby = water_near          -- (the old global name, for other mods)

engine.register_random_tick({
    label = "farmland moisture",
    nodenames = { "farmland_dry", "farmland_wet" },
    action = function(x, y, z, name)
        local above = engine.get_block_name(x, y + 1, z)
        if engine.is_opaque and engine.is_opaque(x, y + 1, z) then engine.set_block(x, y, z, "dirt") return end
        if water_near(x, y, z) then
            if name ~= "farmland_wet" then engine.set_block(x, y, z, "farmland_wet") end
        elseif name == "farmland_wet" then
            engine.set_block(x, y, z, "farmland_dry")
        elseif not crops[above] then
            engine.set_block(x, y, z, "dirt")                              -- bare and dry: back to dirt
        end
    end,
})

-- The farmland under a crop broken: the crop pops off (where the world is decided; a joined
-- player's game leaves it to the server)
local previous_broken = on_block_broken
function on_block_broken(x, y, z, block, ...)
    if is_farmland(block) and authority() then
        local above = engine.get_block_name(x, y + 1, z)
        if crops[above] then pop_crop(x, y + 1, z, above) end
    end
    if previous_broken then return previous_broken(x, y, z, block, ...) end
end

-- ---------------------------------------------------------------------------
-- Hoes: till grass and dirt (air above it, like Minecraft)
-- ---------------------------------------------------------------------------
local previous_hoe = on_use_hoe
function on_use_hoe(x, y, z, block_name, ...)
    if (block_name == "grass" or block_name == "dirt") and engine.get_block_name(x, y + 1, z) == "air" then
        engine.set_block(x, y, z, "farmland_dry")
        return true
    end
    if previous_hoe then return previous_hoe(x, y, z, block_name, ...) end
    return false
end

-- ---------------------------------------------------------------------------
-- Harvesting by breaking (survival), and seeds from tall grass
-- ---------------------------------------------------------------------------
local previous_dig = on_dig_block
function on_dig_block(x, y, z, block, item, ...)
    if crops[block] then
        for _, d in ipairs(crop_drops(block, false)) do drop(d[1], d[2], x, y, z) end
        if previous_dig then previous_dig(x, y, z, block, item, ...) end
        return true                                                         -- (we dropped them)
    end
    if block == "short_grass" then
        if math.random(1, 8) == 1 then drop("wheat_seeds", 1, x, y, z) end   -- Minecraft: 1 in 8
        if previous_dig then previous_dig(x, y, z, block, item, ...) end
        return true
    end
    if previous_dig then return previous_dig(x, y, z, block, item, ...) end
    return false
end

-- ---------------------------------------------------------------------------
-- The scythe: reaps every ripe crop in the 3x3 patch and replants each (the seed for it is
-- kept back from the harvest). Anything else: false, and the scythe digs like any tool.
--
-- Who does it: where the world is decided. Singleplayer (and the host's own player) reaps right
-- here. A player joined to a server only *asks* (engine.request_harvest); the server's copy of
-- this mod reaps in on_player_harvest from its own world, replanting each crop with
-- engine.player_set_block - the same checks as that player's own edits (reach, spawn
-- protection, can_edit_block) - and only a crop whose replant was allowed gives its harvest. So
-- a protected field can't be reaped for free, and the change and its reward happen together.
-- ---------------------------------------------------------------------------
local function reapable(name)
    local c = crops[name]
    if c and c.def.scythe_replants and farming.is_ripe(name) then return c end
    return nil
end

-- Reaps the 3x3 patch around (x, y, z). `player`: on the server, for a joined player (checked);
-- nil: the local game. Returns how many crops it reaped.
local function reap(x, y, z, player)
    local reaped, refusal = 0, nil
    local rewards = {}                                            -- delivered together, below
    for dx = -1, 1 do for dz = -1, 1 do
        local cx, cz = x + dx, z + dz
        local n = engine.get_block_name(cx, y, cz)
        local c = reapable(n)
        if c then
            local done = true
            if player then
                local why
                done, why = engine.player_set_block(player, cx, y, cz, c.def.stages[1])
                if not done and why and not refusal then refusal = why end
            else
                engine.set_block(cx, y, cz, c.def.stages[1])
            end
            if done then                                          -- replanted: now its harvest
                for _, d in ipairs(crop_drops(n, true)) do rewards[#rewards + 1] = { d[1], d[2], cx, y, cz } end
                reaped = reaped + 1
            end
        end
    end end
    deliver(rewards)                       -- whatever doesn't reach the world waits (retry_undelivered)
    if refusal and engine.message_player then engine.message_player(player, refusal, 1, 0.55, 0.55) end
    return reaped
end

local previous_scythe = on_use_scythe
function on_use_scythe(x, y, z, block_name, ...)
    if reapable(block_name) then
        if authority() then return reap(x, y, z) > 0 end
        -- joined to a server: it decides (and the crops and items come from it). This press
        -- doesn't dig either way.
        if engine.request_harvest then engine.request_harvest("scythe", x, y, z) end
        return true
    end
    if previous_scythe then return previous_scythe(x, y, z, block_name, ...) end
    return false
end

-- The server: a joined player's scythe (engine.request_harvest on their side)
local previous_harvest = on_player_harvest
function on_player_harvest(name, item, x, y, z, ...)
    if engine.player_set_block and type(item) == "string" and item:find("scythe", 1, true)
       and reapable(engine.get_block_name(x, y, z)) then
        reap(x, y, z, name)
        return
    end
    if previous_harvest then return previous_harvest(name, item, x, y, z, ...) end
end

-- ---------------------------------------------------------------------------
-- Trampling: landing on farmland after a fall (or a jump onto it) turns it back to dirt, and
-- the crop on it pops off (Minecraft). The game watches its own player.
-- ---------------------------------------------------------------------------
local falling_speed = 0
local previous_update = on_update
local retry_timer = 0
function on_update(dt, ...)
    -- harvest rewards the world couldn't take yet (full of entities...): try again every second
    retry_timer = retry_timer + (tonumber(dt) or 0)
    if retry_timer >= 1 then
        retry_timer = 0
        if authority() then retry_undelivered() end
    end
    if engine.get_player_state and engine.is_in_game and engine.is_in_game() then
        local st = engine.get_player_state()
        if st then
            if not st.on_ground then
                falling_speed = math.min(falling_speed, st.vy or 0)
            else
                -- about 1 block or more of fall (Minecraft: more than half a block), not creative
                if falling_speed < -6.5 and not in_creative() and not st.flying then
                    local x, y, z = math.floor(st.x), math.floor(st.y - 0.1), math.floor(st.z)
                    if is_farmland(engine.get_block_name(x, y, z)) then
                        local above = engine.get_block_name(x, y + 1, z)
                        engine.set_block(x, y, z, "dirt")
                        if crops[above] then pop_crop(x, y + 1, z, above) end
                    end
                end
                falling_speed = 0
            end
        end
    end
    if previous_update then return previous_update(dt, ...) end
end

-- ---------------------------------------------------------------------------
-- Food, recipes, the creative inventory
-- ---------------------------------------------------------------------------
if survival and survival.register_food then survival.register_food("bread", 5, 6) end
if survival and survival.register_craft then
    survival.register_craft({ output = "wooden_hoe", recipe = { { "planks_oak", "planks_oak" }, { "", "stick" }, { "", "stick" } } })
    survival.register_craft({ output = "stone_hoe", recipe = { { "stone", "stone" }, { "", "stick" }, { "", "stick" } } })
    survival.register_craft({ output = "scythe", recipe = { { "stone", "stone", "stone" }, { "", "", "stick" }, { "", "stick", "" } } })
    survival.register_craft({ output = "bread", recipe = { { "wheat", "wheat", "wheat" } } })
end
if creative and creative.register_tab then
    creative.register_tab({ name = "Farming", icon = "wooden_hoe",
        items = { "wooden_hoe", "stone_hoe", "scythe", "wheat_seeds", "wheat", "bread", "farmland_dry", "farmland_wet" } })
end

print("[Farming] Ready: hoes till, seeds plant, crops grow on random ticks, a scythe reaps and replants")
