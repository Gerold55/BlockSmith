-- mobs: cows and zombies, made with the entities API (docs/MODDING.md, "Mobs and entities").
-- Copy it to make your own: register a kind of mob with its model and what it does each step,
-- and say where it spawns. The engine does the rest (walking, gravity, saving, other players
-- seeing it, being hit).
--
-- Who runs what: your game in singleplayer and when you host, the server on a server. Their
-- on_step / on_punch / on_death only run there; every game loads the models to draw them.

local function is_day()
    local t = sky.get_time and sky.get_time() or 0.75
    return t >= 0.5 and t < 0.98           -- 0.5 sunrise, 0.75 midday, ~1.0 sunset
end

-- ---------------------------------------------------------------------------
-- What they drop
-- ---------------------------------------------------------------------------
for _, item in ipairs({ "raw_beef", "leather", "rotten_flesh" }) do
    engine.register_block(item, 1, 1, 1, "mobs_" .. item .. ".png", nil, { inventory_image = "mobs_" .. item .. ".png" })
end
if survival and survival.register_food then
    survival.register_food("raw_beef", 3, 1.8)
    survival.register_food("rotten_flesh", 4, 0.8)
end

-- ---------------------------------------------------------------------------
-- Shared wandering: stand around for a bit, then walk to somewhere it can reach. go_to /
-- wander find a way round trees and walls and only climb steps it fits up (the engine's
-- pathfinding), so nothing walks into a tree or jumps at a gap it can't get through.
-- ---------------------------------------------------------------------------
local function wander(self, dt, speed, walk_anim, idle_anim)
    self.timer = (self.timer or 0) - dt
    local moving = self:path_state() == "following"
    -- In water: head for the nearest land straight away (wander does that when swimming)
    if self:is_in_water() and not moving then self.timer = 0 end
    if self.timer <= 0 and not moving then
        if (self:is_in_water() or math.random() < 0.5) and self:wander(10, speed) then
            self.timer = 8                       -- (a walk ends when it arrives)
        else
            self:stop()
            self.timer = math.random(3, 7)
        end
        moving = self:path_state() == "following"
    end
    self:set_animation(moving and walk_anim or idle_anim)
end

-- ---------------------------------------------------------------------------
-- Cow: wanders, runs away when hit, drops beef and leather
-- ---------------------------------------------------------------------------
entities.register("mobs:cow", {
    hitbox = { 0.9, 1.4 },           -- width, height (blocks)
    health = 10,
    model = "mobs_cow.geo.json", texture = "mobs_cow.png", animations = "mobs_cow.animation.json",
    drops = { { "raw_beef", 1, 3 }, { "leather", 0, 2 } },

    on_spawn = function(self, saved)
        self.timer = math.random(1, 4)
        if saved == "named" then self.named = true self:set_nametag("Daisy") end
    end,

    on_step = function(self, dt)
        if self.panic and self.panic > 0 then      -- just hit: run away (round things, not into them)
            self.panic = self.panic - dt
            self.turn = (self.turn or 0) - dt
            if self.turn <= 0 or self:path_state() ~= "following" then
                self.turn = 1.0
                local x, y, z = self:get_pos()
                local away = self.flee_x and math.atan(z - self.flee_z, x - self.flee_x) or math.rad(self:get_yaw())
                away = away + math.rad(math.random(-50, 50))
                if not self:go_to(x + math.cos(away) * 8, y, z + math.sin(away) * 8, 3.2) then self:wander(6, 3.2) end
            end
            self:set_animation("run")
            if self.panic <= 0 then self:stop() end
            return
        end
        wander(self, dt, 1.1, "walk", "idle")
    end,

    on_punch = function(self, player, item, damage)
        self.panic = 4
        for _, p in ipairs(entities.get_players()) do
            if p.name == player then self.flee_x, self.flee_z = p.x, p.z end
        end
        -- (returning nothing: the engine still takes the damage and knocks it back)
    end,

    on_right_click = function(self, player, item)
        if item == "name_tag" or item == "paper" then self.named = true self:set_nametag("Daisy") end
    end,

    on_save = function(self)                         -- kept with the world; back in on_spawn
        return self.named and "named" or nil
    end,
})

