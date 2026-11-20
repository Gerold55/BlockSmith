-- Game Showcase: the whole game in one place, built in front of you.
--
--   /showcase           builds the Showcase Park around you (singleplayer, or when you host)
--   /showcase go        takes you back to it
--   /showcase panel     the control panel (also F8, or right-click the Guide block)
--
-- The park, seen from above (x to the right, z down), with the plaza through the middle:
--
--   +----------------+ +-----------+ +----------------+
--   | Lighting Hall  | | Stained   | | Lamp Avenue    |
--   | R  G  B lamps  | | Glass     | | posts + lava   |
--   +------door------+ +--opening--+ +----------------+
--   ===================== Plaza: Guide + UI kiosk ====
--   | Pool   | | Farm   | | Garden  | | Cow pen       |
--
-- What it shows off:
--   * Coloured light (Light.h): every block light is red, green and blue light spreading on its
--     own, so lamps mix - red + green = yellow, green + blue = cyan, all three = white. Blocks
--     give light with `light = 15, light_color = "#ff3030"`.
--   * Stained glass (`light_filter`): block light passing through it takes its colour (white
--     lamps behind coloured panes). Sunlight is one brightness, not a colour: glass doesn't tint it.
--   * Torches (plantlike, warm light), lava light, water dimming the light under it.
--   * Fluids flowing (a fountain), farming (wet farmland, crop stages, the scythe), grass
--     spreading onto dirt (random ticks), trees and flowers, doors, mobs (the entities API).
--   * The UI: a guide on the HUD (render_hud), a control panel made with ui_kit, the UI Kit
--     Showcase's laptop at the kiosk.
--   * The day: time of day and weather from the control panel, so you can see the lamps at night.
-- Every part is optional: what the game doesn't have (no farming, no mobs...) is skipped.

local kit = require("ui_kit")
local MOD = "game_showcase"

game_showcase = {}

-- ===========================================================================
-- Blocks
-- ===========================================================================

-- Colours: the lamps and the stained glass come in all of these
local COLORS = {
    { "red",    "#ff3030", "Red" },
    { "orange", "#ff8a20", "Orange" },
    { "yellow", "#ffe040", "Yellow" },
    { "green",  "#30ff50", "Green" },
    { "cyan",   "#30f0ff", "Cyan" },
    { "blue",   "#3a5cff", "Blue" },
    { "purple", "#b040ff", "Purple" },
    { "pink",   "#ff60c0", "Pink" },
    { "white",  "#ffffff", "White" },
}
local COLOR_OF = {}
for _, c in ipairs(COLORS) do COLOR_OF[c[1]] = c[2] end

-- A torch: drawn as a cross like a plant, warm light (14, as in Minecraft)
engine.register_block("gs_torch", 1, 1, 1, "gs_torch.png", nil, {
    transparent = true, walkable = false, selectable = true, pattern = "plantlike",
    light = 14, light_color = "#ffd9a0", hardness = 0,
})

for _, c in ipairs(COLORS) do
    -- Lamps: full-strength light in their colour
    engine.register_block("gs_lamp_" .. c[1], 1, 1, 1, "gs_lamp_" .. c[1] .. ".png", nil, {
        light = 15, light_color = c[2], hardness = 0.3,
    })
    -- Stained glass: see-through, and light passing through takes its colour
    engine.register_block("gs_glass_" .. c[1], 1, 1, 1, "gs_glass_" .. c[1] .. ".png", nil, {
        transparent = true, light_filter = c[2], hardness = 0.3,
    })
end

engine.register_block("gs_floor", 1, 1, 1, "gs_floor.png", nil, { hardness = 1.5 })
engine.register_block("gs_wall", 1, 1, 1, "gs_wall.png", nil, { hardness = 1.5 })
-- The Guide: right-click it for the control panel
engine.register_block("gs_guide", 1, 1, 1,
    { top = "gs_guide_top.png", sides = "gs_guide_side.png", bottom = "gs_guide_side.png" }, nil, { hardness = 1 })

-- ===========================================================================
-- Helpers
-- ===========================================================================

