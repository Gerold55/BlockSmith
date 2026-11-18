-- Leaf decay, like Minecraft: cut a tree down and its leaves fade away over the next few
-- seconds, one by one. A leaf stays while a log is within 6 steps of it through other
-- leaves; leaves a player places never decay.
--
--   leafdecay.settings.distance = 6        -- steps through leaves a log keeps leaves alive
--   leafdecay.settings.delay = { 1, 12 }   -- seconds before each leaf goes (random)
--   leafdecay.is_leaves = function(name) ... end   -- which blocks count (default: "leaves" in the name)
--   leafdecay.is_log    = function(name) ... end   -- which keep them alive (default: "log" in the name)
--   leafdecay.on_decay(function(x, y, z, leaves) end)   -- runs when a leaf decays
--
-- Drops, like Minecraft: breaking leaves by hand (instant) or leaves decaying can give a sapling
-- (5%), 1-2 sticks (2%) and from oak leaves an apple (0.5%) - only items the game has. Leaves
-- themselves only drop with shears (a tool with tool_type = "shears").
--   leafdecay.settings.drops = { { item = "sapling", chance = 0.05 }, ... }   -- for every leaves
--   leafdecay.register_drops("leaves_oak", { { item = "sapling_oak", chance = 0.05 },
--                                            { item = "stick", chance = 0.02, min = 1, max = 2 } })
--
-- Works in singleplayer, when hosting and on servers: only whoever runs the world decides,
-- and every player sees the leaves go.

leafdecay = leafdecay or {}
local settings = { distance = 6, delay = { 1, 12 }, checks_per_frame = 40,
                   drop_range = 32 }   -- decaying leaves give their drops to a player this close
leafdecay.settings = settings

local PERSISTENT = 128   -- metadata bit on leaves a player placed (bit 1 is "waterlogged")

function leafdecay.is_leaves(name) return name ~= nil and name:find("leaves", 1, true) ~= nil end
function leafdecay.is_log(name) return name ~= nil and name:find("log", 1, true) ~= nil end

-- ---------------------------------------------------------------------------
-- Drops
-- ---------------------------------------------------------------------------
local function exists(item) return item and engine.get_block_id and engine.get_block_id(item) ~= 0 end

-- The sapling for these leaves: "leaves_oak" -> "sapling_oak", "oak_leaves" -> "oak_sapling", else "sapling"
local function sapling_for(leaves)
    for _, guess in ipairs({ (leaves:gsub("leaves", "sapling")), "sapling" }) do
        if exists(guess) then return guess end
    end
    return nil
end

local custom_drops = {}
function leafdecay.register_drops(leaves, list) custom_drops[leaves] = list end

