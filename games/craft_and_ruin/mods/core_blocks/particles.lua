-- Particles (part of Craft & Ruin's core blocks): Minecraft-style particle pictures (in
-- core_blocks/textures/), and the little effects blocks make by themselves.
--
-- The engine draws particles (ParticleSystem.h) and makes these on its own, with the pictures
-- particle_*.png from core_blocks/textures/:
--   * bits of a block when it breaks (also when other players break one), chips while digging
--   * dust kicked up when sprinting, and a puff when you land from a fall
--   * a splash when you drop into water, and bubbles while your head is under
--   * crit sparks when a mob is hit, and a white "poof" when one dies
-- This file adds the ambient ones (particles.register_ambient), Minecraft's "animate tick":
--   * torches: a flame flickering at the top, and smoke rising
--   * lava: now and then a glowing blob pops out and falls back, leaving a wisp of smoke
--   * leaves: one floats down every so often
--   * water: drips from the ceiling under it
-- ...and a few ready-made effects for other mods: particle_effects.hearts(x, y, z),
-- .happy(x, y, z), .smoke(x, y, z), .flame(x, y, z), .notes(x, y, z), .sparks(x, y, z).
-- Particles only exist in the game that draws them: nothing is sent to other players.

if not particles or not particles.spawn then
    print("[Particles] this engine has no particles.* API")
    return
end

