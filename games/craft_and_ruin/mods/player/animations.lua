-- Player model and animations.
-- The model and its animations are Bedrock / Blockbench files in this mod's models/ folder; the
-- engine plays them (engine.load_animations, anim:apply) exactly as Blockbench shows them. This
-- file decides which ones play, and blends between them.
--
-- The model has two joints in every limb (upper/lower arm, upper/lower leg), so knees and elbows
-- bend: see models/player.animation.json for the animations themselves:
--   moving:   idle, idle_look, walk, sprint, sneak, sneak_walk, jump, fall, land,
--             swim, tread_water, fly, fly_move
--   actions:  attack (every swing: digging, hitting, placing), use, eat, hurt, death,
--             wave, sit (on a chair), sit_ground (on the floor), sleep (lying down), blink
-- Other mods can play any of them on top of the movement:
--   player_anim.play("wave")              once
--   player_anim.play("sit", true)         until player_anim.stop("sit")
--   player_anim.stop("sit"), player_anim.is_playing("wave")
-- Sitting and sleeping are poses: the whole body takes them (no walking legs underneath):
--   player_anim.sit()          on the floor        player_anim.sit("chair")   on a seat
--   player_anim.sleep()        lying on the back, eyes shut
--   player_anim.stand()        back up             player_anim.get_pose() -> name or nil
--   Started like this they end by themselves when the player moves or jumps; pass true as the
--   last argument (player_anim.sleep(true), player_anim.sit("chair", true)) to stay until
--   player_anim.stand() - for a bed or a chair mod that holds the player in place.
--   Commands: /sit [chair], /sleep (or /lay), /stand

-- The model (Blockbench: File > Export > Export Bedrock Geometry); the first of these that loads
local MODEL_FILES = { "models/player_model.json", "models/player.geo.json", "models/player.json" }
-- The model's skin. Blockbench model files don't say which picture they use, so name it here
-- (the first of these that exists is used; without one the model is plain white)
local SKIN_FILES = { "player_model.png", "player.png", "player_skin.png", "skin.png", "character.png", "jack.png" }
local ANIMATION_FILE = "models/player.animation.json"
local WALK_SPEED = 4.3      -- blocks per second the walk animation was made for (Minecraft walking)
local SPRINT_SPEED = 5.6
local FADE = 8              -- how fast one movement animation turns into the next (per second)

-- The engine draws g_PlayerModel in third person (F5)
local skin = nil
for _, name in ipairs(SKIN_FILES) do
    local found = engine.find_texture and engine.find_texture(name)
    if found then skin = name; print("[Player] Skin: " .. found); break end
end
if not skin then
    print("[Player] No skin picture found (" .. table.concat(SKIN_FILES, ", ") .. "): the model will be white. "
          .. "Put your Blockbench texture in this mod's textures folder with one of those names.")
end
g_PlayerModel = nil
for _, file in ipairs(MODEL_FILES) do
    local ok, m = pcall(engine.load_model, file, skin)
    if ok and m and (not m.is_loaded or m:is_loaded()) then g_PlayerModel = m; print("[Player] Model: " .. file); break end
end
if not g_PlayerModel then
    print("[Player] No player model loaded (tried " .. table.concat(MODEL_FILES, ", ")
          .. "): F5 shows no body. Export it from Blockbench as Bedrock Geometry into this mod's models folder.")
end

local set = engine.load_animations(ANIMATION_FILE)
local A = {}
if set then
    for _, name in ipairs({ "idle", "idle_look", "walk", "sprint", "sneak", "sneak_walk", "jump", "fall", "land",
                            "swim", "tread_water", "fly", "fly_move", "attack", "use", "eat", "hurt", "death",
                            "wave", "sit", "sit_ground", "sleep", "blink" }) do
        A[name] = set:get(name)
    end
end
local count = 0
for _ in pairs(A) do count = count + 1 end
print("[Player] " .. count .. " animations from " .. ANIMATION_FILE)

local head = g_PlayerModel and g_PlayerModel:get_bone("head")
local rnd = math.random

-- ---------------------------------------------------------------------------
-- The movement layer: one animation at a time, crossfading into the next
-- ---------------------------------------------------------------------------
local BASE = { "idle", "walk", "sprint", "sneak", "sneak_walk", "jump", "fall", "swim", "tread_water", "fly", "fly_move" }
local weight, clock = {}, {}
for _, n in ipairs(BASE) do weight[n] = 0; clock[n] = 0 end
weight.idle = 1
local airborne, was_on_ground, last_vy, idle_time = 0, true, 0, 0