-- ---------------------------------------------------------------------------
-- Zombie: chases the nearest player in reach, hits them, burns in daylight
-- ---------------------------------------------------------------------------
entities.register("mobs:zombie", {
    hitbox = { 0.6, 1.9 },
    health = 20,
    despawn = true,                  -- comes and goes, like Minecraft's monsters (never saved)
    model = "mobs_zombie.geo.json", texture = "mobs_zombie.png", animations = "mobs_zombie.animation.json",
    drops = { { "rotten_flesh", 0, 2 } },

    on_spawn = function(self) self.cooldown = 0 self.burn = 0 end,

    on_step = function(self, dt)
        self.cooldown = math.max(0, (self.cooldown or 0) - dt)
        local x, y, z = self:get_pos()

        -- Burns in daylight under the open sky (not in water)
        if is_day() and engine.is_outdoors and engine.is_outdoors(math.floor(x), math.floor(y + 1.7), math.floor(z))
           and not self:is_in_water() then
            self.burn = (self.burn or 0) + dt
            if self.burn >= 1 then self.burn = 0 self:damage(1) end
            if not self:is_alive() then return end
        end

        -- The nearest player it can SEE (a ray from its eyes through the blocks), or the place it
        -- last saw one, for a few seconds. It finds its way there round trees and walls.
        local name, px, py, pz, dist = entities.nearest_player(x, y, z, 24)
        if name and not self:can_see(px, py + 1.6, pz) then name = nil end
        if name then
            self.seen_x, self.seen_y, self.seen_z, self.seen_time = px, py, pz, 6
        elseif self.seen_time and self.seen_time > 0 then
            self.seen_time = self.seen_time - dt
        end
        if name and dist <= 1.8 and math.abs(py - y) < 2 then
            self:stop()
            self:look_at(px, pz)
            if self.cooldown <= 0 then
                entities.damage_player(name, 3, "was slain by a zombie")
                self.cooldown = 1.0
                self:set_animation("attack", 1, false)
            end
        elseif self.seen_time and self.seen_time > 0 then
            self:go_to(self.seen_x, self.seen_y, self.seen_z, 2.4)
            if self:path_state() ~= "following" then self.seen_time = 0 end   -- got there (or can't): give up
            if self.cooldown <= 0.6 then self:set_animation("walk") end
        else
            wander(self, dt, 0.8, "walk", "idle")
        end
    end,
})

-- ---------------------------------------------------------------------------
-- Where they spawn by themselves (the game rule doMobSpawning turns it off)
-- ---------------------------------------------------------------------------
entities.register_spawn({ name = "mobs:cow", on = { "grass" }, min_light = 9, sky = true,
                          chance = 0.25, group = { 2, 4 }, max_nearby = 6 })
entities.register_spawn({ name = "mobs:zombie", max_light = 7, chance = 0.6, group = { 1, 2 }, max_nearby = 5 })

-- ---------------------------------------------------------------------------
-- Spawn eggs (right-click a block) and /summon <mob> for operators
-- ---------------------------------------------------------------------------
local MOBS = { cow = "mobs:cow", zombie = "mobs:zombie" }
for short, name in pairs(MOBS) do
    local egg = short .. "_spawn_egg"
    engine.register_block(egg, 1, 1, 1, "mobs_" .. short .. "_egg.png", nil, { inventory_image = "mobs_" .. short .. "_egg.png" })
    engine.register_item_use(egg, function(pointed)
        if not pointed.above then return false end
        if engine.is_authority and not engine.is_authority() then
            chat.send("On a server, operators can use /summon " .. short, 1, 0.8, 0.5)
            return true
        end
        entities.spawn(name, pointed.above.x + 0.5, pointed.above.y, pointed.above.z + 0.5)
        return true
    end)
end
if creative and creative.register_tab then
    creative.register_tab({ name = "Mobs", icon = "cow_spawn_egg",
        items = { "cow_spawn_egg", "zombie_spawn_egg", "raw_beef", "leather", "rotten_flesh" } })
end

if chat and chat.register_command then
    chat.register_command("summon", "/summon cow|zombie [count]", "Spawn mobs next to you (operators)")
end
local previous_command = on_chat_command
function on_chat_command(text, player, is_operator, reply, ...)
    local cmd, what, count = text:match("^(%S+)%s*(%S*)%s*(%S*)")
    if cmd ~= "summon" then
        if previous_command then return previous_command(text, player, is_operator, reply, ...) end
        return false
    end
    if engine.is_authority and not engine.is_authority() then return false end   -- the server does it
    reply = reply or function(t) chat.send(t) end
    if is_operator == false then reply("Only operators can summon mobs") return true end
    local name = MOBS[what] or (what:find(":") and what) or nil
    if not name then reply("Usage: /summon cow|zombie [count]") return true end
    local x, y, z
    for _, p in ipairs(entities.get_players()) do
        if p.name == player or not player then x, y, z = p.x, p.y, p.z end
    end
    if not x then x, y, z = engine.get_player_position() end
    local n = math.max(1, math.min(tonumber(count) or 1, 20))
    local made = 0
    for i = 1, n do
        local a = math.random() * math.pi * 2
        if entities.spawn(name, x + math.cos(a) * 2.5, y + 0.5, z + math.sin(a) * 2.5) then made = made + 1 end
    end
    reply("Summoned " .. made .. " " .. what)
    return true
end

local previous_complete = on_chat_complete
function on_chat_complete(words, word, ...)
    local out = previous_complete and previous_complete(words, word, ...) or {}
    if words[1] == "summon" and #words == 1 then out[#out + 1] = "cow" out[#out + 1] = "zombie" end
    return out
end
