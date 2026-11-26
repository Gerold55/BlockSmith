-- Aurora: now and then, on a clear night, an aurora over the land. The engine only draws it
-- (sky.set_aurora); this mod decides which nights, where it's seen, and how it looks. Games
-- change any of that with aurora.set_options, like weather.register.
--
--   aurora.set_options({
--       chance = 0.12,                       -- the chance a night has one (0..1)
--       where = function(x, y, z) ... end,   -- where it can be seen (default: everywhere;
--                                            -- e.g. cold biomes). Asked in each player's game.
--       strength = { 0.7, 1.3 },             -- picked for each aurora night, lowest to highest
--       palettes = { { "#4cf2d1", "#8c57ff" }, ... },   -- colours, one picked each night:
--                                            -- the bottom of the curtains and the top
--       speed = 1, size = 1, direction = 0,  -- as for sky.set_aurora (0 = in the north)
--       style = "smooth", pixels = 80,       -- as for sky.set_aurora ("pixel": square sky pixels)
--       haze = 1, light = 1,                 -- the glow round the curtains, and on the land below
--       fade = 20,                           -- seconds to fade in and out
--       hidden_by = { "cloudy", "rain", ... },   -- weathers whose clouds hide it
--   })
--   aurora.start([palette]) / aurora.stop()  -- tonight (where the world runs: commands, events)
--   aurora.is_active() -> bool               -- is tonight an aurora night
--   aurora.is_visible() -> bool              -- is it showing in this game right now
--   aurora.on_change(function(active) ... end)
--
-- Multiplayer: whoever runs the world decides each night (engine.set_shared("aurora", ...)), so
-- everyone sees the same aurora night; each player's game shows it where `where` says.

aurora = aurora or {}
local A = aurora

local is_server = engine.is_server and engine.is_server() or false

local options = {
    chance = 0.12,
    where = function() return true end,
    strength = { 0.7, 1.3 },
    palettes = {
        { "#4cf2d1", "#8c57ff" },   -- teal, violet at the top (the usual)
        { "#59ff7a", "#a24cff" },   -- green, violet at the top
        { "#59ff7a", "#ff3c78" },   -- green, red at the top
    },
    speed = 1, size = 1, direction = 0,
    style = "smooth", pixels = 80,
    haze = 1, light = 1,
    fade = 20,
    hidden_by = { "cloudy", "rain", "thunder", "snow", "blizzard" },
}
local callbacks = {}
local state = { active = false, strength = 1, palette = 1 }   -- tonight (the shared value)
local shown = nil       -- what this game shows: nil (not yet), false, or the look's key

function A.set_options(t)
    assert(type(t) == "table", "aurora.set_options({ chance = ..., where = ... })")
    for k, v in pairs(t) do options[k] = v end
    shown = nil     -- (look again)