local function pick_base(p, moving, speed)
    if not p then return moving and "walk" or "idle", 1 end
    local water = p.head_fluid ~= nil
    if p.flying then return (moving and "fly_move" or "fly"), 1 end
    if water then return (moving and "swim" or "tread_water"), 1 end
    if not p.on_ground and airborne > 0.08 and not (p.feet_fluid) then
        if (p.vy or 0) > 0.5 then return "jump", 1 end
        if airborne > 0.25 then return "fall", 1 end
    end
    if p.sneaking then return (moving and "sneak_walk" or "sneak"), math.max(0.6, speed / 1.3) end
    if moving and p.sprinting and speed > 4.6 then return "sprint", speed / SPRINT_SPEED end
    if moving then return "walk", math.max(0.5, math.min(1.6, speed / WALK_SPEED)) end
    return "idle", 1
end

-- ---------------------------------------------------------------------------
-- The action layer: one-shots and held poses on top of the movement
-- ---------------------------------------------------------------------------
local actions = {}     -- name -> { t = seconds in, loop = bool }
player_anim = player_anim or {}
function player_anim.play(name, loop)
    if not A[name] then return false end
    actions[name] = { t = 0, loop = loop and true or false }
    return true
end
function player_anim.stop(name) actions[name] = nil end
function player_anim.is_playing(name) return actions[name] ~= nil end
function player_anim.list() local l = {} for n in pairs(A) do l[#l + 1] = n end table.sort(l) return l end

-- Poses: one at a time, faded in, replacing the movement underneath
local POSES = { sit = true, sit_ground = true, sleep = true }
local pose = nil       -- { name, t, w (fade 0..1), stay }
local function start_pose(name, stay)
    if not A[name] then return false end
    for n in pairs(POSES) do actions[n] = nil end
    pose = { name = name, t = 0, w = (pose and pose.w) or 0, stay = stay and true or false }
    return true
end
function player_anim.sit(kind, stay)
    if type(kind) == "boolean" then kind, stay = nil, kind end
    return start_pose((kind == "chair" or kind == "seat") and "sit" or "sit_ground", stay)
end
function player_anim.sleep(stay) return start_pose("sleep", stay) end
function player_anim.stand() pose = nil end
function player_anim.get_pose() return pose and pose.name or nil end
-- player_anim.play("sit"/"sit_ground"/"sleep", ...) is the same as starting the pose to stay
local play = player_anim.play
function player_anim.play(name, loop)
    if POSES[name] then return start_pose(name, true) end
    return play(name, loop)
end
local stop = player_anim.stop
function player_anim.stop(name)
    if pose and pose.name == name then pose = nil end
    return stop(name)
end
local is_playing = player_anim.is_playing
function player_anim.is_playing(name) return (pose and pose.name == name) or is_playing(name) end

-- /sit [chair], /sleep (/lay), /stand
if chat and chat.register_command then
    chat.register_command("sit", "/sit [chair]", "Sit down (on the floor, or as on a chair); move to get up")
    chat.register_command("sleep", "/sleep", "Lie down with your eyes shut; move to get up")
    chat.register_command("stand", "/stand", "Get up again")
end
local previous_command = on_chat_command
function on_chat_command(text, player, is_operator, reply, ...)
    local cmd, arg = text:match("^(%S+)%s*(%S*)")
    if engine.is_server and engine.is_server() then cmd = nil end
    if cmd == "sit" then player_anim.sit(arg ~= "" and arg or nil); return true end
    if cmd == "sleep" or cmd == "lay" or cmd == "lie" then player_anim.sleep(); return true end
    if cmd == "stand" then player_anim.stand(); return true end
    if previous_command then return previous_command(text, player, is_operator, reply, ...) end
    return false
end

-- Eating (the survival mod) plays "eat" for a moment; getting hurt plays "hurt"
local hooked, last_hp = false, nil
local function hook_survival()
    hooked = true
    if survival and survival.feed then
        local feed = survival.feed
        survival.feed = function(...)
            player_anim.play("eat")
            actions.eat.until_t = 0.9
            return feed(...)
        end
    end
end

local blink_in = 2 + rnd() * 3
local look_cooldown = 8

-- Called every frame by the engine
function update_player_animation(dt, is_moving, camera_pitch)
    if not g_PlayerModel then return end
    if not hooked then hook_survival() end
    dt = math.min(dt or 0, 0.1)

    local p = engine.get_player_state and engine.get_player_state()
    local speed = 0
    if p and p.vx and p.vz then speed = math.sqrt(p.vx * p.vx + p.vz * p.vz) end
    local moving = (p and p.moving or is_moving) and speed > 0.3
    if p then
        if p.on_ground then
            -- just landed after a real fall: knees soak it up
            if not was_on_ground and last_vy < -9 then player_anim.play("land") end
            airborne = 0
        else
            airborne = airborne + dt
        end
        was_on_ground, last_vy = p.on_ground, p.vy or 0
    end
    local dead = survival and survival.is_dead and survival.is_dead()
    if survival and survival.get_hp then
        local hp = survival.get_hp()
        if last_hp and hp < last_hp and not dead then player_anim.play("hurt") end
        last_hp = hp
    end

    -- A pose ends when the player moves off (unless it's held), or dies
    if pose and not pose.stay then
        local jumped = p and (not p.on_ground) and (p.vy or 0) > 1
        if moving or jumped or (p and (p.flying or p.head_fluid)) then pose = nil end
    end
    if pose and dead then pose = nil end
    local sleeping = pose and pose.name == "sleep"

    -- Movement: fade towards the one that fits now (standing still under a pose)
    local want, rate = pick_base(p, moving, speed)
    if pose then want, rate = "idle", 1 end
    for _, n in ipairs(BASE) do
        local target = (n == want) and 1 or 0
        if weight[n] < target then weight[n] = math.min(target, weight[n] + dt * FADE)
        else weight[n] = math.max(target, weight[n] - dt * FADE) end
        if weight[n] > 0 then clock[n] = clock[n] + dt * ((n == want) and rate or 1) else clock[n] = 0 end
    end
    idle_time = (want == "idle") and (idle_time + dt) or 0

    g_PlayerModel:reset_pose()
    -- the strongest one last, so it wins where they disagree
    local order = {}
    for _, n in ipairs(BASE) do if weight[n] > 0 and A[n] then order[#order + 1] = n end end
    table.sort(order, function(a, b) return weight[a] < weight[b] end)
    for _, n in ipairs(order) do A[n]:apply(g_PlayerModel, clock[n], weight[n]) end

    -- Standing about for a while: now and then a look around
    if want == "idle" and idle_time > look_cooldown and not actions.idle_look then
        player_anim.play("idle_look")
        look_cooldown = idle_time + 10 + rnd() * 10
    end
    if want ~= "idle" then actions.idle_look = nil; look_cooldown = 8 end

    -- Dead: falls over and stays down
    if dead then
        if not actions.death then player_anim.play("death", false) end
    elseif actions.death then
        actions.death = nil
    end

    -- The swing (digging, hitting, placing - engine.get_swing goes 0..1 while it lasts)
    local swing = engine.get_swing and engine.get_swing() or -1
    if swing >= 0 and A.attack then A.attack:apply(g_PlayerModel, swing * A.attack:get_length(), 1) end

    -- The pose, fading in over a quarter of a second
    if pose then
        pose.t = pose.t + dt
        pose.w = math.min(1, pose.w + dt * 4)
        local anim = A[pose.name]
        anim:apply(g_PlayerModel, pose.t % anim:get_length(), pose.w)
    end

    -- Actions on top
    for name, a in pairs(actions) do
        local anim = A[name]
        a.t = a.t + dt
        local len = anim:get_length()
        local stop_at = a.until_t or len
        if not a.loop and name ~= "death" and a.t >= stop_at then
            actions[name] = nil
        else
            local t = a.t
            if a.loop or name == "eat" then t = t % len end
            anim:apply(g_PlayerModel, math.min(t, len), 1)
        end
    end

    -- Blinking every few seconds (not when dead: eyes stay shut)
    if dead then
        if A.blink then A.blink:apply(g_PlayerModel, 0.07, 1) end
    elseif sleeping then
        actions.blink = nil      -- (the sleep animation keeps them shut)
    else
        blink_in = blink_in - dt
        if blink_in <= 0 then player_anim.play("blink"); blink_in = 2 + rnd() * 4 end
    end

    -- The head follows the camera, up and down (Bedrock: +X looks down), on top of the animation
    if head and not dead and not sleeping then
        local rx, ry, rz = head:get_rotation()
        local pitch = math.max(-80, math.min(80, camera_pitch or 0))
        head:set_rotation(rx - pitch, ry, rz)
    end
end
