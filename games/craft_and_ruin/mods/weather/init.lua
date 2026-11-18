-- Weather: decides the weather and makes it look right. The engine only draws effects
-- (sky.set_precipitation, set_sky_tint, set_light, flash...); everything about WHICH
-- weather, WHEN and HOW it changes is here, so games and mods can change any of it.
--
-- Feels like Minecraft out of the box: long clear spells, rain now and then, sometimes a
-- thunderstorm, snow instead of rain where it's cold. Mods add or change weathers Luanti
-- style:
--
--   weather.register("sandstorm", {
--       light = 0.7,                                   -- sunlight on the world (1 = normal)
--       sky_tint = { 0.8, 0.65, 0.45, 0.6 },           -- r, g, b, how much (0..1)
--       rain = 0.6,                                    -- 0..1 thickens the fog
--       precipitation = { type = "dust", texture = "sand_grain.png", intensity = 0.7,
--                         fall_speed = 3, size = 0.06, streaks = false },
--       wind = { 6, 1 },                               -- blocks/s, x and z
--       lightning = 0,                                 -- flashes per minute
--       clouds = nil,                                  -- optional: clouds.set_weather(name)
--       chance = 5,                                    -- how often the cycle picks it (0 = never)
--       duration = { 120, 300 },                       -- seconds, min and max
--       fade = 15,                                     -- seconds to fade in
--       cold_variant = nil,                            -- weather to show instead where it's cold
--   })
--
--   weather.set("rain" [, fade_seconds [, duration_seconds]])   -- start a weather now
--   weather.get()                       -- current weather name
--   weather.list()                      -- every registered name
--   weather.set_cycle(false)            -- stop changing by itself (e.g. for a minigame)
--   weather.on_change(function(old, new) ... end)
--   weather.on_lightning(function(x, z) ... end)    -- a flash near the player (client side)
--   weather.is_cold = function(x, z) return ... end -- where rain should be snow
--
-- Multiplayer: whoever runs the world (singleplayer, the host or the dedicated server)
-- decides, using engine.set_shared("weather", ...); every player sees the same weather.

weather = weather or {}

local types, order = {}, {}
local change_callbacks, lightning_callbacks = {}, {}
local cycle_enabled = true
local current = nil          -- name everyone agreed on (from the shared value)
local remaining = 0          -- seconds until the cycle picks again (authority only)
local started = false

local is_server = engine.is_server and engine.is_server() or false

-- ---------------------------------------------------------------------------
-- Registry
-- ---------------------------------------------------------------------------
local function color4(c, default)
    c = c or default
    return { c[1] or 0, c[2] or 0, c[3] or 0, c[4] or 0 }
end