local function has_block(name) return engine.get_block_id(name) ~= 0 end
local function authority() return not engine.is_authority or engine.is_authority() end
local function say(text)
    print("[Showcase] " .. text)
    if chat and chat.send then chat.send(text) end
end

-- The park's corner (x, y = its floor level, z), remembered with the world
local origin = nil
do
    local saved = engine.get_storage and engine.get_storage(MOD .. ":origin")
    if saved then
        local x, y, z = saved:match("^(%-?%d+) (%-?%d+) (%-?%d+)$")
        if x then origin = { tonumber(x), tonumber(y), tonumber(z) } end
    end
end

local W, D = 48, 27   -- the park's size (x, z)

-- The stations: where each one is (park coordinates, inclusive), what it's about, where you
-- stand when you go there
local STATIONS = {
    { id = "hall", name = "Lighting Hall", x0 = 1, x1 = 17, z0 = 1, z1 = 12, stand = { 9, 4 },
      text = { "Red, green and blue lamps on the back wall. Their light spreads",
               "on its own, so it mixes: yellow, cyan, magenta - white where all meet.",
               "Torches by the door give warm light. Try it at midnight (F8)." } },
    { id = "glass", name = "Stained Glass Gallery", x0 = 19, x1 = 29, z0 = 1, z1 = 11, stand = { 24, 4 },
      text = { "White lamps shine through stained glass: light passing through",
               "glass takes its colour (light_filter), so the room glows like a",
               "rainbow. Look out of the east windows: glass tints everything",
               "you see through it too (several panes in a row multiply)." } },
    { id = "avenue", name = "Lamp Avenue", x0 = 31, x1 = 46, z0 = 1, z1 = 11, stand = { 38, 2 },
      text = { "Lamp posts in every colour, and a lava pit glowing orange.",
               "Lava is a fluid and a light source. Pick up lamps from the",
               "control panel (F8) and build your own." } },
    { id = "tanks", name = "Tank Lab", x0 = 30, x1 = 38, z0 = 12, z1 = 15, stand = { 34, 15 },
      text = { "Liquids in see-through tanks (the tanks mod), like Tinkers' Construct.",
               "Right-click a tank with a bucket to fill or empty it, with an empty",
               "hand to see how much is in it. Right-click a spout to pour it down." } },
    { id = "plaza", name = "Plaza", x0 = 0, x1 = 47, z0 = 13, z1 = 15, stand = { 24, 14 },
      text = { "The Guide (the lectern) opens the control panel; so does F8.",
               "Next to it, the UI Kit laptop: windows, drag and drop, widgets.",
               "Walk into each area - this panel tells you what it shows." } },
    { id = "pool", name = "Pool & Fountain", x0 = 1, x1 = 12, z0 = 16, z1 = 25, stand = { 6, 15 },
      text = { "Water flows from the top of the fountain into the pool.",
               "Water dims light (light_absorb): look how dark it gets down there.",
               "Swim in it, or scoop it up with a bucket." } },
    { id = "farm", name = "Farm", x0 = 14, x1 = 24, z0 = 16, z1 = 25, stand = { 19, 15 },
      text = { "Wet farmland next to water, wheat at every stage, growing on",
               "random ticks. Left-click the ripe rows with a scythe: it reaps",
               "the 3x3 patch and replants it. Hoe and scythe: F8." } },
    { id = "garden", name = "Garden", x0 = 26, x1 = 34, z0 = 16, z1 = 25, stand = { 30, 15 },
      text = { "A tree, flowers and grass. The bare dirt in the corner turns to",
               "grass bit by bit (random ticks), like Minecraft.",
               "Cut the tree and its leaves decay." } },
    { id = "pen", name = "Cow Pen", x0 = 36, x1 = 46, z0 = 16, z1 = 25, stand = { 41, 15 },
      text = { "Cows made with the entities API: they wander, path around",
               "obstacles and swim. Hit one and it runs. Mods add their own",
               "mobs the same way (entities.register)." } },
}

local function station_at(x, z)
    if not origin then return nil end
    local rx, rz = math.floor(x) - origin[1], math.floor(z) - origin[3]
    for _, s in ipairs(STATIONS) do
        if rx >= s.x0 - 1 and rx <= s.x1 + 1 and rz >= s.z0 - 1 and rz <= s.z1 + 1 then return s end
    end
    return nil
