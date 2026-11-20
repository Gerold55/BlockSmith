-- Survival: health, hunger, air, damage, death and respawning - Minecraft's rules, all in
-- Lua so a game can change any number here or leave the mod out for a creative-only game.
--
-- The engine supplies the building blocks (engine.set_creative, get_player_state,
-- set_player_controls, respawn_player, get_storage, render_hud...); this mod decides.
--
-- For other mods:
--   survival.damage(amount, reason)        -- 1 = half a heart; reason shows on the death screen
--   survival.heal(amount)
--   survival.feed(food, saturation)
--   survival.get_hp(), get_food(), get_air(), is_dead()
--   survival.set_mode("survival" | "creative"), survival.get_mode()
--   survival.register_food(item, food, saturation)  -- right-click to eat (food: 2 = one drumstick)
--   survival.on_damage(function(amount, reason) return false to cancel end)
--   survival.on_death(function(reason) end)
--   survival.on_respawn(function() end)
--   survival.settings.*                    -- tune everything below

survival = survival or {}

local settings = {
    default_mode = "survival",   -- for new worlds
    max_hp = 20,                 -- 10 hearts
    max_food = 20,               -- 10 drumsticks
    max_air = 15,                -- seconds of air under water
    keep_inventory = true,       -- false: the hotbar empties when you die
    fall_safe = 3,               -- blocks you can fall without damage
    starve_to = 1,               -- starving stops at this many hp (0 = can starve to death)
    sprint_min_food = 7,         -- below this you can't sprint
    regen_min_food = 18,         -- regenerate health at or above this
    fluid_damage = { lava = 4 }, -- damage every half second while standing in these fluids
    drown_fluids = { water = true },
    max_armor = 20,              -- 10 chestplate icons; each armour point blocks 4% of damage

    -- HUD pictures (9x9 like Minecraft's). Point these at your game's own files. The
    -- defaults have names only this mod uses (survival_*), so a same-named file elsewhere
    -- in the game can't be picked up by mistake.
    --   *_empty = the dark outline drawn behind each icon
    --   *_half  = drawn for an odd last point; *_half_flip = true mirrors it (a half drawn
    --             as the right half shows as the left half, like Minecraft's half heart)
    -- Each can be one file name or a list to try in order: your game's own pictures first,
    -- then this mod's survival_* ones, so the HUD always shows something.
    icons = {
        heart_full = { "heart_full.png", "survival_heart_full.png" },
        heart_half = { "heart_half.png", "survival_heart_half.png" },
        heart_empty = { "heart_bg.png", "survival_heart_empty.png" },
        heart_half_flip = true,    -- the game's half heart is drawn as the right half
        hunger_full = { "hunger_full.png", "survival_hunger_full.png" },
        hunger_half = { "hunger_half.png", "survival_hunger_half.png" },
        hunger_empty = { "hunger_bg.png", "survival_hunger_empty.png" },
        hunger_half_flip = false,
        armor_full = { "armor_full.png", "survival_armor_full.png" },
        armor_half = { "armor_half.png", "survival_armor_half.png" },
        armor_empty = { "armor_empty.png", "survival_armor_empty.png" },
        armor_half_flip = false,
        air = { "air.png", "survival_air.png" },
    },

    -- The crack on a block being dug: a vertical strip of square pictures, first stage on
    -- top; the number of stages is worked out from the picture. The first file found is used
    -- (the game's crack_anylength.png, Luanti's name for it, then this mod's own).
    crack = { "crack_anylength.png", "destroy_stages.png" },
}
survival.settings = settings

-- ---------------------------------------------------------------------------
-- State (saved with the world)
-- ---------------------------------------------------------------------------
local S = {
    mode = nil, hp = 20, food = 20, saturation = 5, exhaustion = 0, air = 15, dead = false,
    death_reason = "", armor = 0,
}
local timers = { regen = 0, starve = 0, drown = 0, fluid = 0, invulnerable = 0, flash = 0, save = 0 }
local fall_top = nil        -- highest point of the current fall
local last = nil            -- player state last frame
local was_on_ground = true
local loaded = false
local foods = {}
local callbacks = { damage = {}, death = {}, respawn = {} }

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function save()
    engine.set_storage("survival:state", string.format("%s,%.2f,%.2f,%.2f,%.2f,%s",
        S.mode, S.hp, S.food, S.saturation, S.air, S.dead and "1" or "0"))
end

local function load()
    local s = engine.get_storage("survival:state")
    if s then
        local mode, hp, food, sat, air, dead = s:match("^(%a+),([%d%.%-]+),([%d%.%-]+),([%d%.%-]+),([%d%.%-]+),(%d)")
        if mode then
            S.mode, S.hp, S.food, S.saturation, S.air = mode, tonumber(hp), tonumber(food), tonumber(sat), tonumber(air)
            S.dead = dead == "1"
        end
    end
    -- A new world: the game mode picked on the Create World screen, else the default
    S.mode = S.mode or engine.get_storage("world:game_mode") or settings.default_mode
    if S.mode ~= "survival" and S.mode ~= "creative" then S.mode = settings.default_mode end
end

local function survival_on() return S.mode == "survival" end

-- Game rules (/gamerule on a server, or in singleplayer): shared values "gamerule.<name>",
-- "true"/"false". Not set: the default given here (this mod's own settings)
local function rule(name, default)
    local v = engine.get_shared and engine.get_shared("gamerule." .. name)
    if v == nil or v == "" then return default end
    return v == "true"
end
local function keeps_inventory() return rule("keepInventory", settings.keep_inventory) end

-- ---------------------------------------------------------------------------
-- API
-- ---------------------------------------------------------------------------
-- Armour points 0..max_armor (e.g. from an armour mod: chestplate 6, helmet 2...).
-- Each point blocks 4% of damage, like Minecraft (20 points = 80%).
function survival.set_armor(points) S.armor = clamp(points or 0, 0, settings.max_armor) end
function survival.get_armor() return S.armor end
function survival.get_hp() return S.hp end
function survival.get_food() return S.food end
function survival.get_air() return S.air end
function survival.is_dead() return S.dead end
function survival.get_mode() return S.mode end
function survival.on_damage(fn) callbacks.damage[#callbacks.damage + 1] = fn end
function survival.on_death(fn) callbacks.death[#callbacks.death + 1] = fn end
function survival.on_respawn(fn) callbacks.respawn[#callbacks.respawn + 1] = fn end

local function exhaust(amount) S.exhaustion = S.exhaustion + amount end

-- On a server, the server decides your game mode (only its operators can change it)
local function server_mode()
    return engine.get_server_game_mode and engine.get_server_game_mode() or nil
end

local function apply_mode(mode, online)
    S.mode = mode
    if not online then engine.set_storage("world:game_mode", mode) end   -- the world list shows it
    engine.set_creative(mode == "creative")
    if mode == "creative" and survival.inventory and survival.inventory.is_open() then
        survival.inventory.set_open(false)
        if engine.set_inventory_open then engine.set_inventory_open(false) end
    end
    if mode == "creative" then
        S.hp, S.air = settings.max_hp, settings.max_air
        if S.dead then survival.respawn() end
    end
    save()
end

-- Returns false (and changes nothing) on a server, unless it's the mode the server gave you
function survival.set_mode(mode)
    if mode ~= "survival" and mode ~= "creative" then return false end
    local server = server_mode()
    if server and server ~= mode then return false end
    apply_mode(mode, server ~= nil)
    return true
end

-- The server set our mode (when we join, or an operator changed it)
local previous_game_mode = on_game_mode
function on_game_mode(mode, is_operator)
    if loaded and (mode == "survival" or mode == "creative") then apply_mode(mode, true) end
    if previous_game_mode then previous_game_mode(mode, is_operator) end
end

function survival.heal(amount)
    if S.dead then return end
    S.hp = clamp(S.hp + amount, 0, settings.max_hp)
end

function survival.feed(food, saturation)
    S.food = clamp(S.food + (food or 0), 0, settings.max_food)
    S.saturation = clamp(S.saturation + (saturation or 0), 0, S.food)
end

local function die(reason)
    S.dead = true
    S.death_reason = reason or "died"
    engine.set_player_controls{ move = false, jump = false, sprint = false, sneak = false }
    if not keeps_inventory() then
        -- Everything you carried falls where you died, scattered (Minecraft), and can be picked
        -- up again for 5 minutes. (An older engine without drop_item: it's just lost.)
        if engine.drop_item then
            local px, py, pz = engine.get_player_position()
            local stacks = {}
            for i = 1, hotbar.get_slot_count() do
                local name, count = hotbar.get_slot(i)
                if name and name ~= "" and (count or 0) > 0 then stacks[#stacks + 1] = { name = name, count = count } end
            end
            if survival.inventory and survival.inventory.all_stacks then
                for _, s in ipairs(survival.inventory.all_stacks()) do stacks[#stacks + 1] = s end
            end
            for _, s in ipairs(stacks) do
                local a = math.random() * math.pi * 2
                local speed = math.random() * 2.5
                engine.drop_item(s.name, s.count, px, py + 1.2, pz, math.cos(a) * speed, 3 + math.random() * 2, math.sin(a) * speed, 0.5)
            end
        end
        for i = 1, hotbar.get_slot_count() do hotbar.set_slot(i, "", 0) end
    end
    for _, fn in ipairs(callbacks.death) do pcall(fn, S.death_reason) end
    save()
end

function survival.damage(amount, reason)
    if not survival_on() or S.dead or amount <= 0 then return false end
    if timers.invulnerable > 0 then return false end
    for _, fn in ipairs(callbacks.damage) do
        local ok, result = pcall(fn, amount, reason)
        if ok and result == false then return false end
    end
    -- Armour soaks part of it (not starving, drowning or falling, as in Minecraft)
    if S.armor > 0 and reason ~= "starved" and reason ~= "drowned" and reason ~= "fell from a high place" then
        amount = amount * (1 - S.armor * 0.04)
    end
    S.hp = math.max(0, S.hp - amount)
    timers.invulnerable = 0.5            -- Minecraft's half-second after a hit
    timers.flash = 0.35
    exhaust(0.1)
    if S.hp <= 0 then die(reason) end
    return true
end

-- Where you come back: the world's spawn (remembered the first time you stand there)
local function spawn_point()
    local s = engine.get_storage("survival:spawn")
    if s then
        local x, y, z = s:match("([%d%.%-]+),([%d%.%-]+),([%d%.%-]+)")
        if x then return tonumber(x), tonumber(y), tonumber(z) end
    end
    return nil
end

-- Dies at once, whatever the armour or health (/kill). false in creative mode (nothing to kill).
function survival.kill(reason)
    if not survival_on() or S.dead then return false end
    S.hp = 0
    die(reason or "was killed")
    return true
end

function survival.respawn()
    S.dead = false
    S.hp, S.food, S.saturation, S.air, S.exhaustion = settings.max_hp, settings.max_food, 5, settings.max_air, 0
    fall_top = nil
    engine.set_player_controls{ move = true, jump = true, sprint = true, sneak = true }
    local x, y, z = spawn_point()
    engine.respawn_player(x, y, z)       -- on a server, the server picks its spawn
    for _, fn in ipairs(callbacks.respawn) do pcall(fn) end
    save()
end

-- survival.is_food(item) -> true if it can be eaten (the creative inventory's Food tab uses it)
function survival.is_food(item) return foods[item] ~= nil end

function survival.register_food(item, food, saturation)
    foods[item] = { food = food, saturation = saturation or food * 0.6 }
    engine.register_item_use(item, function(pointed)
        if not survival_on() or S.dead then return false end
        if S.food >= settings.max_food then return true end      -- full: can't eat (like Minecraft)
        survival.feed(food, saturation or food * 0.6)
        if pointed.count > 1 then hotbar.set_slot(pointed.slot, item, pointed.count - 1)
        else hotbar.set_slot(pointed.slot, "", 0) end
        return true
    end)
end

-- Pictures are found now, while this mod loads: the HUD is drawn every frame, long after
-- loading, when the engine no longer knows which mod's folder to look in. A picture that
-- isn't in this mod's folder keeps its plain name (the engine then finds it in the game's
-- textures folder).
local MOD_PATH = g_CurrentModPath or "."
local function resolve(name)
    if type(name) ~= "string" then return name end
    -- the engine looks in this mod's folder (while it loads), the game's textures and every mod
    if engine.find_texture then
        local found = engine.find_texture(name)
        if found then
            print("[Survival] " .. name .. " -> " .. found)
            return found
        end
        print("[Survival] Can't find the picture " .. name .. " - put it in " .. MOD_PATH .. "/textures/")
        return name
    end
    if io and io.open then
        for _, path in ipairs({ MOD_PATH .. "/textures/" .. name, MOD_PATH .. "/" .. name }) do
            local f = io.open(path, "rb")
            if f then f:close(); return path end
        end
    end
    return name
end
survival.resolve_texture = resolve
-- A list of names: the first one that exists
local function resolve_first(list)
    if engine.find_texture then
        for _, name in ipairs(list) do
            local found = engine.find_texture(name)
            if found then print("[Survival] " .. name .. " -> " .. found) return found end
        end
        print("[Survival] Can't find any of: " .. table.concat(list, ", ") .. " - put one in " .. MOD_PATH .. "/textures/")
    end
    return resolve(list[1])
end
for k, v in pairs(settings.icons) do
    if type(v) == "string" then settings.icons[k] = resolve(v)
    elseif type(v) == "table" then settings.icons[k] = resolve_first(v) end
end

-- The crack picture for survival digging: the game's crack_anylength.png if it has one
if settings.crack and engine.set_dig_crack_texture then
    for _, file in ipairs(settings.crack) do
        local path = engine.find_texture and engine.find_texture(file)
        if path and engine.set_dig_crack_texture(path) then break end
    end
end

-- Minecraft's food values (only work if the game has these items)
for item, v in pairs({
    apple = { 4, 2.4 }, bread = { 5, 6 }, carrot = { 3, 3.6 }, potato = { 1, 0.6 },
    baked_potato = { 5, 6 }, beetroot = { 1, 1.2 }, melon_slice = { 2, 1.2 }, cookie = { 2, 0.4 },
    cooked_beef = { 8, 12.8 }, cooked_porkchop = { 8, 12.8 }, cooked_chicken = { 6, 7.2 },
    cooked_mutton = { 6, 9.6 }, cooked_fish = { 5, 6 }, mushroom_stew = { 6, 7.2 },
    raw_beef = { 3, 1.8 }, raw_porkchop = { 3, 1.8 }, raw_chicken = { 2, 1.2 },
}) do survival.register_food(item, v[1], v[2]) end

-- ---------------------------------------------------------------------------
-- Every frame
-- ---------------------------------------------------------------------------
-- A marker in world storage, to notice when another world has opened: opening a world replaces
-- the whole storage, so the marker read back no longer matches. It must be NEW for every load:
-- with one marker for the whole game run, each world saved the same one, so going back to a
-- world played earlier looked like "nothing changed" - the previous world's inventory and stats
-- stayed (and were saved into it) instead of that world's own being loaded.
local session_count = 0
local session = nil

local function first_load()
    session_count = session_count + 1
    session = string.format("%d-%d-%s", math.random(1, 1e9), session_count, tostring(os and os.time and os.time() or ""))
    S.mode, S.dead = nil, false
    S.hp, S.food, S.saturation, S.air, S.exhaustion = settings.max_hp, settings.max_food, 5, settings.max_air, 0
    load()
    loaded = true
    engine.set_storage("survival:session", session)
    if survival.inventory then survival.inventory.load() end
    fall_top, last, was_on_ground = nil, nil, true
    S.mode = server_mode() or S.mode   -- on a server, its mode wins
    engine.set_creative(S.mode == "creative")
    engine.set_player_controls{ move = not S.dead, jump = not S.dead, sprint = not S.dead, sneak = not S.dead }
end

local function tick(dt)
    local p = engine.get_player_state()
    if not p then return end
    if not loaded or engine.get_storage("survival:session") ~= session then first_load() end

    -- remember the spawn point the first time we're standing somewhere
    if not engine.get_storage("survival:spawn") and p.on_ground then
        engine.set_storage("survival:spawn", string.format("%.2f,%.2f,%.2f", p.x, p.y + 0.5, p.z))
    end

    timers.invulnerable = math.max(0, timers.invulnerable - dt)
    timers.flash = math.max(0, timers.flash - dt)

    timers.save = timers.save + dt
    if timers.save > 2 then timers.save = 0; save() end

    if not survival_on() or S.dead then
        last = p
        return
    end

    -- Exhaustion from moving (Minecraft: sprinting 0.1 per block, jumping 0.05, sprint-jump 0.2)
    if last then
        local dx, dz = p.x - last.x, p.z - last.z
        local dist = math.sqrt(dx * dx + dz * dz)
        if dist < 2 then                                       -- not a teleport
            if p.sprinting then exhaust(0.1 * dist)
            elseif p.feet_fluid then exhaust(0.01 * dist) end
        end
        if was_on_ground and not p.on_ground and p.vy > 0.5 then
            exhaust(p.sprinting and 0.2 or 0.05)
        end
    end
    while S.exhaustion >= 4 do
        S.exhaustion = S.exhaustion - 4
        if S.saturation > 0 then S.saturation = math.max(0, S.saturation - 1)
        else S.food = math.max(0, S.food - 1) end
    end

    -- Regeneration and starvation (every 4 s; fast regen when full with saturation)
    if S.food >= settings.regen_min_food and S.hp < settings.max_hp and rule("naturalRegeneration", true) then
        local every = (S.food >= settings.max_food and S.saturation > 0) and 0.5 or 4
        timers.regen = timers.regen + dt
        if timers.regen >= every then timers.regen = 0; survival.heal(1); exhaust(6) end
    else
        timers.regen = 0
    end
    if S.food <= 0 then
        timers.starve = timers.starve + dt
        if timers.starve >= 4 then
            timers.starve = 0
            if S.hp > settings.starve_to then survival.damage(1, "starved") end
        end
    else
        timers.starve = 0
    end
    engine.set_player_controls{ sprint = S.food >= settings.sprint_min_food }

    -- Fall damage: blocks fallen beyond fall_safe, landing anywhere but in a fluid
    if p.feet_fluid then
        fall_top = nil
    elseif not p.on_ground then
        if not fall_top or p.y > fall_top then fall_top = p.y end
    else
        if not was_on_ground and fall_top then
            local fallen = fall_top - p.y
            local dmg = math.floor(fallen - settings.fall_safe)
            if dmg > 0 and rule("fallDamage", true) then survival.damage(dmg, "fell from a high place") end
        end
        fall_top = nil
    end

    -- Air and drowning
    if p.head_fluid and settings.drown_fluids[p.head_fluid] then
        S.air = math.max(0, S.air - dt)
        if S.air <= 0 then
            timers.drown = timers.drown + dt
            if timers.drown >= 1 then
                timers.drown = 0
                if rule("drowningDamage", true) then survival.damage(2, "drowned") end
            end
        end
    else
        S.air = math.min(settings.max_air, S.air + dt * 5)   -- comes back quickly
        timers.drown = 0
    end

    -- Hurting fluids (lava)
    local hurt = p.feet_fluid and settings.fluid_damage[p.feet_fluid]
              or p.head_fluid and settings.fluid_damage[p.head_fluid]
    if hurt then
        timers.fluid = timers.fluid + dt
        if timers.fluid >= 0.5 then
            timers.fluid = 0
            if rule("fireDamage", true) then survival.damage(hurt, "tried to swim in " .. (p.feet_fluid or p.head_fluid)) end
        end
    else
        timers.fluid = 0.5        -- the first touch hurts straight away
    end

    was_on_ground = p.on_ground
    last = p
end

-- ---------------------------------------------------------------------------
-- HUD: hearts left, hunger right, air above hunger - just above the hotbar
-- ---------------------------------------------------------------------------
-- One row of icons. `kind` = "heart", "hunger" or "armor" (pictures from settings.icons)
local function draw_row(kind, value, max, x, y, size, step, right_to_left, shake)
    local ic = settings.icons
    local full, half, empty = ic[kind .. "_full"], ic[kind .. "_half"], ic[kind .. "_empty"]
    local flip = ic[kind .. "_half_flip"]
    local count = math.ceil(max / 2)
    for i = 1, count do
        local px = right_to_left and (x - i * step) or (x + (i - 1) * step)
        local py = y
        if shake then py = py + (math.random() - 0.5) * size * 0.25 end
        if empty then ui.draw_image(empty, px, py, size, size) end
        local left = value - (i - 1) * 2
        if left >= 2 and full then
            ui.draw_image(full, px, py, size, size)
        elseif left >= 1 and half then
            if flip then ui.draw_image(half, px, py, size, size, 1, 0, 0, 1)   -- mirrored
            else ui.draw_image(half, px, py, size, size) end
        end
    end
end

local hud_note = nil
local previous_hud = render_hud           -- HUD pieces from mods loaded earlier (chained)
local draw_survival_hud
function render_hud(W, H)
    if previous_hud then pcall(previous_hud, W, H) end
    return draw_survival_hud(W, H)
end
draw_survival_hud = function(W, H)
    if not loaded or not survival_on() then
        local why = not loaded and "survival hasn't started yet (no player)" or ("the game mode is " .. tostring(S.mode))
        if hud_note ~= why then hud_note = why; print("[Survival] HUD hidden: " .. why) end
        return
    end
    hud_note = nil
    local bx, by, bw = hotbar.get_rect(W, H)
    local size = math.floor(math.max(12, bw / 20))
    local step = size * 0.92
    local y = by - size - size * 0.35

    if not S.dead then
        draw_row("heart", math.ceil(S.hp), settings.max_hp, bx, y, size, step, false, S.hp <= 4)
        draw_row("hunger", math.ceil(S.food), settings.max_food, bx + bw, y, size, step, true,
                 S.saturation <= 0 and S.food <= 6 and math.random() < 0.3)
        if S.armor > 0 then   -- above the hearts, like Minecraft
            draw_row("armor", math.ceil(S.armor), settings.max_armor, bx, y - size * 1.1, size, step, false, false)
        end
        if S.air < settings.max_air and settings.icons.air then
            local bubbles = math.ceil(S.air / settings.max_air * 10)
            for i = 1, bubbles do ui.draw_image(settings.icons.air, bx + bw - i * step, y - size * 1.1, size, size) end
        end
        if timers.flash > 0 then ui.draw_rect(0, 0, W, H, 0.8, 0.0, 0.0, timers.flash * 0.6) end
    else
        -- Death screen
        ui.draw_rect(0, 0, W, H, 0.45, 0.0, 0.0, 0.55)
        local big, small = H / 540 * 1.6, H / 540 * 0.8
        local title = "You died!"
        ui.draw_text(title, (W - ui.text_width(title, big)) / 2, H * 0.32, big, 1, 1, 1)
        local reason = "You " .. S.death_reason
        ui.draw_text(reason, (W - ui.text_width(reason, small)) / 2, H * 0.45, small, 0.9, 0.85, 0.85)
        local hint = "Click to respawn"
        local pulse = 0.7 + 0.3 * math.sin(ui.get_time() * 4)
        ui.draw_text(hint, (W - ui.text_width(hint, small)) / 2, H * 0.58, small, pulse, pulse, 0.6)
    end
end

-- ---------------------------------------------------------------------------
-- Hooks (each keeps any other mod's version working)
-- ---------------------------------------------------------------------------
local previous_update = on_update
function on_update(dt)
    -- Another mod's on_update failing must not stop survival (the HUD would never appear)
    if previous_update then
        local ok, err = pcall(previous_update, dt)
        if not ok then print("[Survival] another mod's on_update failed: " .. tostring(err)) end
    end
    if engine.is_server and engine.is_server() then return end   -- each player's own game runs this
    tick(dt)
end

local previous_click = on_player_click
function on_player_click(button)
    if S.dead then survival.respawn(); return true end
    if previous_click then return previous_click(button) end
    return false
end

-- Digging costs a little hunger, like Minecraft
local previous_dig = on_dig_block
function on_dig_block(x, y, z, block, item)
    exhaust(0.005)
    if previous_dig then return previous_dig(x, y, z, block, item) end
    return false
end

-- /gamemode survival|creative  (/gm s, /gm c)
chat.register_command("gamemode", "/gamemode survival|creative", "Switch between survival and creative")
local previous_command = on_chat_command
function on_chat_command(text, player, is_operator, reply)
    local cmd, arg = text:match("^(%S+)%s*(%S*)")
    if cmd ~= "gamemode" and cmd ~= "gm" then
        if previous_command then return previous_command(text, player, is_operator, reply) end
        return false
    end
    if engine.is_server and engine.is_server() then return false end
    -- On a server: the server handles /gamemode (only its operators may change modes)
    if server_mode() then return false end
    local say = reply or function(t) chat.send(t, 0.75, 0.85, 1) end
    local modes = { s = "survival", survival = "survival", ["0"] = "survival",
                    c = "creative", creative = "creative", ["1"] = "creative" }
    local mode = modes[arg]
    if not mode then say("Game mode: " .. tostring(S.mode) .. ". Use /gamemode survival|creative"); return true end
    survival.set_mode(mode)
    say("Game mode set to " .. mode)
    return true
end

-- The survival inventory (inventory.lua). If inventory.lua is really a copy of this file, it
-- would load itself forever: stop with a clear message instead.
if survival.__loading_inventory then
    error("survival/inventory.lua is a copy of survival/init.lua - put the survival inventory file back "
          .. "(it starts with '-- Survival inventory, drawn on the game's inventory picture')", 0)
end
survival.__loading_inventory = true
dofile(MOD_PATH .. "/inventory.lua")
survival.__loading_inventory = nil

local function close_inventory()
    if survival.inventory and survival.inventory.is_open() then
        survival.inventory.set_open(false)
        if engine.set_inventory_open then engine.set_inventory_open(false) end
    end
end
survival.on_death(function()
    close_inventory()
    if not keeps_inventory() and survival.inventory then survival.inventory.clear() end
end)

-- No picking up items while dead (they stay on the ground for when you're back)
local previous_can_pick_up = can_pick_up_item
function can_pick_up_item(item, count, ...)
    if S.dead then return false end
    if previous_can_pick_up then return previous_can_pick_up(item, count, ...) end
    return true
end

-- The server's /kill and /clear (GameServer::sendAction): a death with the death screen, and the
-- whole inventory (not just the hotbar) cleared
local previous_server_action = on_server_action
function on_server_action(action, a, b, ...)
    if action == "kill" then
        if survival.kill(a ~= "" and a or nil) then return true end
    elseif action == "damage" then          -- a mob hit us (entities.damage_player)
        survival.damage(tonumber(a) or 1, b ~= "" and b or "was hurt")
        return true
    elseif action == "clear" then
        for i = 1, hotbar.get_slot_count() do hotbar.set_slot(i, "", 0) end
        if survival.inventory then survival.inventory.clear() end
        return true
    end
    if previous_server_action then return previous_server_action(action, a, b, ...) end
    return false
end

print("[Survival] Ready")
