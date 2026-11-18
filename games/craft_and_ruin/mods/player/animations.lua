-- Player model and animations.
-- The model and its animations are Bedrock / Blockbench files in this mod's models/ folder.
-- The engine plays them (engine.load_animations, anim:apply); this file decides when.

local MODEL_FILE = "models/player_model.json"
-- The model's skin. Blockbench model files don't say which picture they use, so name it here
-- (the first of these that exists is used; without one the model is plain white)
local SKIN_FILES = { "player_model.png", "player.png", "player_skin.png", "skin.png", "character.png" }
local ANIMATION_FILE = "models/model.animation.walk.json"
local WALK_SPEED = 4.3      -- blocks per second the walk animation was made for (Minecraft walking)
local FADE = 6              -- how fast walking fades in and out (per second)

-- The engine draws g_PlayerModel in third person (F5)
local skin = nil
for _, name in ipairs(SKIN_FILES) do
    if engine.find_texture and engine.find_texture(name) then skin = name; break end
end
if not skin then
    print("[Player] No skin picture found (" .. table.concat(SKIN_FILES, ", ") .. "): the model will be white. "
          .. "Put your Blockbench texture in this mod's textures folder with one of those names.")
end
g_PlayerModel = engine.load_model(MODEL_FILE, skin)

local anims = engine.load_animations(ANIMATION_FILE)
-- The walk: by name if the file names it "...walk", else the file's first animation
local walk = anims and (anims:get("walk") or anims:get())
if walk then
    walk:set_loop(true)
    print("[Player] Walk animation: " .. walk:get_name() .. " (" .. walk:get_length() .. " s)")
end

local head = g_PlayerModel and g_PlayerModel:get_bone("head")

-- Bones the swing moves (the first name the model has is used)
local function find_bone(names)
    if not g_PlayerModel then return nil end
    for _, n in ipairs(names) do local b = g_PlayerModel:get_bone(n) if b then return b end end
    return nil
end
local right_arm = find_bone({ "upper_arm_right", "right_arm", "arm_right", "rightArm" })
local left_arm  = find_bone({ "upper_arm_left", "left_arm", "arm_left", "leftArm" })
local body      = find_bone({ "body", "torso", "chest" })

-- Minecraft's third-person attack swing (when you dig, place or use something): the main arm
-- sweeps forward and across while the body twists a little. Added on top of the walk.
local function apply_swing(camera_pitch)
    local p = engine.get_swing and engine.get_swing() or -1
    if p < 0 then return end
    local arm = (engine.is_left_handed and engine.is_left_handed()) and left_arm or right_arm
    if not arm then return end
    local side = (arm == left_arm) and -1 or 1
    local f = 1 - p
    f = 1 - f * f * f * f
    local lift = math.sin(f * math.pi) * 1.2
               + math.sin(p * math.pi) * (math.rad(camera_pitch) + 0.7) * 0.75   -- higher when looking up
    local twist = math.sin(math.sqrt(p) * math.pi * 2) * 0.2                     -- body turns into the swing
    local rx, ry, rz = arm:get_rotation()
    arm:set_rotation(rx - math.deg(lift), ry + side * math.deg(twist * 2), rz - side * math.deg(math.sin(p * math.pi) * 0.4))
    if body then
        local bx, by, bz = body:get_rotation()
        body:set_rotation(bx, by + side * math.deg(twist), bz)
    end
end

local walk_time = 0     -- where we are in the walk cycle
local walk_blend = 0    -- 0 standing .. 1 walking, so starting and stopping are smooth

-- Called every frame by the engine
function update_player_animation(dt, is_moving, camera_pitch)
    if not g_PlayerModel then return end

    -- Speed: the walk plays faster when sprinting, slower when sneaking
    local rate = 1
    local p = engine.get_player_state and engine.get_player_state()
    if p and p.vx and p.vz then
        local speed = math.sqrt(p.vx * p.vx + p.vz * p.vz)
        rate = math.max(0.5, math.min(2.0, speed / WALK_SPEED))
        if speed < 0.2 then is_moving = false end
    end

    local target = is_moving and 1 or 0
    if walk_blend < target then walk_blend = math.min(target, walk_blend + dt * FADE)
    else walk_blend = math.max(target, walk_blend - dt * FADE) end

    g_PlayerModel:reset_pose()
    if walk and walk_blend > 0 then
        walk_time = walk_time + dt * rate
        walk:apply(g_PlayerModel, walk_time, walk_blend)
    else
        walk_time = 0   -- start the next walk from the beginning of the cycle
    end

    apply_swing(camera_pitch)

    -- The head looks up and down with the camera
    if head then head:set_rotation(camera_pitch, 0, 0) end
end