end
function A.get_options() return options end
function A.is_active() return state.active end
function A.on_change(fn) callbacks[#callbacks + 1] = fn end

local function authority() return not engine.is_authority or engine.is_authority() end

-- "on;strength;palette" or "off"
local function share(active, strength, palette)
    local v = active and string.format("on;%.2f;%d", strength, palette) or "off"
    engine.set_shared("aurora", v)
    if engine.set_storage then engine.set_storage("aurora:tonight", v) end
end

-- Which night it is: nights run from sunset (time 0) to sunrise (0.5); the day number goes up at
-- midnight (0.25). nil in the daytime.
local function night_key()
    if not sky or not sky.get_time then return nil end
    local t = sky.get_time()
    if type(t) ~= "number" or t >= 0.5 then return nil end
    local d = sky.get_day and sky.get_day() or 0
    return (t < 0.25) and (d + 1) or d
end

-- The night now, or (by day) the coming one
local function this_or_next_night()
    local key = night_key()
    if key then return key end
    return (sky and sky.get_day and sky.get_day() or 0) + 1
end

function A.start(palette)
    if not authority() then return false, "Only the server can start an aurora" end
    local lo, hi = options.strength[1] or 1, options.strength[2] or options.strength[1] or 1
    palette = tonumber(palette) or math.random(#options.palettes)
    share(true, lo + math.random() * (hi - lo), math.max(1, math.min(#options.palettes, palette)))
    -- (this night is decided: not rolled again at dusk)
    if engine.set_storage then engine.set_storage("aurora:night", tostring(this_or_next_night())) end
    return true
end
function A.stop()
    if not authority() then return false, "Only the server can stop an aurora" end
    share(false)
    if engine.set_storage then engine.set_storage("aurora:night", tostring(this_or_next_night())) end
    return true
end

-- Where the world runs: one roll per night
local last_night = nil
local function decide()
    local key = night_key()
    if key == nil then
        if last_night ~= nil and state.active then share(false) end   -- (morning: over)
        last_night = nil
        return
    end
    if key == last_night then return end
    last_night = key
    local rolled = engine.get_storage and engine.get_storage("aurora:night")
    if rolled == tostring(key) then
        -- (this night was decided before the world was reloaded: the same again)
        local v = engine.get_storage("aurora:tonight")
        if v and v ~= "" and engine.get_shared("aurora") ~= v then engine.set_shared("aurora", v) end
        return
    end
    if engine.set_storage then engine.set_storage("aurora:night", tostring(key)) end
    if math.random() < (options.chance or 0) then A.start() else share(false) end
end

-- In each player's game: show it when it's an aurora night, here, under a clear sky
local function player_pos()
    local x, y, z
    if engine.get_player_position then x, y, z = engine.get_player_position() end
    if type(x) ~= "number" then return nil end
    return x, y, z
end

local function hidden_by_weather()
    if not (weather and weather.get) then return false end
    local w = weather.get()
    for _, n in ipairs(options.hidden_by or {}) do if n == w then return true end end
    return false
end

function A.is_visible() return shown ~= nil and shown ~= false end

local function update_view()
    if is_server or not (sky and sky.set_aurora) then return end
    local want = false
    if state.active and not hidden_by_weather() then
        local x, y, z = player_pos()
        if x then
            local ok, here = pcall(options.where, x, y, z)
            if not ok then print("[Aurora] where() failed: " .. tostring(here)) end
            want = ok and here and true or false
        end
    end
    local key = want and string.format("%.2f;%d", state.strength, state.palette) or false
    if key == shown then return end
    shown = key
    if want then
        local pal = options.palettes[state.palette] or options.palettes[1]
        sky.set_aurora({ strength = state.strength, fade = options.fade, colors = { pal[1], pal[2] },
                         speed = options.speed, size = options.size, direction = options.direction,
                         style = options.style, pixels = options.pixels,
                         haze = options.haze, light = options.light })
    else
        sky.set_aurora({ strength = 0, fade = options.fade })
    end
end

-- Hooks (each keeps any other mod's version working)
local previous_shared = on_shared_changed
function on_shared_changed(key, value)
    if key == "aurora" then
        local was = state.active
        local s, p = value:match("^on;([%d%.]+);(%d+)")
        state.active = s ~= nil
        state.strength = tonumber(s) or 1
        state.palette = tonumber(p) or 1
        update_view()
        if was ~= state.active then
            for _, fn in ipairs(callbacks) do
                local ok, err = pcall(fn, state.active)
                if not ok then print("[Aurora] on_change callback failed: " .. tostring(err)) end
            end
        end
    end
    if previous_shared then return previous_shared(key, value) end
end

local check = 0
local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end
    check = check - dt
    if check > 0 then return end
    check = 2                                   -- (every 2 seconds is plenty)
    if authority() then decide() end
    update_view()                               -- (walking into or out of where it's seen)
end

-- /aurora [on [palette] | off]: see or change tonight's aurora
if chat and chat.register_command then
    chat.register_command("aurora", "/aurora [on [palette] | off]", "Show or change tonight's aurora")
end
local previous_command = on_chat_command
function on_chat_command(text, player, is_operator, reply)
    local args = {}
    for w in text:gmatch("%S+") do args[#args + 1] = w end
    if args[1] ~= "aurora" then
        if previous_command then return previous_command(text, player, is_operator, reply) end
        return false
    end
    if not authority() then return false end           -- joined: the server answers
    local say = reply or function(t) chat.send(t, 0.6, 1, 0.8) end
    if not args[2] then
        say("Aurora tonight: " .. (state.active and "yes" or "no") .. string.format(" (chance %d%% a night)", math.floor((options.chance or 0) * 100 + 0.5)))
        return true
    end
    if is_operator == false then say("Only operators can change the aurora"); return true end
    if args[2] == "on" then
        A.start(args[3])
        say("An aurora tonight" .. (night_key() and "" or " (it shows once it's dark)"))
    elseif args[2] == "off" then
        A.stop(); say("No aurora tonight")
    else
        say("/aurora [on [palette] | off]")
    end
    return true
end