function weather.register(name, def)
    def = def or {}
    def.name = name
    def.light = def.light or 1.0
    def.rain = def.rain or 0.0
    def.sky_tint = color4(def.sky_tint, { 0.5, 0.55, 0.6, 0 })
    def.wind = def.wind or { 0, 0 }
    def.lightning = def.lightning or 0
    def.chance = def.chance or 0
    def.duration = def.duration or { 300, 900 }
    def.fade = def.fade or 12
    if not types[name] then order[#order + 1] = name end
    types[name] = def
end

function weather.get() return current end
function weather.get_def(name) return types[name] end
function weather.list() local l = {} for i, n in ipairs(order) do l[i] = n end return l end
function weather.set_cycle(on) cycle_enabled = on and true or false end
function weather.is_cycling() return cycle_enabled end
function weather.time_left() return remaining end
function weather.on_change(fn) change_callbacks[#change_callbacks + 1] = fn end
function weather.on_lightning(fn) lightning_callbacks[#lightning_callbacks + 1] = fn end

-- Games override this to make rain fall as snow in cold places, e.g.
--   weather.is_cold = function(x, z) local _, temp = get_biome_parameters(x, z) return temp < 0.25 end
function weather.is_cold(x, z) return false end

local function random_duration(def)
    local lo, hi = def.duration[1] or 300, def.duration[2] or def.duration[1] or 900
    return lo + math.random() * math.max(0, hi - lo)
end

-- Start a weather. Only whoever runs the world can (when joined, the server decides).
function weather.set(name, fade, duration)
    local def = types[name]
    if not def then return false, "Unknown weather '" .. tostring(name) .. "'" end
    if not engine.is_authority() then return false, "Only the server can change the weather" end
    remaining = duration or random_duration(def)
    engine.set_shared("weather", name .. ";" .. tostring(fade or def.fade))
    return true
end

-- ---------------------------------------------------------------------------
-- Minecraft-like defaults (games can re-register any of these to change them)
-- ---------------------------------------------------------------------------
weather.register("clear", {
    chance = 0,                     -- the cycle comes back to clear by itself (see pick_next)
    duration = { 600, 1500 },       -- 10 - 25 minutes
    fade = 15,
})
weather.register("cloudy", {
    light = 0.85, rain = 0.1, sky_tint = { 0.62, 0.66, 0.7, 0.3 },
    chance = 30, duration = { 240, 600 }, fade = 20,
})
weather.register("rain", {
    light = 0.68, rain = 0.75, sky_tint = { 0.45, 0.5, 0.55, 0.55 },
    precipitation = { type = "rain", intensity = 0.75 },
    wind = { 0.6, 0.3 },
    chance = 45, duration = { 180, 540 }, fade = 12,
    cold_variant = "snow",
})
weather.register("thunder", {
    light = 0.45, rain = 1.0, sky_tint = { 0.28, 0.31, 0.36, 0.78 },
    precipitation = { type = "rain", intensity = 1.0, fall_speed = 17 },
    wind = { 2.5, 1.2 },
    lightning = 7,
    chance = 12, duration = { 120, 360 }, fade = 10,
    cold_variant = "blizzard",
})
weather.register("snow", {
    light = 0.82, rain = 0.45, sky_tint = { 0.78, 0.8, 0.84, 0.45 },
    precipitation = { type = "snow", intensity = 0.7 },
    wind = { 0.4, 0.2 },
    chance = 0, duration = { 240, 600 }, fade = 15,      -- comes from rain's cold_variant
})
weather.register("blizzard", {
    light = 0.6, rain = 0.9, sky_tint = { 0.8, 0.82, 0.86, 0.75 },
    precipitation = { type = "snow", intensity = 1.0, fall_speed = 4, sway = 1.2 },
    wind = { 5, 2 },
    chance = 0, duration = { 120, 300 }, fade = 10,
})

-- Clear most of the time; after bad weather it usually clears up again.
local function pick_next()
    if current ~= "clear" and types.clear and math.random() < 0.8 then return "clear" end
    local total = 0
    for _, n in ipairs(order) do
        if n ~= current then total = total + (types[n].chance or 0) end
    end
    if total <= 0 then return "clear" end
    local r = math.random() * total
    for _, n in ipairs(order) do
        if n ~= current then
            r = r - (types[n].chance or 0)
            if r <= 0 then return n end
        end
    end
    return "clear"
end

-- ---------------------------------------------------------------------------
-- Looks: fade between the old and the new weather's values
-- ---------------------------------------------------------------------------
local vis_from, vis_to = nil, nil   -- the two looks being blended
local vis_t, vis_fade = 1, 0
local shown_name = nil              -- the look on screen (may be a cold_variant)
local cold_check = 0
local next_flash, double_flash = nil, nil

local function look_of(def)
    local p = def.precipitation
    return {
        light = def.light, rain = def.rain,
        tint = { def.sky_tint[1], def.sky_tint[2], def.sky_tint[3], def.sky_tint[4] },
        wind = { def.wind[1] or 0, def.wind[2] or 0 },
        lightning = def.lightning,
        precip = p, precip_amount = p and (p.intensity or 0.7) or 0,
    }
end

local function lerp(a, b, t) return a + (b - a) * t end

-- The look part-way (t = 0..1) from look a to look b
local function blend(a, b, t)
    local r = { light = lerp(a.light, b.light, t), rain = lerp(a.rain, b.rain, t),
                wind = { lerp(a.wind[1], b.wind[1], t), lerp(a.wind[2], b.wind[2], t) },
                lightning = lerp(a.lightning, b.lightning, t), tint = {} }
    -- tint: blend the colours and the amount (a tint that's off takes the other's colour)
    for i = 1, 4 do r.tint[i] = lerp(a.tint[i], b.tint[i], t) end
    if a.tint[4] == 0 then for i = 1, 3 do r.tint[i] = b.tint[i] end end
    if b.tint[4] == 0 then for i = 1, 3 do r.tint[i] = a.tint[i] end end
    -- precipitation: same kind -> blend the amount; different kinds -> old fades out, new fades in
    local pa, pb = a.precip, b.precip
    if pa and pb and pa.type == pb.type then
        r.precip, r.precip_amount = pb, lerp(a.precip_amount, b.precip_amount, t)
    elseif pb and not pa then                         -- starting to rain: over the whole fade
        r.precip, r.precip_amount = pb, b.precip_amount * t
    elseif pa and not pb then                         -- stopping
        r.precip, r.precip_amount = pa, a.precip_amount * (1 - t)
    elseif t < 0.5 and pa then
        r.precip, r.precip_amount = pa, a.precip_amount * (1 - t * 2)
    elseif pb then
        r.precip, r.precip_amount = pb, b.precip_amount * math.max(0, t * 2 - 1)
    else
        r.precip, r.precip_amount = nil, 0
    end
    return r
end

local function on_screen()
    if not vis_to then return nil end
    return blend(vis_from or vis_to, vis_to, vis_t)
end

local function apply_look()
    if is_server or not vis_to then return end
    local l = on_screen()
    sky.set_light(l.light)
    sky.set_rain_intensity(l.rain)
    sky.set_sky_tint(l.tint[1], l.tint[2], l.tint[3], l.tint[4])
    if l.precip then
        local s = {}
        for k, v in pairs(l.precip) do s[k] = v end
        s.intensity, s.wind_x, s.wind_z = l.precip_amount, l.wind[1], l.wind[2]
        sky.set_precipitation(s)
    else
        sky.set_precipitation{ intensity = 0 }
    end
end

-- Show weather `name` (or its cold variant here), fading over `fade` seconds
local function show(name, fade)
    local def = types[name]
    if not def then return end
    local px, _, pz = 0, 0, 0
    if engine.get_player_position then px, _, pz = engine.get_player_position() end
    if def.cold_variant and types[def.cold_variant] and weather.is_cold(px, pz) then
        def = types[def.cold_variant]
    end
    if def.name == shown_name and vis_to then return end
    -- start from exactly what's on screen right now (even mid-fade)
    vis_from = on_screen() or look_of(types.clear or def)
    vis_to = look_of(def)
    shown_name = def.name
    vis_fade = math.max(fade or 0, 0)
    vis_t = vis_fade > 0 and 0 or 1
    if def.clouds and clouds and clouds.set_weather then clouds.set_weather(def.clouds, vis_fade) end
    if def.cloud_color and clouds and clouds.set_color then
        clouds.set_color(def.cloud_color[1], def.cloud_color[2], def.cloud_color[3])
    end
    apply_look()
end

-- The agreed weather changed (set here, or sent by the server)
local function on_weather_value(value)
    local name, fade = value:match("^([^;]+);?([%d%.]*)")
    if not name or not types[name] then return end
    local old = current
    current = name
    -- the very first weather after joining/loading shows straight away
    show(name, started and tonumber(fade) or 0)
    started = true
    if old ~= name then
        for _, fn in ipairs(change_callbacks) do
            local ok, err = pcall(fn, old, name)
            if not ok then print("[Weather] on_change callback failed: " .. tostring(err)) end
        end
    end
end

local function lightning(dt)
    if is_server or not vis_to then return end
    local rate = on_screen().lightning   -- flashes per minute
    if double_flash then
        double_flash = double_flash - dt
        if double_flash <= 0 then sky.flash(0.6, 0.25); double_flash = nil end
    end
    if rate <= 0 or math.random() > rate / 60 * dt then return end
    sky.flash(0.7 + math.random() * 0.3, 0.35)
    if math.random() < 0.4 then double_flash = 0.12 + math.random() * 0.1 end
    local px, _, pz = 0, 0, 0
    if engine.get_player_position then px, _, pz = engine.get_player_position() end
    local ang, dist = math.random() * 6.283, 20 + math.random() * 80
    local lx, lz = px + math.cos(ang) * dist, pz + math.sin(ang) * dist
    for _, fn in ipairs(lightning_callbacks) do pcall(fn, lx, lz) end
end

-- ---------------------------------------------------------------------------
-- Hooks (each keeps any other mod's version working)
-- ---------------------------------------------------------------------------
local previous_shared = on_shared_changed
function on_shared_changed(key, value)
    if key == "weather" then on_weather_value(value) end
    if previous_shared then return previous_shared(key, value) end
end

local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end

    -- Whoever runs the world decides
    if engine.is_authority() then
        if engine.get_shared("weather") == "" then
            weather.set("clear", 0)                       -- a new world starts clear
        elseif cycle_enabled then
            remaining = remaining - dt
            if remaining <= 0 then weather.set(pick_next()) end
        end
    end

    if is_server then return end
    -- Walking into a cold place turns rain into snow (checked every 2 s)
    cold_check = cold_check - dt
    if cold_check <= 0 and current then
        cold_check = 2
        show(current, 6)
    end
    if vis_t < 1 then
        vis_t = math.min(1, vis_t + dt / math.max(vis_fade, 0.01))
        apply_look()
    end
    lightning(dt)
end

-- /weather [type [seconds]] | list | cycle on|off
chat.register_command("weather", "/weather [type [seconds]] | list | cycle on|off", "Show or change the weather")
local previous_command = on_chat_command
function on_chat_command(text, player, is_operator, reply)
    local args = {}
    for w in text:gmatch("%S+") do args[#args + 1] = w end
    if args[1] ~= "weather" then
        if previous_command then return previous_command(text, player, is_operator, reply) end
        return false
    end
    if not engine.is_authority() then return false end      -- joined: the server answers
    local say = reply or function(t) chat.send(t, 0.75, 0.85, 1) end

    local sub = args[2]
    if not sub then
        say(string.format("Weather: %s (%s, next change in %d s)", tostring(current),
            cycle_enabled and "changes by itself" or "cycle off", math.max(0, math.floor(remaining))))
        return true
    end
    if sub == "list" then say("Weathers: " .. table.concat(order, ", ")); return true end
    if is_operator == false then say("Only operators can change the weather"); return true end
    if sub == "cycle" then
        weather.set_cycle(args[3] ~= "off")
        say("Weather cycle " .. (cycle_enabled and "on" or "off"))
        return true
    end
    local ok, err = weather.set(sub, nil, tonumber(args[3]))
    if ok then
        say("Weather set to " .. sub .. (args[3] and (" for " .. args[3] .. " s") or ""))
    else
        say(err)
    end
    return true
end

print("[Weather] Ready: " .. table.concat(order, ", "))
