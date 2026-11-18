-- fog.lua: Minecraft-style distance fog for the base mod.
-- Loaded by init.lua. Restart the game after editing.

-- The world loads 3 chunks out (16 blocks each), so the loaded edge is 48 blocks.
-- Fog reaches full density just inside that edge, so chunks fade in instead of popping.
local RENDER_DISTANCE_CHUNKS = 12
local LOADED_EDGE = RENDER_DISTANCE_CHUNKS * 16

fog.set_enabled(true)
fog.set_exponential(false)   -- linear fog, like Minecraft's
fog.set_range(LOADED_EDGE * 0.55, LOADED_EDGE - 2)   -- haze from about 26 to 46 blocks
fog.set_rain_scale(0.4)

-- Fog color should match the sky near the horizon, or a seam shows.
fog.set_day_color(0.55, 0.78, 0.92)
fog.set_sunset_color(0.82, 0.48, 0.30)
fog.set_night_color(0.04, 0.06, 0.12)

-- Weather narrows the fog a little, the way Minecraft's rain and storms do.
local fogPresets = {
    clear  = { near = LOADED_EDGE * 0.55, far = LOADED_EDGE - 2 },
    cloudy = { near = LOADED_EDGE * 0.50, far = LOADED_EDGE - 2 },
    rain   = { near = LOADED_EDGE * 0.38, far = LOADED_EDGE - 6 },
    storm  = { near = LOADED_EDGE * 0.25, far = LOADED_EDGE - 14 },
}

local originalSetWeather = clouds.set_weather
clouds.set_weather = function(name, seconds)
    originalSetWeather(name, seconds)
    local p = fogPresets[name]
    if p then
        fog.set_range(p.near, p.far)
    end
end

-- Underwater look -----------------------------------------------------------

-- Seconds to fade into water, and to fade back out of it
underwater.set_fade(0.6, 0.4)

-- Colours: taken from the water block itself (its texture), so being underwater looks like the
-- water you see from above. get_water_color() below shifts them a little per biome.
underwater.set_visibility(25, 15)   -- blocks visible near the surface, and at depth
underwater.set_full_depth(40)      -- depth where colors and visibility reach their deep values

-- Biome-specific water colors. The engine calls this while the camera is
-- underwater or fading. Returns shallow r,g,b and deep r,g,b.
local function clamp01(x)
    return math.max(0, math.min(1, x))
end

local function smooth(e0, e1, x)
    local t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)
end

local function mix3(a, b, t)
    return {
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    }
end

-- Biome shifts: how far (0..1) the water colour is pulled toward each of these
local WATER_SWAMP = { shallow = {0.20, 0.36, 0.22}, deep = {0.05, 0.11, 0.07}, pull = 0.6 }  -- murky green
local WATER_WARM  = { shallow = {0.22, 0.62, 0.66}, deep = {0.04, 0.24, 0.33}, pull = 0.3 }  -- clear turquoise
local WATER_OCEAN = { shallow = {0.10, 0.30, 0.62}, deep = {0.01, 0.03, 0.15}, pull = 0.25 } -- deeper blue

-- The water block's own colour (from its texture), or a blue if the engine can't say
local function water_block_color()
    if underwater.get_water_color then
        local r, g, b = underwater.get_water_color()
        if r then return { r, g, b } end
    end
    return { 0.16, 0.38, 0.72 }
end

function get_water_color(x, z)
    local base = water_block_color()
    local s = base
    local d = { base[1] * 0.35, base[2] * 0.35, base[3] * 0.35 }   -- deep: the same colour, darker

    local ok, c, temp, hum = pcall(get_biome_parameters, x, z)
    if ok and c then
        local swamp = smooth(0.50, 0.60, hum) * smooth(0.30, 0.40, temp)
        local warm  = smooth(0.62, 0.70, temp) * (1 - smooth(0.30, 0.38, hum))
        local ocean = smooth(-0.05, -0.25, c)
        for _, b in ipairs({ { WATER_SWAMP, swamp }, { WATER_WARM, warm }, { WATER_OCEAN, ocean } }) do
            local t = b[2] * b[1].pull
            s = mix3(s, b[1].shallow, t)
            d = mix3(d, b[1].deep, t)
        end
    end

    return { s[1], s[2], s[3], d[1], d[2], d[3] }
end

print("[fog.lua] Fog range " .. string.format("%.0f", LOADED_EDGE * 0.55) .. " to " .. (LOADED_EDGE - 2) .. " blocks")