end

-- ===========================================================================
-- Building: every block goes in a queue, placed a few hundred a frame (no freeze)
-- ===========================================================================

local queue, qpos = {}, 1
local after_build = {}   -- functions to run once it's all placed (mobs...)
local building = false

local function put(x, y, z, name, meta)
    if not name or (name ~= "air" and not has_block(name)) then return end
    queue[#queue + 1] = { x, y, z, name, meta }
end

local function plan(ox, y, oz)
    queue, qpos, after_build = {}, 1, {}
    local function P(rx, ry, rz, name, meta) put(ox + rx, y + ry, oz + rz, name, meta) end
    local function box(x0, y0, z0, x1, y1, z1, name)
        for x = x0, x1 do for yy = y0, y1 do for z = z0, z1 do P(x, yy, z, name) end end end
    end
    local wood = has_block("planks_oak") and "planks_oak" or "gs_wall"
    local log = has_block("log_oak") and "log_oak" or wood

    -- 1. Clear the space and lay the floor (stone under it, so nothing hangs in the air)
    box(0, 0, 0, W - 1, 9, D - 1, "air")
    box(0, -2, 0, W - 1, -2, D - 1, "stone")
    box(0, -1, 0, W - 1, -1, D - 1, "gs_floor")
    -- a low rim around it (open at both ends of the plaza), so nothing outside runs in
    for x = 0, W - 1 do P(x, 0, 0, "gs_wall"); P(x, 0, D - 1, "gs_wall") end
    for z = 1, D - 2 do
        if z < 13 or z > 15 then P(0, 0, z, "gs_wall"); P(W - 1, 0, z, "gs_wall") end
    end

    -- 2. Lighting Hall: closed, so only its own lamps light it
    box(1, 0, 1, 17, 5, 1, "gs_wall"); box(1, 0, 12, 17, 5, 12, "gs_wall")
    box(1, 0, 1, 1, 5, 12, "gs_wall"); box(17, 0, 1, 17, 5, 12, "gs_wall")
    box(1, 6, 1, 17, 6, 12, "gs_wall")
    P(5, 1, 12, "gs_lamp_red"); P(9, 1, 12, "gs_lamp_green"); P(13, 1, 12, "gs_lamp_blue")
    P(2, 0, 2, "gs_torch"); P(16, 0, 2, "gs_torch")
    -- the door (or a doorway)
    P(9, 0, 1, "air"); P(9, 1, 1, "air")
    if has_block("door_oak") then P(9, 0, 1, "door_oak", 0); P(9, 1, 1, "door_oak", 8) end

    -- 3. Stained Glass Gallery: a closed room. Along the back, white lamps set in the wall, each
    --    behind a pane of stained glass: their light reaches the room only through the glass
    box(19, 0, 1, 29, 4, 1, "gs_wall"); box(19, 0, 11, 29, 4, 12, "gs_wall")
    box(19, 0, 1, 19, 4, 11, "gs_wall"); box(29, 0, 1, 29, 4, 11, "gs_wall")
    box(19, 5, 1, 29, 5, 12, "gs_wall")
    -- five colours, two apart, so each throws its own pool of colour before they blend
    for i, c in ipairs({ "red", "yellow", "green", "blue", "purple" }) do
        local x = 18 + i * 2
        for _, yy in ipairs({ 0, 1, 2 }) do
            P(x, yy, 11, "gs_lamp_white"); P(x, yy, 10, "gs_glass_" .. c)
        end
    end
    P(24, 0, 1, "air"); P(24, 1, 1, "air")                          -- the way in (small: little daylight)
    -- windows of every colour in the east wall: look out at the lamp posts through them
    for i, c in ipairs({ "red", "orange", "yellow", "green", "cyan", "blue", "purple" }) do
        P(29, 1, 1 + i, "gs_glass_" .. c); P(29, 2, 1 + i, "gs_glass_" .. c)
    end

    -- 4. Lamp Avenue: posts with a lamp on top, a lava pit behind a glass rail
    local n = 0
    for _, z in ipairs({ 3, 9 }) do
        for x = 32, 44, 3 do
            n = n + 1
            local c = COLORS[(n - 1) % #COLORS + 1][1]
            P(x, 0, z, log); P(x, 1, z, log); P(x, 2, z, "gs_lamp_" .. c)
        end
    end
    if has_block("lava") then
        box(36, -2, 5, 40, -2, 7, "gs_wall")
        box(37, -1, 6, 39, -1, 6, "lava")
        for x = 36, 40 do P(x, 0, 5, "gs_glass_white"); P(x, 0, 7, "gs_glass_white") end
        P(36, 0, 6, "gs_glass_white"); P(40, 0, 6, "gs_glass_white")
    end

    -- 5. Plaza: the Guide and the UI kiosk
    P(24, 0, 13, "gs_guide")
    if has_block("showcase_laptop") then P(22, 0, 13, "gs_wall"); P(22, 1, 13, "showcase_laptop") end
    P(20, 0, 13, "gs_torch"); P(28, 0, 13, "gs_torch")

    -- 5b. Tank Lab (the tanks mod): tanks at different levels, and two spouts pouring
    if has_block("tanks_tank") then
        local function tank(x, yy, fluid, level)
            if fluid and has_block("tanks_tank_" .. fluid) then P(x, yy, 13, "tanks_tank_" .. fluid, level)
            else P(x, yy, 13, "tanks_tank", 0) end
        end
        tank(31, 0, "water", 16); tank(32, 0, "water", 8); tank(33, 0, "lava", 12); tank(33, 1, "lava", 4)
        for _, s in ipairs({ { 35, "lava" }, { 37, "water" } }) do
            tank(s[1], 2, s[2], 16)
            P(s[1], 1, 13, "tanks_spout", 0)
            tank(s[1], 0, nil, 0)
            after_build[#after_build + 1] = function()
                if tanks and tanks.start_pouring then tanks.start_pouring(ox + s[1], y + 1, oz + 13) end
            end
        end
    end

    -- 6. Pool & fountain: a basin two deep, a pillar with a water source on top
    if has_block("water") then
        box(1, -4, 16, 11, -1, 24, "gs_wall")
        box(2, -3, 17, 10, -1, 23, "water")
        box(6, -3, 20, 6, 1, 20, "gs_wall")
        P(6, 2, 20, "water")
        P(1, 0, 16, "gs_torch"); P(11, 0, 16, "gs_torch")
    end

    -- 7. Farm: wet farmland, a water channel, wheat at every stage
    if has_block("farmland_wet") then
        box(14, -3, 16, 24, -2, 24, "stone")
        box(15, -1, 17, 23, -1, 23, "farmland_wet")
        if has_block("water") then box(15, -1, 20, 23, -1, 20, "water") end
        local stages = { "wheat_stage_0", "wheat_stage_1", "wheat_stage_2", "wheat_ripe" }
        for x = 15, 23 do
            local s = stages[math.min(4, math.floor((x - 15) / 2) + 1)]
            for z = 17, 23 do if z ~= 20 then P(x, 0, z, s) end end
        end
    end

    -- 8. Garden: grass, a tree, flowers, a dirt patch for the grass to spread onto
    if has_block("grass") then
        box(27, -1, 17, 33, -1, 24, "grass")
        box(27, -1, 23, 29, -1, 24, "dirt")
        local flowers = { "poppy", "flower_daisy", "flower_bluebell", "flower_tulip_red", "flower_allium",
                          "flower_azure_bluet", "flower_tulip_pink", "short_grass" }
        local spots = { { 28, 17 }, { 32, 17 }, { 33, 19 }, { 27, 19 }, { 31, 22 }, { 33, 23 }, { 28, 21 }, { 32, 21 },
                        { 30, 24 }, { 33, 17 } }
        for i, s in ipairs(spots) do P(s[1], 0, s[2], flowers[(i - 1) % #flowers + 1]) end
        -- a tree
        local leaves = has_block("leaves_oak") and "leaves_oak" or nil
        if leaves then
            for dx = -2, 2 do for dz = -2, 2 do for dy = 3, 5 do
                if math.abs(dx) + math.abs(dz) + math.max(0, dy - 4) * 2 <= 3 then P(30 + dx, dy, 20 + dz, leaves) end
            end end end
        end
        for dy = 0, 4 do P(30, dy, 20, log) end
    end

    -- 9. Cow pen: grass, a fence (planks), glass at the front to look through
    if has_block("grass") then box(37, -1, 17, 45, -1, 24, "grass") end
    box(36, 0, 16, 46, 1, 16, "gs_glass_white")
    box(36, 0, 25, 46, 1, 25, wood); box(36, 0, 16, 36, 1, 25, wood); box(46, 0, 16, 46, 1, 25, wood)
    after_build[#after_build + 1] = function()
        if entities and entities.spawn then           -- (nothing happens without the mobs mod)
            for i = 1, 3 do pcall(entities.spawn, "mobs:cow", ox + 38.5 + i * 2, y + 0.1, oz + 19.5 + (i % 2) * 2) end
        end
    end
end

local function stand_at(s)
    if not origin then return end
    engine.set_player_position(origin[1] + s.stand[1] + 0.5, origin[2] + 0.1, origin[3] + s.stand[2] + 0.5)
end

-- Builds the park around the player: they end up on the plaza
function game_showcase.build()
    if not authority() then
        say("The showcase builds in your own world: singleplayer, or when you host a LAN game.")
        return false
    end
    if building then say("Still building the showcase...") return false end
    local px, py, pz = engine.get_player_position()
    local ox, oz = math.floor(px) - 24, math.floor(pz) - 14
    -- The floor goes where the player is standing, or the ground under them. Under water, it
    -- goes on the surface instead (a park on the sea floor would just fill with water).
    local fx, fz = math.floor(px), math.floor(pz)
    local function wet(yy)
        local n = engine.get_block_name(fx, yy, fz)
        return n:find("water", 1, true) ~= nil or n:find("lava", 1, true) ~= nil
    end
    local y = math.floor(py + 0.01)
    for _ = 1, 128 do if not wet(y) then break end y = y + 1 end
    for _ = 1, 40 do
        if engine.get_block_name(fx, y - 1, fz) ~= "air" then break end
        y = y - 1
    end
    origin = { ox, y, oz }
    if engine.set_storage then engine.set_storage(MOD .. ":origin", string.format("%d %d %d", ox, y, oz)) end
    plan(ox, y, oz)
    building = true
    say(string.format("Building the Showcase Park (%d blocks)...", #queue))
    return true
end

local function build_step()
    if not building then return end
    local placed = 0
    while qpos <= #queue and placed < 700 do
        local b = queue[qpos]; qpos = qpos + 1
        if b[5] then
            engine.set_block_meta(b[1], b[2], b[3], b[4], b[5]); placed = placed + 8
        elseif engine.get_block_name(b[1], b[2], b[3]) ~= b[4] then
            engine.set_block(b[1], b[2], b[3], b[4]); placed = placed + 8
        else
            placed = placed + 1                                  -- (already right: cheap)
        end
    end
    if qpos > #queue then
        building = false
        for _, fn in ipairs(after_build) do fn() end
        queue = {}
        say("The Showcase Park is ready. Walk around - the guide (top left) tells you what each part shows. F8: control panel.")
    end
end

function game_showcase.is_building() return building end
function game_showcase.origin() if origin then return origin[1], origin[2], origin[3] end end

function game_showcase.go(id)
    if not origin then say("No showcase in this world yet - type /showcase to build one.") return end
    for _, s in ipairs(STATIONS) do if s.id == (id or "plaza") then stand_at(s) return end end
end

-- ===========================================================================
-- The control panel (F8, /showcase panel, right-click the Guide)
-- ===========================================================================

local TIMES = { { "Sunrise", 0.5 }, { "Noon", 0.75 }, { "Sunset", 0.0 }, { "Midnight", 0.25 } }

local function light_here()
    local px, py, pz = engine.get_player_position()
    if not engine.get_light then return nil end
    return { engine.get_light(math.floor(px), math.floor(py + 0.5), math.floor(pz)) }
end

local function give(name, count)
    if not has_block(name) then return end
    if engine.give_item then engine.give_item(name, count or 1) end
end

local panel = kit.form{
    id = MOD .. ":panel", width = 21, height = 13.6, title = "Showcase control panel",
    draw = function(f, st)
        local mine = authority()
        local y = 0
        -- Time of day
        f:label(0, y, "Time of day", { color = kit.theme.title })
        if sky and sky.get_clock then f:label(4.2, y, sky.get_clock(), { color = kit.theme.muted }) end
        y = y + 0.8
        for i, t in ipairs(TIMES) do
            if f:button("time" .. i, (i - 1) * 3.3, y, 3.1, 1, t[1], { disabled = not mine }) then sky.set_time(t[2], true) end
        end
        if sky and sky.get_time_speed then
            local stopped = sky.get_time_speed() == 0
            local now = f:checkbox("freeze", 13.4, y + 0.1, "Stop the clock", stopped, { disabled = not mine })
            if mine and now ~= stopped then sky.set_time_speed(now and 0 or 1) end
        end
        y = y + 1.6

        -- Weather
        if weather and weather.list then
            f:label(0, y, "Weather", { color = kit.theme.title })
            f:label(4.2, y, weather.get and weather.get() or "", { color = kit.theme.muted })
            y = y + 0.8
            local x = 0
            for i, name in ipairs(weather.list()) do
                local label = name:gsub("_", " "):gsub("^%l", string.upper)
                local w = math.min(6, f:button_width(label, 1))
                if x + w > 20.5 then x = 0; y = y + 1.2 end
                if f:button("w" .. i, x, y, w, 1, label, { disabled = not mine }) then weather.set(name, 3) end
                x = x + w + 0.2
            end
            y = y + 1.6
        end

        -- Go to
        f:label(0, y, "Go to", { color = kit.theme.title })
        y = y + 0.8
        if origin then
            local x = 0
            for _, s in ipairs(STATIONS) do
                local w = f:button_width(s.name, 1)
                if x + w > 20.5 then x = 0; y = y + 1.2 end
                if f:button("go_" .. s.id, x, y, w, 1, s.name, { disabled = not mine }) then f:close(); stand_at(s) end
                x = x + w + 0.2
            end
        else
            f:label(0, y, "Nothing built yet:", { color = kit.theme.muted })
            if f:button("build", 4.5, y - 0.15, 6, 1, "Build it here", { disabled = not mine }) then
                f:close(); game_showcase.build()
            end
        end
        y = y + 1.6

        -- Take
        f:label(0, y, "Take", { color = kit.theme.title })
        f:label(2.8, y, "(hover for the name; click to get it)", { color = kit.theme.muted, size = 0.32 })
        y = y + 0.8
        local items = { "gs_torch" }
        for _, c in ipairs(COLORS) do items[#items + 1] = "gs_lamp_" .. c[1] end
        for _, c in ipairs(COLORS) do items[#items + 1] = "gs_glass_" .. c[1] end
        for _, n in ipairs({ "tanks_tank", "tanks_spout", "bucket", "water_bucket", "lava_bucket",
                            "wooden_hoe", "scythe", "wheat_seeds", "showcase_remote", "gs_guide" }) do
            if has_block(n) then items[#items + 1] = n end
        end
        for i, n in ipairs(items) do
            local col, row = (i - 1) % 14, math.floor((i - 1) / 14)
            local label = n:gsub("^gs_", ""):gsub("_", " ")
            if f:item(col * 1.48, y + row * 1.48, 1.35, n, { id = "it" .. i, tooltip = label }) then
                give(n, n:find("^gs_") and 16 or 1)
            end
        end
        y = y + math.ceil(#items / 14) * 1.48 + 0.4

        -- Light meter: what the light is where you stand
        local l = light_here()
        if l then
            f:label(0, y, "Light where you stand", { color = kit.theme.title })
            local names = { { "sun", { 1, 0.95, 0.6, 1 } }, { "red", { 1, 0.25, 0.25, 1 } },
                            { "green", { 0.3, 1, 0.35, 1 } }, { "blue", { 0.35, 0.5, 1, 1 } } }
            for i, nm in ipairs(names) do
                local x = 7 + (i - 1) * 3.5
                f:label(x, y, nm[1], { size = 0.32, color = kit.theme.muted })
                f:rect(x + 1.2, y + 0.05, 2, 0.35, { 0.1, 0.1, 0.12, 1 })
                f:rect(x + 1.2, y + 0.05, 2 * (l[i] or 0) / 15, 0.35, nm[2])
            end
            -- the colour block light makes here
            f:rect(20.2, y - 0.05, 0.6, 0.55, { (l[2] or 0) / 15, (l[3] or 0) / 15, (l[4] or 0) / 15, 1 })
        end
        if not mine then
            f:label(0, y + 0.8, "On a server: time, weather and teleports are the server's to decide.",
                    { size = 0.32, color = kit.theme.muted })
        end
    end,
}

function game_showcase.panel()
    if panel:is_open() then panel:close() return end
    if engine.is_inventory_open and engine.is_inventory_open() and not kit.current() then return end
    panel:open({})
end

-- ===========================================================================
-- The guide on the HUD: where you are and what it shows
-- ===========================================================================

local previous_hud = render_hud
function render_hud(Wpx, Hpx, ...)
    if previous_hud then previous_hud(Wpx, Hpx, ...) end
    if not origin or not ui or not ui.draw_text or kit.current() then return end
    local px, py, pz = engine.get_player_position()
    if math.abs(py - origin[2]) > 24 then return end
    local s = station_at(px, pz)
    if not s then return end                       -- outside the park: nothing to say
    local k = math.max(0.8, Hpx / 900)
    local pad, line = 12 * k, 20 * k
    local title_scale, text_scale = 1.15 * k, 0.8 * k
    local lines = s and s.text or {}
    local w = 0
    for _, t in ipairs(lines) do w = math.max(w, ui.text_width and ui.text_width(t, text_scale) or #t * 8 * text_scale) end
    w = math.max(w, 300 * k) + pad * 2
    local h = pad * 2 + line * 1.4 + #lines * line + line
    local x, y = 14 * k, 14 * k
    if ui.draw_rounded_rect then ui.draw_rounded_rect(x, y, w, h, 8 * k, 0.06, 0.06, 0.09, 0.78)
    else ui.draw_rect(x, y, w, h, 0.06, 0.06, 0.09, 0.78) end
    ui.draw_text("Showcase Park  -  " .. (s and s.name or ""), x + pad, y + pad, title_scale, 1, 0.88, 0.55, 1)
    for i, t in ipairs(lines) do
        ui.draw_text(t, x + pad, y + pad + line * 1.4 + (i - 1) * line, text_scale, 0.92, 0.92, 0.95, 1)
    end
    ui.draw_text("F8: control panel (time, weather, teleports, items, light meter)", x + pad,
                 y + pad + line * 1.4 + #lines * line + 2 * k, text_scale * 0.9, 0.6, 0.65, 0.75, 1)
end

-- ===========================================================================
-- Ways in: /showcase, F8, the Guide block
-- ===========================================================================

if chat and chat.register_command then
    chat.register_command("showcase", "/showcase [go|panel]", "Build the Showcase Park here (go: back to it, panel: control panel)")
end
local previous_command = on_chat_command
function on_chat_command(text, ...)
    local cmd, arg = text:match("^(%S+)%s*(.-)%s*$")
    if cmd == "showcase" then
        if arg == "" or arg == "build" then game_showcase.build()
        elseif arg == "go" then game_showcase.go("plaza")
        elseif arg == "panel" then game_showcase.panel()
        else say("Usage: /showcase [go|panel]") end
        return true
    end
    if previous_command then return previous_command(text, ...) end
    return false
end

local previous_update = on_update
function on_update(dt, ...)
    build_step()
    for _, k in ipairs(ui and ui.get_key_events and ui.get_key_events() or {}) do
        if k.key == ui.keys.F8 and k.action == "press" then game_showcase.panel() end
    end
    if previous_update then return previous_update(dt, ...) end
end

local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "gs_guide" and not sneaking then
        if not repeat_click then game_showcase.panel() end
        return true
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end

-- The creative inventory: a tab with this mod's blocks
if creative and creative.register_tab then
    creative.register_tab{ name = "Showcase", icon = "gs_lamp_purple" }
end

print("[game_showcase] loaded: /showcase builds the Showcase Park, F8 opens its control panel")