particle_effects = particle_effects or {}
local SMOKE = {}
for i = 0, 7 do SMOKE[#SMOKE + 1] = "particle_smoke_" .. i .. ".png" end
local SMOKE_SHORT = { SMOKE[3], SMOKE[4], SMOKE[5], SMOKE[6], SMOKE[7], SMOKE[8] }   -- small puffs
local rnd = math.random

local function name_has(name, words)
    for _, w in ipairs(words) do if name:find(w, 1, true) then return true end end
    return false
end
local function air(x, y, z) return engine.get_block_name(x, y, z) == "air" end

-- ---------------------------------------------------------------------------
-- Ready-made effects (any mod may call these)
-- ---------------------------------------------------------------------------
function particle_effects.smoke(x, y, z, amount)
    particles.spawn{ pos = { x, y, z }, texture = SMOKE, amount = amount or 6, spread = { 0.2, 0.1, 0.2 },
                     velocity = { 0, 0.5, 0 }, velocity_spread = { 0.15, 0.1, 0.15 }, gravity = -0.4, drag = 1,
                     life = 1.4, size = 0.22, size_end = 0.12, color = { 0.4, 0.4, 0.4 }, collide = false }
end
function particle_effects.flame(x, y, z)
    particles.spawn{ pos = { x, y, z }, texture = "particle_flame.png", velocity = { 0, 0.05, 0 }, gravity = 0,
                     life = 0.5, size = 0.12, size_end = 0.03, glow = true, collide = false }
end
function particle_effects.hearts(x, y, z, amount)
    particles.spawn{ pos = { x, y, z }, texture = "particle_heart.png", amount = amount or 5, spread = { 0.4, 0.3, 0.4 },
                     velocity = { 0, 0.6, 0 }, gravity = 0, drag = 1, life = 1.2, size = 0.16, collide = false }
end
function particle_effects.happy(x, y, z, amount)
    particles.spawn{ pos = { x, y, z }, texture = "particle_happy.png", amount = amount or 8, spread = { 0.5, 0.5, 0.5 },
                     velocity_spread = { 0.3, 0.3, 0.3 }, gravity = 0, drag = 2, life = 1.0, size = 0.12, glow = true,
                     collide = false }
end
function particle_effects.notes(x, y, z, color)
    particles.spawn{ pos = { x, y, z }, texture = "particle_note.png", velocity = { 0, 0.8, 0 }, gravity = 0, drag = 2,
                     life = 1.0, size = 0.16, color = color or { rnd(), rnd(), rnd() }, glow = true, collide = false }
end
function particle_effects.sparks(x, y, z, amount)
    particles.spawn{ pos = { x, y, z }, texture = "particle_spark.png", amount = amount or 12, velocity = { 0, 1.5, 0 },
                     velocity_spread = { 2.5, 1.5, 2.5 }, gravity = 10, life = 0.6, size = 0.08, glow = true, bounce = true }
end

-- ---------------------------------------------------------------------------
-- What blocks do by themselves (looked at ~3 times a second each, near the player)
-- ---------------------------------------------------------------------------
local function torch_tick(x, y, z)
    -- Minecraft: every look, a puff of smoke and a flame at the tip
    local tx, ty, tz = x + 0.5, y + 0.72, z + 0.5
    particles.spawn{ pos = { tx, ty + 0.08, tz }, texture = SMOKE_SHORT, velocity = { 0, 0.35, 0 },
                     velocity_spread = { 0.04, 0.05, 0.04 }, gravity = -0.3, drag = 0.5, life = rnd(10, 18) / 10,
                     size = 0.18, size_end = 0.08, color = { 0.42, 0.42, 0.42 }, collide = false }
    particles.spawn{ pos = { tx, ty, tz }, texture = "particle_flame.png", velocity = { 0, 0.03, 0 }, gravity = 0,
                     life = 0.45 + rnd() * 0.3, size = 0.1, size_end = 0.02, glow = true, collide = false }
end

local function lava_tick(x, y, z)
    if not air(x, y + 1, z) or rnd() > 0.06 then return end
    -- a glowing blob jumps out and falls back (bouncing once), with a wisp of smoke
    particles.spawn{ pos = { x + rnd(), y + 1, z + rnd() }, texture = "particle_lava.png",
                     velocity = { (rnd() - 0.5) * 1.2, 3 + rnd() * 2, (rnd() - 0.5) * 1.2 }, gravity = 14,
                     life = 1.6, size = 0.11, size_end = 0.05, glow = true, bounce = true }
    if rnd() < 0.5 then
        particles.spawn{ pos = { x + rnd(), y + 1.05, z + rnd() }, texture = SMOKE_SHORT, velocity = { 0, 0.4, 0 },
                         gravity = -0.3, drag = 0.6, life = 1.5, size = 0.15, size_end = 0.08,
                         color = { 0.3, 0.3, 0.3 }, collide = false }
    end
end

local function leaves_tick(x, y, z, name)
    if rnd() > 0.04 or not air(x, y - 1, z) then return end
    -- a leaf drifts down, swaying, and lies on the ground a moment
    particles.spawn{ pos = { x + rnd(), y - 0.05, z + rnd() }, block = name, velocity = { 0, -0.4, 0 },
                     velocity_spread = { 0.5, 0.1, 0.5 }, gravity = 0.6, drag = 1.6, life = 4 + rnd() * 2,
                     size = 0.09 }
end

local function water_tick(x, y, z)
    -- water above a ceiling drips through it (Minecraft): water here, a solid block under, air below that
    if rnd() > 0.05 or air(x, y - 1, z) or engine.get_block_name(x, y - 1, z):find("water", 1, true) then return end
    if not air(x, y - 2, z) then return end
    particles.spawn{ pos = { x + 0.2 + rnd() * 0.6, y - 1.02, z + 0.2 + rnd() * 0.6 }, texture = "particle_drip.png",
                     velocity = { 0, -0.2, 0 }, gravity = 12, life = 3, size = 0.06 }
end

local function lit_furnace_tick(x, y, z)
    if not air(x, y + 1, z) or rnd() > 0.3 then return end
    particle_effects.smoke(x + 0.5, y + 1.05, z + 0.5, 1)
end

-- Every block is known only once all mods have loaded (core_blocks loads first): register then
local registered = false
local previous_update = on_update
function on_update(dt, ...)
    if not registered then
        registered = true
        local torches, lava, leaves, water, lit = {}, {}, {}, {}, {}
        for _, name in ipairs(engine.list_blocks and engine.list_blocks() or {}) do
            if name:find("torch", 1, true) then torches[#torches + 1] = name
            elseif name == "lava" or name == "flowing_lava" then lava[#lava + 1] = name
            elseif name:find("leaves", 1, true) then leaves[#leaves + 1] = name
            elseif name == "water" then water[#water + 1] = name
            elseif name_has(name, { "controller_lit", "furnace_lit", "furnace_active" }) then lit[#lit + 1] = name end
        end
        if #torches > 0 then particles.register_ambient{ label = "torches", nodenames = torches, action = torch_tick } end
        if #lava > 0 then particles.register_ambient{ label = "lava", nodenames = lava, action = lava_tick } end
        if #leaves > 0 then particles.register_ambient{ label = "leaves", nodenames = leaves, action = leaves_tick } end
        if #water > 0 then particles.register_ambient{ label = "dripping water", nodenames = water, action = water_tick } end
        if #lit > 0 then particles.register_ambient{ label = "smoking furnaces", nodenames = lit, action = lit_furnace_tick } end
        print(string.format("[Particles] ambient: %d torch, %d lava, %d leaf, %d water, %d furnace block(s)",
                            #torches, #lava, #leaves, #water, #lit))
    end
    if previous_update then return previous_update(dt, ...) end
end

print("[Particles] Ready (core_blocks)")