local function drops_for(leaves)
    if custom_drops[leaves] then return custom_drops[leaves] end
    if settings.drops then return settings.drops end
    local list = {
        { item = sapling_for(leaves), chance = 0.05 },
        { item = "stick", chance = 0.02, min = 1, max = 2 },
    }
    if leaves:find("oak", 1, true) and not leaves:find("dark", 1, true) then
        list[#list + 1] = { item = "apple", chance = 0.005 }
    end
    return list
end

-- Rolls the drops for one leaf: { { item, count }, ... }
function leafdecay.roll_drops(leaves)
    local out = {}
    for _, d in ipairs(drops_for(leaves)) do
        if exists(d.item) and math.random() < (d.chance or 0) then
            local lo, hi = d.min or 1, d.max or d.min or 1
            out[#out + 1] = { d.item, math.random(lo, hi) }
        end
    end
    return out
end

local function give(list)
    if not engine.give_item then return end
    for _, d in ipairs(list) do engine.give_item(d[1], d[2]) end
end

local callbacks = {}
function leafdecay.on_decay(fn) callbacks[#callbacks + 1] = fn end

local function authority() return not engine.is_authority or engine.is_authority() end
local function has_bit(v, bit) return math.floor(v / bit) % 2 == 1 end

-- Is there a log within `distance` steps of (x, y, z), going through leaves?
local DIRS = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }
local function supported(x, y, z)
    local seen = { [x .. "," .. y .. "," .. z] = true }
    local frontier = { { x, y, z } }
    for _ = 1, settings.distance do
        local nextf = {}
        for _, p in ipairs(frontier) do
            for _, d in ipairs(DIRS) do
                local nx, ny, nz = p[1] + d[1], p[2] + d[2], p[3] + d[3]
                local key = nx .. "," .. ny .. "," .. nz
                if not seen[key] then
                    seen[key] = true
                    local name = engine.get_block_name(nx, ny, nz)
                    if leafdecay.is_log(name) then return true end
                    if leafdecay.is_leaves(name) then nextf[#nextf + 1] = { nx, ny, nz } end
                end
            end
        end
        if #nextf == 0 then return false end
        frontier = nextf
    end
    return false
end

-- Leaves waiting to be checked: key -> { x, y, z, at = time }
local queue, clock = {}, 0
local function schedule(x, y, z)
    local key = x .. "," .. y .. "," .. z
    if queue[key] then return end
    local lo, hi = settings.delay[1], settings.delay[2]
    queue[key] = { x, y, z, at = clock + lo + math.random() * (hi - lo) }
end

-- Every leaf near (x, y, z) gets checked (after a log there went away)
local function schedule_around(x, y, z)
    local r = settings.distance
    for dx = -r, r do
        for dy = -r, r do
            for dz = -r, r do
                if math.abs(dx) + math.abs(dy) + math.abs(dz) <= r + 2 then
                    local name = engine.get_block_name(x + dx, y + dy, z + dz)
                    if leafdecay.is_leaves(name) then schedule(x + dx, y + dy, z + dz) end
                end
            end
        end
    end
end

-- A player cut a log: the leaves around it may have lost their tree
local previous_broken = on_block_broken
function on_block_broken(x, y, z, block, meta, ...)
    if authority() and leafdecay.is_log(block) then schedule_around(x, y, z) end
    if previous_broken then return previous_broken(x, y, z, block, meta, ...) end
end

-- A player breaks leaves (survival): no leaves, but a chance of a sapling, sticks or an apple.
-- With shears the leaves themselves drop, like Minecraft.
local previous_dig = on_dig_block
function on_dig_block(x, y, z, block, item, ...)
    if leafdecay.is_leaves(block) then
        local def = item and engine.get_block_def and engine.get_block_def(item)
        local shears = def and def.tool_type == "shears"
        if not shears then
            give(leafdecay.roll_drops(block))
            if previous_dig then previous_dig(x, y, z, block, item, ...) end
            return true   -- handled: the leaves don't drop themselves
        end
    end
    if previous_dig then return previous_dig(x, y, z, block, item, ...) end
    return false
end

-- A player placed leaves: those stay forever
local previous_placed = on_block_placed
function on_block_placed(x, y, z, block, ...)
    if authority() and leafdecay.is_leaves(block) then
        local meta = engine.get_metadata(x, y, z)
        if not has_bit(meta, PERSISTENT) then engine.set_block_meta(x, y, z, block, meta + PERSISTENT) end
    end
    if previous_placed then return previous_placed(x, y, z, block, ...) end
end

-- Check the leaves that are due, a few per frame
local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end
    clock = clock + dt
    if not authority() or next(queue) == nil then return end
    local done = 0
    local later = {}   -- added after the loop (a table mustn't grow while pairs() walks it)
    for key, q in pairs(queue) do
        if done >= settings.checks_per_frame then break end
        if q.at <= clock then
            queue[key] = nil
            done = done + 1
            local name = engine.get_block_name(q[1], q[2], q[3])
            if leafdecay.is_leaves(name) and not has_bit(engine.get_metadata(q[1], q[2], q[3]), PERSISTENT)
               and not supported(q[1], q[2], q[3]) then
                engine.set_block(q[1], q[2], q[3], "air")
                for _, fn in ipairs(callbacks) do pcall(fn, q[1], q[2], q[3], name) end
                -- Its drops go to the player nearby (there are no dropped items lying in the
                -- world yet), only in survival
                if engine.give_item and engine.get_player_position and not (engine.is_server and engine.is_server())
                   and not (engine.is_creative and engine.is_creative()) then
                    local px, py, pz = engine.get_player_position()
                    local dx, dy, dz = px - q[1], py - q[2], pz - q[3]
                    if dx * dx + dy * dy + dz * dz <= settings.drop_range * settings.drop_range then
                        give(leafdecay.roll_drops(name))
                    end
                end
                -- its neighbours may have hung on through this leaf
                for _, d in ipairs(DIRS) do
                    local nx, ny, nz = q[1] + d[1], q[2] + d[2], q[3] + d[3]
                    if leafdecay.is_leaves(engine.get_block_name(nx, ny, nz)) then later[#later + 1] = { nx, ny, nz } end
                end
            end
        end
    end
    for _, p in ipairs(later) do schedule(p[1], p[2], p[3]) end
end

print("[Leaf Decay] Ready")
