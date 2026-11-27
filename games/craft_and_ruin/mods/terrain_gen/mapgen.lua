-- mapgen.lua - Craft & Ruin's terrain generator
--
-- Generates layered, multi-noise terrain (continentalness, erosion, peaks, temperature,
-- humidity, rivers) evaluated on a 4-block grid and bilinearly interpolated. Features
-- are placed deterministically in world space so trees, flowers, ores, and underground
-- patches never clip along chunk borders.
--
-- Public Mod API:
--   get_biome(x, z)            -> biome name string
--   get_biome_parameters(x, z) -> continentalness, temperature, humidity, river
--   get_surface_height(x, z)   -> integer y of top solid block (sea level = 62)
--   find_volcano(x, z)         -> x, z, rim_y, radius (or nil)

local floor, abs, min, max = math.floor, math.abs, math.min, math.max
local sin, cos, sqrt, pi   = math.sin, math.cos, math.sqrt, math.pi
local ipairs, type         = ipairs, type

local world_seed = 1337
terrain_version  = 4

local SEA        = 62
local GRID       = 4
local VCELL      = 640
local TREE_REACH = 6
local MAX_TREES  = 0.065

engine.set_sun_texture("cw_sun.png")

local volcano_cache = {}
local grid_cache    = {}
local grid_cached   = 0

function set_world_seed(seed)
    if seed and seed ~= world_seed then
        world_seed    = seed
        volcano_cache = {}
        grid_cache    = {}
        grid_cached   = 0
    end
end

-- ---------------------------------------------------------------------------
-- Noise & Math Utilities
-- ---------------------------------------------------------------------------

function get_rand(x, z, seed_offset)
    local val = sin((x + world_seed * 0.1) * 12.9898 + (z + world_seed * 0.1) * 78.233 + seed_offset) * 43758.5453
    return val - floor(val)
end

function hash_rand(x, z, salt)
    local salt_offset = floor(salt * 100 + 0.5) * 0.618033988749895
    return get_rand(floor(x), floor(z), salt_offset)
end

local function fade(t)
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
end

local function lerp(t, a, b)
    return a + t * (b - a)
end

local function clamp01(v)
    return v < 0 and 0 or (v > 1 and 1 or v)
end

local function smoothstep(e0, e1, x)
    local t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)
end

local function smooth_noise2d(x, z, seed_offset)
    local X, Z = floor(x), floor(z)
    local u, v = fade(x - X), fade(z - Z)
    local n00  = get_rand(X, Z, seed_offset)
    local n10  = get_rand(X + 1, Z, seed_offset)
    local n01  = get_rand(X, Z + 1, seed_offset)
    local n11  = get_rand(X + 1, Z + 1, seed_offset)
    return lerp(v, lerp(u, n00, n10), lerp(u, n01, n11))
end

local function octave_noise2d(x, z, octaves, persistence, scale, seed_offset)
    local total, frequency, amplitude, max_val = 0, scale, 1, 0
    for i = 1, octaves do
        total     = total + smooth_noise2d(x * frequency, z * frequency, seed_offset + i * 1.5) * amplitude
        max_val   = max_val + amplitude
        amplitude = amplitude * persistence
        frequency = frequency * 2
    end
    return total / (max_val > 0 and max_val or 1)
end

local function curve(points, x)
    if x <= points[1][1] then return points[1][2] end
    for i = 2, #points do
        local a, b = points[i - 1], points[i]
        if x <= b[1] then
            local t = (x - a[1]) / (b[1] - a[1])
            t = t * t * (3 - 2 * t)
            return a[2] + (b[2] - a[2]) * t
        end
    end
    return points[#points][2]
end

-- ---------------------------------------------------------------------------
-- Terrain Shape & Climate
-- ---------------------------------------------------------------------------

function get_biome_parameters(world_x, world_z)
    local offset          = world_seed % 1000
    local wx, wz          = world_x + offset, world_z + offset
    local continentalness = octave_noise2d(wx, wz, 3, 0.5, 0.0011, 1.0) * 2.0 - 1.0
    local temperature     = octave_noise2d(wx, wz, 3, 0.5, 0.0007, 2.0)
    local humidity        = octave_noise2d(wx, wz, 3, 0.5, 0.0007, 4.0)
    local river           = abs(octave_noise2d(wx, wz, 2, 0.5, 0.0055, 3.0) * 2.0 - 1.0)
    return continentalness, temperature, humidity, river
end

local BASE_HEIGHT = {
    { -1.00, 18.0 }, { -0.60, 24.0 }, { -0.42, 34.0 }, { -0.28, 49.0 },
    { -0.18, 57.0 }, { -0.10, 61.0 }, { -0.03, 63.5 }, {  0.06, 65.0 },
    {  0.25, 69.0 }, {  0.55, 75.0 }, {  1.00, 82.0 },
}

local function grid_point(gx, gz)
    local key = gx * 1000003 + gz
    local g   = grid_cache[key]
    if g then return g end

    local world_x, world_z = gx * GRID, gz * GRID
    local offset           = world_seed % 1000
    local wx, wz           = world_x + offset, world_z + offset

    local c, temp, hum, river = get_biome_parameters(world_x, world_z)
    local biome_shape         = octave_noise2d(wx, wz, 2, 0.5, 0.0015, 20.0)

    temp = clamp01((temp - 0.5) * 1.6 + (biome_shape - 0.5) * 0.25 + 0.5)
    hum  = clamp01((hum  - 0.5) * 1.6 + (biome_shape - 0.5) * 0.25 + 0.5)

    local erosion  = octave_noise2d(wx, wz, 2, 0.5, 0.0024, 7.0)
    local inland   = smoothstep(-0.12, 0.10, c)
    local mountain = smoothstep(0.42, 0.30, erosion) * inland
    local hills    = smoothstep(0.55, 0.40, erosion) * inland

    local bad = smoothstep(0.62, 0.70, temp)
              * smoothstep(0.40, 0.32, hum)
              * smoothstep(0.46, 0.54, octave_noise2d(wx, wz, 2, 0.5, 0.004, 11.0))
              * inland

    local swamp_noise = octave_noise2d(wx, wz, 2, 0.5, 0.0015, 15.0)
    local swamp = smoothstep(0.58, 0.68, hum)
                * smoothstep(0.55, 0.75, swamp_noise)
                * smoothstep(0.15, -0.05, c)
                * (1 - mountain)
                * (1 - hills)
    swamp = swamp * (0.7 + smoothstep(0.15, 0.03, river) * 0.6)

    local height  = curve(BASE_HEIGHT, c)
    local rolling = 3 + inland * 4
    height = height
           + (octave_noise2d(wx, wz, 4, 0.5, 0.008, 5.0) * 2.0 - 1.0) * rolling
           + (octave_noise2d(wx, wz, 2, 0.5, 0.030, 6.0) * 2.0 - 1.0) * (0.5 + inland * 0.9)

    if hills > 0 then
        height = height + hills * (8 + (octave_noise2d(wx, wz, 3, 0.5, 0.011, 8.0) * 2.0 - 1.0) * 13.0)
    end

    local peaks = 0
    if mountain > 0 then
        local ridge = 1 - abs(octave_noise2d(wx, wz, 4, 0.5, 0.0055, 9.0) * 2.0 - 1.0)
        local crag  = (octave_noise2d(wx, wz, 2, 0.5, 0.07, 10.0) * 2.0 - 1.0) * 3.0
        peaks       = smoothstep(0.35, 0.65, octave_noise2d(wx, wz, 2, 0.5, 0.0038, 13.0))
        height      = height + mountain * (16 + ridge * ridge * (50 + peaks * 70) + crag * (1 + peaks))
    end

    local plateau = bad > 0 and smoothstep(0.42, 0.58, octave_noise2d(wx, wz, 3, 0.5, 0.011, 12.0)) or 0

    g = {
        c = c, temp = temp, hum = hum, river = river, inland = inland,
        mountain = mountain, hills = hills, bad = bad, swamp = swamp,
        height = height, plateau = plateau, peaks = peaks,
    }

    if grid_cached > 60000 then
        grid_cache  = {}
        grid_cached = 0
    end
    grid_cache[key] = g
    grid_cached     = grid_cached + 1
    return g
end

local SHAPE_FIELDS = {
    "c", "temp", "hum", "river", "inland", "mountain",
    "hills", "bad", "swamp", "height", "plateau", "peaks",
}

local function base_column(world_x, world_z)
    local gx, gz   = floor(world_x / GRID), floor(world_z / GRID)
    local fx, fz   = (world_x - gx * GRID) / GRID, (world_z - gz * GRID) / GRID
    local g00, g10 = grid_point(gx, gz), grid_point(gx + 1, gz)
    local g01, g11 = grid_point(gx, gz + 1), grid_point(gx + 1, gz + 1)

    local v = {}
    for _, k in ipairs(SHAPE_FIELDS) do
        v[k] = lerp(fz, lerp(fx, g00[k], g10[k]), lerp(fx, g01[k], g11[k]))
    end

    local height = v.height
    if v.bad > 0 then
        local raised   = SEA + 4 + v.plateau * 42 + (height - SEA) * 0.3
        local step     = 6
        local t        = raised / step
        local terraced = (floor(t) + smoothstep(0.86, 1.0, t - floor(t))) * step
        height         = lerp(v.bad, height, terraced)
    end

    if v.swamp > 0 then
        height = lerp(v.swamp, height, SEA - 0.4 + (height - SEA) * 0.12)
    end

    local is_river = false
    if v.c > -0.08 and v.river < 0.07 and v.mountain < 0.6 and v.bad < 0.5 then
        local f    = (0.07 - v.river) / 0.07
        local bank = lerp(smoothstep(0, 0.6, f), height, min(height, SEA + 1))
        local bed  = SEA - 1 - clamp01((f - 0.35) / 0.65) * 3
        height     = (f > 0.35) and lerp(clamp01((f - 0.35) / 0.3), bank, bed) or bank
        if height < SEA - 3 then height = SEA - 3 end
        is_river = height < SEA
    end

    local y     = min(floor(height), 240)
    local t_eff = v.temp - max(0, y - 88) * 0.0045

    return {
        y = y, c = v.c, temp = v.temp, t_eff = t_eff, hum = v.hum,
        mountain = v.mountain, hills = v.hills, bad = v.bad, swamp = v.swamp,
        river = is_river, inland = v.inland, peaks = v.peaks,
        volcanic = 0, vd = 99, lava_to = nil, height = height,
    }
end

-- ---------------------------------------------------------------------------
-- Volcanoes & Basalt Hills
-- ---------------------------------------------------------------------------

local function volcano_of(ci, cj)
    local key = ci * 100003 + cj
    local v   = volcano_cache[key]
    if v ~= nil then return v or nil end

    v = false
    if hash_rand(ci, cj, 50.1) < 0.5 then
        local cx = floor(ci * VCELL + 120 + hash_rand(ci, cj, 50.3) * (VCELL - 240))
        local cz = floor(cj * VCELL + 120 + hash_rand(ci, cj, 50.7) * (VCELL - 240))
        local b  = base_column(cx, cz)
        local ok = b.c > 0.0 and b.mountain < 0.1 and b.bad < 0.1 and b.swamp < 0.3
                   and not b.river and b.y >= SEA + 2 and b.y <= SEA + 30

        if ok then
            for k = 0, 7 do
                local a = k * pi / 4
                local n = base_column(floor(cx + cos(a) * 70), floor(cz + sin(a) * 70))
                if n.bad > 0.1 or n.mountain > 0.15 or n.y < SEA then
                    ok = false
                    break
                end
            end
        end

        if ok then
            local ang = hash_rand(ci, cj, 51.1) * pi * 2
            local R   = 24 + floor(hash_rand(ci, cj, 51.5) * 10)
            local H   = 34 + floor(hash_rand(ci, cj, 51.9) * 12)
            local rc  = R * 0.32
            local cap = floor(H * (1 - rc / R) ^ 0.8)
            v = {
                x = cx, z = cz, R = R, H = H, rc = rc, base = b.y, cap = cap,
                dx = cos(ang), dz = sin(ang), lava = b.y + cap - 4,
            }
        end
    end

    volcano_cache[key] = v
    return v or nil
end

local function near_volcano(x, z)
    local ci, cj = floor(x / VCELL), floor(z / VCELL)
    local best, best_d
    for i = ci - 1, ci + 1 do
        for j = cj - 1, cj + 1 do
            local v = volcano_of(i, j)
            if v then
                local d = sqrt((x - v.x) ^ 2 + (z - v.z) ^ 2) / v.R
                if d < 1.7 and (not best_d or d < best_d) then
                    best, best_d = v, d
                end
            end
        end
    end
    return best, best_d
end

function find_volcano(x, z)
    local ci, cj = floor(x / VCELL), floor(z / VCELL)
    local best, best_d
    for i = ci - 2, ci + 2 do
        for j = cj - 2, cj + 2 do
            local v = volcano_of(i, j)
            if v then
                local d = (x - v.x) ^ 2 + (z - v.z) ^ 2
                if not best_d or d < best_d then
                    best, best_d = v, d
                end
            end
        end
    end
    if not best then return nil end
    return best.x, best.z, best.base + best.cap, best.R
end

local function column(x, z)
    local col  = base_column(x, z)
    local v, d = near_volcano(x, z)
    if not v then return col end

    local around = smoothstep(1.7, 1.0, d)
    local h      = lerp(around, col.height, v.base + (col.height - v.base) * 0.35)

    local cone = d < 1 and (v.H * (1 - d) ^ 0.8) or 0
    if d < 1.45 then
        cone = cone + 3 * smoothstep(1.45, 0.9, d)
    end

    local rough = hash_rand(x, z, 52.3)
    local cap   = v.cap + ((rough < 0.3) and floor(hash_rand(x, z, 52.7) * 5) or 0)
    local r     = d * v.R
    if cone > cap then cone = cap end
    h = h + cone

    if r < v.rc then
        h = v.base + v.cap - 9 * (1 - (r / v.rc) ^ 2) - 1
    end

    local px, pz = x - v.x, z - v.z
    local along  = px * v.dx + pz * v.dz
    local across = px * v.dz - pz * v.dx + sin(along * 0.13) * 2.5
    local width  = 2.2 + hash_rand(v.x, v.z, 53.1) * 1.2
    local lava_to, flow = nil, false

    if r < v.rc then
        if h < v.lava then lava_to = v.lava end
    elseif along > 0 and along < v.R * 1.3 and abs(across) < width then
        local bed = floor(h) - 2
        if r < v.rc + 3 then bed = min(bed, v.lava - 1) end
        h       = bed
        lava_to = bed + 1
        flow    = true
    end

    col.height   = h
    col.y        = min(floor(h), 240)
    col.volcanic = 1
    col.vd       = d
    col.flow     = flow
    col.river    = false
    col.swamp    = 0
    if lava_to and lava_to > col.y then col.lava_to = lava_to end

    return col
end

function get_surface_height(x, z) return column(x, z).y end
function get_terrain_height(x, z) return column(x, z).y end

-- ---------------------------------------------------------------------------
-- Biome Resolution
-- ---------------------------------------------------------------------------

local function biome_of(col, x, z)
    local y = col.y
    if y < SEA then
        local frozen = col.t_eff < 0.27
        if col.river then return frozen and "frozen_river" or "river" end
        if frozen then return "frozen_ocean" end
        if SEA - y > 24 then return "deep_ocean" end
        if col.temp > 0.62 then return "warm_ocean" end
        if col.temp < 0.38 then return "cold_ocean" end
        return "ocean"
    end

    if col.vd < 1.6 then return "basalt_hills" end
    if col.c < -0.04 and y <= SEA + 1 and col.mountain < 0.2 and col.bad < 0.4 then return "beach" end
    if col.mountain > 0.55 and y > 92 then return "mountains" end
    if col.bad > 0.4 then return "badlands" end
    if col.swamp > 0.5 then return "swamp" end

    local t, h = col.t_eff, col.hum
    if col.mountain > 0.3 and y > 82 then
        return (t < 0.30) and "snowy_grove" or "meadow"
    end
    if col.hills > 0.7 and y > 80 and t < 0.56 then
        return "windswept_hills"
    end

    if t < 0.29 then
        return (h > 0.45) and "snowy_taiga" or "snowy_plains"
    elseif t < 0.40 then
        return (h > 0.40) and "taiga" or "plains"
    elseif t > 0.60 then
        if h < 0.45 then return "desert" end
        if h > 0.56 then return "jungle" end
        return "savanna"
    elseif t > 0.54 and h < 0.48 then
        return "savanna"
    else
        if h > 0.46 then
            if h > 0.64 and t < 0.54 then return "dark_forest" end
            local patch = octave_noise2d(x, z, 1, 0.5, 0.0035, 31.0)
            if patch > 0.62 then return "birch_forest" end
            if patch < 0.17 and h < 0.62 then return "flower_forest" end
            return "forest"
        end
        return "plains"
    end
end

function get_biome(x, z)
    return biome_of(column(x, z), x, z)
end

-- ---------------------------------------------------------------------------
-- Block ID Registry
-- ---------------------------------------------------------------------------

local B
local function ids()
    if B then return B end

    local function id(name, fallback)
        local v = engine.get_block_id(name) or 0
        if v == 0 and fallback then
            v = engine.get_block_id(fallback) or 0
        end
        return v
    end

    B = {
        stone          = id("stone"),
        bedrock        = id("bedrock", "stone"),
        grass          = id("grass"),
        dirt           = id("dirt"),
        sand           = id("sand"),
        gravel         = id("gravel", "stone"),
        water          = id("water"),
        clay           = id("clay", "dirt"),
        snow           = id("snow_block", "stone"),
        snowy_grass    = id("snowy_grass", "grass"),
        ice            = id("ice", "water"),
        red_sand       = id("red_sand", "sand"),
        coarse         = id("coarse_dirt", "dirt"),
        andesite       = id("andesite", "stone"),
        dead_bush      = id("dead_bush"),
        cactus         = id("cactus"),
        short_grass    = id("short_grass"),
        lily           = id("lily_pad"),
        reed           = id("reed"),
        reed_top       = id("reed_top"),
        cattail        = id("cattail"),
        cattail_top    = id("cattail_top"),
        log_oak        = id("log_oak"),
        leaves_oak     = id("leaves_oak"),
        log_birch      = id("log_birch", "log_oak"),
        leaves_birch   = id("leaves_birch", "leaves_oak"),
        log_spruce     = id("log_spruce", "log_oak"),
        leaves_spruce  = id("leaves_spruce", "leaves_oak"),
        log_jungle     = id("log_jungle", "log_oak"),
        leaves_jungle  = id("leaves_jungle", "leaves_oak"),
        log_acacia     = id("log_acacia", "log_oak"),
        leaves_acacia  = id("leaves_acacia", "leaves_oak"),
        mushroom_red   = id("mushroom_red"),
        mushroom_brown = id("mushroom_brown"),
        basalt         = id("basalt", "stone"),
        obsidian       = id("obsidian", "stone"),
        magma          = id("magma_block", "stone"),
        ash            = id("volcanic_ash", "gravel"),
        lava           = id("lava", "magma_block"),
        ore_coal       = id("ore_coal"),
        ore_iron       = id("ore_iron"),
        ore_gold       = id("ore_gold"),
        ore_diamond    = id("ore_diamond"),
        ore_copper     = id("ore_copper"),
        ore_emerald    = id("ore_emerald"),
        flowers        = {},
        terracotta     = {},
    }

    B.soft = { [0] = true }
    for _, k in ipairs({
        "short_grass", "dead_bush", "leaves_oak", "leaves_birch",
        "leaves_spruce", "leaves_jungle", "leaves_acacia",
        "mushroom_red", "mushroom_brown",
    }) do
        if B[k] ~= 0 then B.soft[B[k]] = true end
    end

    B.flower_by_name = {}
    local flower_defs = {
        { "poppy", 4 }, { "flower_poppy", 4 }, { "flower_daisy", 4 },
        { "flower_azure_bluet", 2 }, { "flower_tulip_red", 1 },
        { "flower_tulip_pink", 1 }, { "flower_tulip_white", 1 },
        { "flower_bluebell", 1 }, { "flower_allium", 0.5 },
    }
    local total_weight = 0
    for _, f in ipairs(flower_defs) do
        local v = engine.get_block_id(f[1]) or 0
        if v ~= 0 and not B.flower_by_name[f[1]] then
            B.flower_by_name[f[1]] = v
            B.flowers[#B.flowers + 1] = { id = v, weight = f[2] }
            B.soft[v] = true
            total_weight = total_weight + f[2]
        end
    end
    B.flower_weight = total_weight

    local terra_names = {
        "terracotta", "terracotta_orange", "terracotta_orange", "terracotta_yellow",
        "terracotta", "terracotta_white", "terracotta_light_gray", "terracotta_red",
        "terracotta", "terracotta_brown", "terracotta_orange", "terracotta",
    }
    for _, n in ipairs(terra_names) do
        local v = engine.get_block_id(n) or 0
        B.terracotta[#B.terracotta + 1] = (v ~= 0) and v or B.red_sand
    end

    return B
end

-- ---------------------------------------------------------------------------
-- Trees & Foliage Placement
-- ---------------------------------------------------------------------------

local cur_x0, cur_z0 = 0, 0
local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

local function inside(x, y, z)
    return x >= 0 and x <= 15 and z >= 0 and z <= 15 and y >= 1 and y <= 255
end

local function put(chunk, x, y, z, id)
    if not inside(x, y, z) then return false end
    if chunk:get_block(x, y, z) == 0 then
        chunk:set_block(x, y, z, id)
        return true
    end
    return false
end

local function log(chunk, x, y, z, id)
    if not inside(x, y, z) then return end
    if B.soft[chunk:get_block(x, y, z)] then
        chunk:set_block(x, y, z, id)
    end
end

local function dice(x, y, z)
    return hash_rand(cur_x0 + x, cur_z0 + z, y * 0.37 + 5.1)
end

local function disc(chunk, cx, y, cz, r, id, corners)
    for dx = -r, r do
        for dz = -r, r do
            local corner = abs(dx) == r and abs(dz) == r
            if not (corner and (corners == 0 or (corners and dice(cx + dx, y, cz + dz) < corners))) then
                put(chunk, cx + dx, y, cz + dz, id)
            end
        end
    end
end

local function blob(chunk, cx, cy, cz, rx, ry, id)
    for dy = -ry, ry do
        for dx = -rx, rx do
            for dz = -rx, rx do
                local d = (dx * dx + dz * dz) / (rx * rx + 0.5) + (dy * dy) / (ry * ry + 0.5)
                if d < 1.0 and (d < 0.7 or dice(cx + dx, cy + dy, cz + dz) < 0.65) then
                    put(chunk, cx + dx, cy + dy, cz + dz, id)
                end
            end
        end
    end
end

local function tree_round(chunk, x, y, z, wx, wz, log_id, leaf_id, tmin, tmax)
    local h = tmin + floor(hash_rand(wx, wz, 21.3) * (tmax - tmin + 1))
    disc(chunk, x, y + h - 3, z, 2, leaf_id, 0.5)
    disc(chunk, x, y + h - 2, z, 2, leaf_id, 0.5)
    disc(chunk, x, y + h - 1, z, 1, leaf_id, 0.35)
    disc(chunk, x, y + h,     z, 1, leaf_id, 0)
    for i = 0, h - 1 do
        log(chunk, x, y + i, z, log_id)
    end
end

local function tree_big(chunk, x, y, z, wx, wz, log_id, leaf_id)
    local h = 8 + floor(hash_rand(wx, wz, 21.9) * 4)
    for i = 0, h - 1 do
        log(chunk, x, y + i, z, log_id)
    end
    blob(chunk, x, y + h - 1, z, 4, 3, leaf_id)

    for k = 0, 2 do
        local a      = hash_rand(wx, wz, 22.1 + k) * pi * 2
        local len    = 2 + floor(hash_rand(wx, wz, 22.5 + k) * 2)
        local by     = y + floor(h * (0.5 + 0.12 * k))
        local bx, bz = x + floor(cos(a) * len + 0.5), z + floor(sin(a) * len + 0.5)
        log(chunk, x + floor(cos(a) + 0.5), by, z + floor(sin(a) + 0.5), log_id)
        log(chunk, bx, by + 1, bz, log_id)
        blob(chunk, bx, by + 2, bz, 2, 2, leaf_id)
    end
end

local function tree_dark(chunk, x, y, z, wx, wz, b)
    local h = 6 + floor(hash_rand(wx, wz, 28.3) * 3)
    for i = 0, h - 1 do
        for _, d in ipairs({ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }) do
            log(chunk, x + d[1], y + i, z + d[2], b.log_oak)
        end
    end
    disc(chunk, x,     y + h - 2, z,     3, b.leaves_oak, 0.6)
    disc(chunk, x + 1, y + h - 2, z + 1, 3, b.leaves_oak, 0.6)
    disc(chunk, x,     y + h - 1, z,     4, b.leaves_oak, 0.7)
    disc(chunk, x + 1, y + h - 1, z + 1, 4, b.leaves_oak, 0.7)
    disc(chunk, x,     y + h,     z,     3, b.leaves_oak, 0.5)
    disc(chunk, x + 1, y + h,     z + 1, 3, b.leaves_oak, 0.5)
    disc(chunk, x,     y + h + 1, z,     2, b.leaves_oak, 0.3)
    disc(chunk, x + 1, y + h + 1, z + 1, 2, b.leaves_oak, 0.3)
end

local function tree_spruce(chunk, x, y, z, wx, wz, b)
    local h = 7 + floor(hash_rand(wx, wz, 22.7) * 5)
    for i = 0, h - 1 do
        log(chunk, x, y + i, z, b.log_spruce)
    end

    local widest  = 2 + ((hash_rand(wx, wz, 23.1) < 0.4) and 1 or 0)
    local r, grow = 0, true
    for ly = y + h, y + 2, -1 do
        disc(chunk, x, ly, z, r, b.leaves_spruce, 0)
        if grow then
            r = r + 1
            if r >= widest then grow = false end
        else
            r = (r > 1) and r - 1 or 1
            if r == 1 then
                grow   = true
                widest = max(2, widest - ((hash_rand(wx, wz, 23.5 + ly) < 0.3) and 1 or 0))
            end
        end
    end
    put(chunk, x, y + h + 1, z, b.leaves_spruce)
end

local function tree_jungle(chunk, x, y, z, wx, wz, b)
    local h = 9 + floor(hash_rand(wx, wz, 24.3) * 6)
    for i = 0, h - 1 do
        log(chunk, x, y + i, z, b.log_jungle)
    end
    disc(chunk, x, y + h - 2, z, 3, b.leaves_jungle, 0.5)
    disc(chunk, x, y + h - 1, z, 3, b.leaves_jungle, 0.5)
    disc(chunk, x, y + h,     z, 2, b.leaves_jungle, 0.4)
    disc(chunk, x, y + h + 1, z, 1, b.leaves_jungle, 0)

    for k = 0, 1 do
        if hash_rand(wx, wz, 24.9 + k) < 0.6 then
            local by  = y + floor(h * (0.45 + 0.2 * k))
            local dxz = DIRS[1 + floor(hash_rand(wx, wz, 25.3 + k) * 4)]
            log(chunk, x + dxz[1], by, z + dxz[2], b.log_jungle)
            disc(chunk, x + dxz[1] * 2, by + 1, z + dxz[2] * 2, 1, b.leaves_jungle, 0)
            put(chunk, x + dxz[1] * 2, by + 2, z + dxz[2] * 2, b.leaves_jungle)
        end
    end
end

local function bush(chunk, x, y, z, log_id, leaf_id)
    log(chunk, x, y, z, log_id)
    disc(chunk, x, y,     z, 2, leaf_id, 0.5)
    disc(chunk, x, y + 1, z, 1, leaf_id, 0.3)
end

local function tree_acacia(chunk, x, y, z, wx, wz, b)
    local d        = DIRS[1 + floor(hash_rand(wx, wz, 26.1) * 4)]
    local straight = 3 + floor(hash_rand(wx, wz, 26.7) * 2)
    local bend     = 2 + floor(hash_rand(wx, wz, 27.3) * 2)

    for i = 0, straight - 1 do
        log(chunk, x, y + i, z, b.log_acacia)
    end

    local tx, ty, tz = x, y + straight, z
    for _ = 1, bend do
        tx, tz = tx + d[1], tz + d[2]
        log(chunk, tx, ty, tz, b.log_acacia)
        ty = ty + 1
    end
    log(chunk, tx, ty, tz, b.log_acacia)
    disc(chunk, tx, ty + 1, tz, 3, b.leaves_acacia, 0.6)
    disc(chunk, tx, ty + 2, tz, 2, b.leaves_acacia, 0.5)

    if hash_rand(wx, wz, 27.9) < 0.4 then
        local ox, oz = x - d[1] * 2, z - d[2] * 2
        log(chunk, x - d[1], y + straight,     z - d[2], b.log_acacia)
        log(chunk, ox,       y + straight + 1, oz,       b.log_acacia)
        disc(chunk, ox, y + straight + 2, oz, 2, b.leaves_acacia, 0.5)
    end
end

-- ---------------------------------------------------------------------------
-- Flower Patches
-- ---------------------------------------------------------------------------

local FLOWER_CELL = 16

local function pick_flower(b, roll)
    local t = roll * b.flower_weight
    for _, f in ipairs(b.flowers) do
        t = t - f.weight
        if t < 0 then return f.id end
    end
    return b.flowers[#b.flowers].id
end

local function flower_at(wx, wz, def, b)
    if (def.flowers or 0) <= 0 or #b.flowers == 0 then return nil end

    local grow  = def.big or 1
    local reach = floor(4 * grow + 0.999)
    local c0x, c1x = floor((wx - reach) / FLOWER_CELL), floor((wx + reach) / FLOWER_CELL)
    local c0z, c1z = floor((wz - reach) / FLOWER_CELL), floor((wz + reach) / FLOWER_CELL)

    for ci = c0x, c1x do
        for cj = c0z, c1z do
            if hash_rand(ci, cj, 61.1) < def.flowers then
                local px     = ci * FLOWER_CELL + 4 + hash_rand(ci, cj, 61.3) * (FLOWER_CELL - 8)
                local pz     = cj * FLOWER_CELL + 4 + hash_rand(ci, cj, 61.5) * (FLOWER_CELL - 8)
                local radius = (2 + hash_rand(ci, cj, 61.9) * 2) * grow
                local dx, dz = wx - px, wz - pz
                local d      = sqrt(dx * dx + dz * dz)

                if d < radius and hash_rand(wx, wz, 62.3) < 0.45 * (1 - d / radius) + 0.08 then
                    if def.flower and b.flower_by_name[def.flower] then
                        return b.flower_by_name[def.flower]
                    end
                    return pick_flower(b, hash_rand(ci, cj, 61.7))
                end
            end
        end
    end

    if hash_rand(wx, wz, 62.9) < 0.0015 * def.flowers / 0.3 then
        return pick_flower(b, hash_rand(wx, wz, 63.1))
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- Biome Vegetation & Surface Definitions
-- ---------------------------------------------------------------------------

local BIOMES = {
    plains          = { trees = 0.0012, cover = 0.300, flowers = 0.300 },
    forest          = { trees = 0.0300, cover = 0.220, flowers = 0.220 },
    birch_forest    = { trees = 0.0300, cover = 0.220, flowers = 0.220 },
    dark_forest     = { trees = 0.0360, cover = 0.100, flowers = 0.000 },
    flower_forest   = { trees = 0.0140, cover = 0.300, flowers = 0.850, big = 1.5 },
    taiga           = { trees = 0.0300, cover = 0.200, flowers = 0.000 },
    snowy_taiga     = { trees = 0.0160, cover = 0.000, flowers = 0.000 },
    snowy_plains    = { trees = 0.0008, cover = 0.000, flowers = 0.000 },
    snowy_grove     = { trees = 0.0200, cover = 0.000, flowers = 0.000 },
    meadow          = { trees = 0.0012, cover = 0.350, flowers = 0.800, big = 1.6 },
    windswept_hills = { trees = 0.0030, cover = 0.150, flowers = 0.040 },
    savanna         = { trees = 0.0035, cover = 0.450, flowers = 0.060 },
    jungle          = { trees = 0.0600, cover = 0.450, flowers = 0.080 },
    swamp           = { trees = 0.0240, cover = 0.180, flowers = 0.035 },
    desert          = { trees = 0.0000, cover = 0.012, flowers = 0.000 },
    badlands        = { trees = 0.0000, cover = 0.010, flowers = 0.000 },
    mountains       = { trees = 0.0040, cover = 0.100, flowers = 0.060 },
    basalt_hills    = { trees = 0.0060, cover = 0.120, flowers = 0.020 },
    beach           = { trees = 0, cover = 0, flowers = 0 },
    ocean           = { trees = 0, cover = 0, flowers = 0 },
    deep_ocean      = { trees = 0, cover = 0, flowers = 0 },
    cold_ocean      = { trees = 0, cover = 0, flowers = 0 },
    warm_ocean      = { trees = 0, cover = 0, flowers = 0 },
    frozen_ocean    = { trees = 0, cover = 0, flowers = 0 },
    river           = { trees = 0, cover = 0, flowers = 0 },
    frozen_river    = { trees = 0, cover = 0, flowers = 0 },
}

local function band(b, y, wx, wz)
    local shift = floor(octave_noise2d(wx, wz, 1, 0.5, 0.02, 41.0) * 4)
    local n     = #b.terracotta
    return b.terracotta[((y + shift) % n) + 1]
end

local function ground(b, biome, col, wx, wz, slope)
    local y      = col.y
    local frozen = col.t_eff < 0.27
    local top, under, depth = b.grass, b.dirt, 3

    if biome == "desert" then
        top, under, depth = b.sand, b.sand, 5
    elseif biome == "beach" then
        top, under, depth = b.sand, b.sand, 3
        if slope >= 3 then
            top, under = b.stone, b.stone
        elseif frozen then
            top, under = b.snowy_grass, b.dirt
        end
    elseif biome == "badlands" then
        top, under, depth = b.red_sand, b.red_sand, 2
    elseif biome == "snowy_plains" or biome == "snowy_taiga" or biome == "snowy_grove" then
        top = b.snowy_grass
    elseif biome == "savanna" and octave_noise2d(wx, wz, 1, 0.5, 0.12, 45.0) > 0.88 then
        top = b.coarse
    elseif biome == "windswept_hills" then
        local patch = octave_noise2d(wx, wz, 2, 0.5, 0.07, 48.0)
        if y > 108 or patch > 0.64 then
            top, under = b.stone, b.stone
        elseif patch > 0.5 then
            top, under = b.gravel, b.gravel
        elseif patch < 0.3 then
            top = b.coarse
        end
    elseif biome == "mountains" then
        local snowline = 128 - (0.5 - col.temp) * 40
        local rocky    = octave_noise2d(wx, wz, 2, 0.5, 0.08, 43.0)
        if y >= snowline then
            top, under = b.snow, b.snow
        elseif y > 104 or rocky > 0.62 or slope >= 4 then
            top   = (rocky > 0.5) and b.stone or b.andesite
            under = b.stone
            if rocky < 0.32 and slope < 4 then top = b.gravel end
        elseif frozen then
            top = b.snowy_grass
        end
    elseif biome == "basalt_hills" then
        local patch = octave_noise2d(wx, wz, 2, 0.5, 0.09, 46.0)
        if col.vd < 1.0 then
            top, under, depth = b.basalt, b.basalt, 9
            if patch > 0.66 then
                top = b.stone
            elseif patch < 0.30 then
                top = b.andesite
            end
            if col.vd > 0.55 and octave_noise2d(wx, wz, 2, 0.5, 0.11, 47.0) > 0.6 then
                top = b.grass
            end
        else
            top, under, depth = b.grass, b.dirt, 3
            if patch > 0.66 then
                top = b.basalt
            elseif patch < 0.26 then
                top = b.coarse
            elseif patch < 0.31 then
                top = b.gravel
            end
        end
        if col.lava_to then top = b.magma end
    elseif (biome == "taiga" or biome == "plains") and frozen then
        top = b.snowy_grass
    elseif biome == "meadow" and slope >= 5 then
        top = b.stone
    end

    if top == b.grass
       and (biome == "forest" or biome == "dark_forest" or biome == "plains")
       and octave_noise2d(wx, wz, 1, 0.5, 0.09, 49.0) > 0.86 then
        top = b.coarse
    end

    return top, under, depth
end

local function water_bed(b, biome, col, wx, wz)
    local bed    = octave_noise2d(wx * 0.05, wz * 0.05, 2, 0.5, 1.0, 99.0)
    local wdepth = SEA - col.y

    if col.swamp > 0.5 then
        return (bed < 0.6) and b.dirt or ((bed < 0.85) and b.gravel or b.clay), b.dirt
    elseif biome == "river" or biome == "frozen_river" then
        return (bed < 0.55) and b.sand or ((bed < 0.8) and b.gravel or b.clay), b.sand
    elseif biome == "warm_ocean" then
        return (bed < 0.88) and b.sand or b.clay, b.sand
    elseif biome == "frozen_ocean" or biome == "cold_ocean" then
        local top = (bed < 0.55) and b.gravel or ((bed < 0.8) and b.sand or b.clay)
        if wdepth > 10 and bed > 0.6 then top = b.stone end
        return top, (top == b.stone) and b.stone or b.gravel
    elseif wdepth <= 3 then
        return b.sand, b.sand
    elseif biome == "deep_ocean" then
        local top = (bed < 0.5) and b.gravel or ((bed < 0.8) and b.stone or b.clay)
        return top, b.stone
    end

    local top = (bed < 0.45) and b.gravel or ((bed < 0.75) and b.sand or b.stone)
    return top, (top == b.stone) and b.stone or b.dirt
end

-- ---------------------------------------------------------------------------
-- Underground Rock & Ore Patches
-- ---------------------------------------------------------------------------

local MOUNTAIN_BIOMES = {
    mountains = true, snowy_grove = true, meadow = true, windswept_hills = true,
}

-- { block_key, attempts_per_chunk, base_radius, min_y, max_y, biome_filter (optional) }
local PATCHES = {
    { "dirt",        7, 3.4,  4, 120 },
    { "gravel",      6, 3.4,  2,  90 },
    { "andesite",    9, 4.2,  2, 110 },
    { "ore_coal",   14, 2.4,  5, 120 },
    { "ore_copper", 10, 2.1, 20, 112 },
    { "ore_iron",   10, 1.9,  4, 100 },
    { "ore_gold",    3, 1.7,  2,  32 },
    { "ore_gold",    6, 1.9, 32,  80, "badlands" },
    { "ore_diamond", 2, 1.3,  1,  16 },
    { "ore_emerald", 2, 0.9,  4, 100, MOUNTAIN_BIOMES },
}

local function stone_patches(chunk, chunk_x, chunk_z, b)
    local x0, z0 = chunk_x * 16, chunk_z * 16
    for sx = chunk_x - 1, chunk_x + 1 do
        for sz = chunk_z - 1, chunk_z + 1 do
            for kind, p in ipairs(PATCHES) do
                local id = b[p[1]]
                if id and id ~= 0 then
                    local filter = p[6]
                    for k = 1, p[2] do
                        local salt = kind * 7.0 + k * 0.13
                        local px   = sx * 16 + floor(hash_rand(sx * 31 + k, sz * 17 + kind, 80.1 + salt) * 16)
                        local pz   = sz * 16 + floor(hash_rand(sx * 17 + kind, sz * 31 + k, 80.3 + salt) * 16)
                        local py   = p[4] + floor(hash_rand(sx * 13 + k, sz * 29 + kind, 80.7 + salt) * (p[5] - p[4]))
                        local r    = p[3] * (0.6 + hash_rand(sx * 7 + k, sz * 11 + kind, 80.9 + salt) * 0.7)
                        local ri   = floor(r) + 1

                        if px + ri >= x0 and px - ri <= x0 + 15 and pz + ri >= z0 and pz - ri <= z0 + 15 then
                            local allowed = true
                            if filter then
                                local biome = get_biome(px, pz)
                                if type(filter) == "table" then
                                    allowed = filter[biome] == true
                                else
                                    allowed = biome == filter
                                end
                            end

                            if allowed then
                                local r2  = r * r
                                local ry2 = r2 * 0.5
                                for dx = -ri, ri do
                                    local lx = px + dx - x0
                                    if lx >= 0 and lx <= 15 then
                                        for dz = -ri, ri do
                                            local lz = pz + dz - z0
                                            if lz >= 0 and lz <= 15 then
                                                local xz_dist = (dx * dx + dz * dz) / r2
                                                for dy = -ri, ri do
                                                    if xz_dist + (dy * dy) / ry2 <= 1 then
                                                        local y = py + dy
                                                        if y >= 1 and y <= 250 and chunk:get_block(lx, y, lz) == b.stone then
                                                            chunk:set_block(lx, y, lz, id)
                                                        end
                                                    end
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Main Chunk Generation
-- ---------------------------------------------------------------------------

function generate_chunk(chunk, chunk_x, chunk_z)
    local b      = ids()
    local x0, z0 = chunk_x * 16, chunk_z * 16
    cur_x0, cur_z0 = x0, z0

    local cols = {}
    for lx = -1, 16 do
        cols[lx] = {}
        for lz = -1, 16 do
            cols[lx][lz] = column(x0 + lx, z0 + lz)
        end
    end

    for lx = 0, 15 do
        for lz = 0, 15 do
            local wx, wz = x0 + lx, z0 + lz
            local col    = cols[lx][lz]
            local y      = col.y
            local biome  = biome_of(col, wx, wz)
            local water  = y < SEA
            local frozen = col.t_eff < 0.27
            local slope  = max(
                abs(cols[lx - 1][lz].y - y), abs(cols[lx + 1][lz].y - y),
                abs(cols[lx][lz - 1].y - y), abs(cols[lx][lz + 1].y - y)
            )

            -- Terrain layers
            local top, under, depth
            if water then
                top, under = water_bed(b, biome, col, wx, wz)
                depth = 3
            else
                top, under, depth = ground(b, biome, col, wx, wz, slope)
            end

            chunk:set_block(lx, 0, lz, b.bedrock)
            for ly = 1, y do
                local id
                if ly == y then
                    id = top
                elseif ly > y - depth then
                    id = under
                else
                    id = b.stone
                end

                if biome == "badlands" and ly > SEA - 6 and (ly < y or y > SEA + 12) then
                    id = band(b, ly, wx, wz)
                end
                chunk:set_block(lx, ly, lz, id)
            end

            if biome == "badlands" and y > SEA + 12 then
                chunk:set_block(lx, y, lz, (octave_noise2d(wx, wz, 1, 0.5, 0.15, 44.0) > 0.62) and b.coarse or b.red_sand)
            end

            -- Volcanic lava & obsidian
            local held = false
            if col.lava_to and not water then
                held = true
                for _, d in ipairs(DIRS) do
                    local n = cols[lx + d[1]][lz + d[2]]
                    if n.y < col.lava_to and not (n.lava_to and (n.flow or n.lava_to >= col.lava_to - 1)) then
                        held = false
                        break
                    end
                end
            end

            if held then
                for ly = y + 1, col.lava_to do
                    chunk:set_block(lx, ly, lz, b.lava)
                end
            elseif biome == "basalt_hills" and not water then
                local by_lava = cols[lx - 1][lz].lava_to or cols[lx + 1][lz].lava_to
                             or cols[lx][lz - 1].lava_to or cols[lx][lz + 1].lava_to
                if by_lava and hash_rand(wx, wz, 13.3) < 0.45 then
                    chunk:set_block(lx, y, lz, b.obsidian)
                end
            end

            -- Water & aquatic plants
            if water then
                for wy = y + 1, SEA do
                    chunk:set_block(lx, wy, lz, b.water)
                end
                if frozen and b.ice ~= 0 then
                    chunk:set_block(lx, SEA, lz, b.ice)
                elseif not frozen then
                    if SEA - y == 1 and col.c > -0.3 and hash_rand(wx, wz, 11.2) < 0.25 and b.cattail ~= 0 then
                        local bank = cols[lx - 1][lz].y >= SEA or cols[lx + 1][lz].y >= SEA
                                  or cols[lx][lz - 1].y >= SEA or cols[lx][lz + 1].y >= SEA
                        if bank then
                            if chunk.set_block_with_meta then
                                chunk:set_block_with_meta(lx, y + 1, lz, b.cattail, 1)
                            else
                                chunk:set_block(lx, y + 1, lz, b.cattail)
                            end
                            chunk:set_block(lx, y + 2, lz, b.cattail_top ~= 0 and b.cattail_top or b.cattail)
                        end
                    end

                    if col.swamp > 0.5 and b.lily ~= 0 and hash_rand(wx, wz, 12.7) < 0.025 then
                        if chunk.set_block_with_meta then
                            chunk:set_block_with_meta(lx, SEA, lz, b.lily, 1)
                        else
                            chunk:set_block(lx, SEA, lz, b.lily)
                        end
                    end
                end
            elseif y < 250 then
                -- Land flora
                local def       = BIOMES[biome]
                local r         = hash_rand(wx, wz, 1.1)
                local on_ground = top == b.grass or top == b.snowy_grass or top == b.coarse

                if biome == "basalt_hills" and top ~= b.grass then
                    if r < 0.01 and not col.lava_to and b.dead_bush ~= 0 then
                        chunk:set_block(lx, y + 1, lz, b.dead_bush)
                    end
                elseif biome == "desert" or biome == "badlands" then
                    if r < def.cover then
                        if biome == "desert" and hash_rand(wx, wz, 9.1) < 0.35 and b.cactus ~= 0 then
                            local ch = 1 + floor(hash_rand(wx, wz, 9.9) * 3)
                            for h = 1, ch do
                                chunk:set_block(lx, y + h, lz, b.cactus)
                            end
                        elseif b.dead_bush ~= 0 then
                            chunk:set_block(lx, y + 1, lz, b.dead_bush)
                        end
                    end
                elseif on_ground and top ~= b.snowy_grass then
                    local pick = flower_at(wx, wz, def, b)
                    if not pick and r < def.cover then
                        pick = b.short_grass
                        local f = hash_rand(wx, wz, 2.2)
                        if (biome == "swamp" or biome == "taiga" or biome == "dark_forest") and f < 0.03 then
                            pick = (f < 0.015 and b.mushroom_brown ~= 0) and b.mushroom_brown or b.mushroom_red
                        end
                    end
                    if pick and pick ~= 0 then
                        chunk:set_block(lx, y + 1, lz, pick)
                    end
                end

                if y == SEA and biome ~= "desert" and not frozen and b.reed ~= 0 and hash_rand(wx, wz, 8.8) < 0.15 then
                    local wet = cols[lx - 1][lz].y < SEA or cols[lx + 1][lz].y < SEA
                             or cols[lx][lz - 1].y < SEA or cols[lx][lz + 1].y < SEA
                    if wet then
                        local rh = 2 + floor(hash_rand(wx, wz, 7.7) * 2)
                        for h = 1, rh do
                            chunk:set_block(lx, y + h, lz, (h == rh and b.reed_top ~= 0) and b.reed_top or b.reed)
                        end
                    end
                end
            end
        end
    end

    stone_patches(chunk, chunk_x, chunk_z, b)

    -- Trees (evaluated across chunk neighborhood to prevent border clipping)
    local function get_col(wx, wz)
        local lx, lz = wx - x0, wz - z0
        if lx >= -1 and lx <= 16 and lz >= -1 and lz <= 16 then
            return cols[lx][lz]
        end
        return column(wx, wz)
    end

    for wx = x0 - TREE_REACH, x0 + 15 + TREE_REACH do
        for wz = z0 - TREE_REACH, z0 + 15 + TREE_REACH do
            local roll      = hash_rand(wx, wz, 3.3)
            local bush_roll = hash_rand(wx, wz, 3.9)

            if roll < MAX_TREES or bush_roll < 0.03 then
                local col = get_col(wx, wz)
                local y   = col.y

                if y >= SEA and y + 20 < 256 and not col.lava_to then
                    local biome     = biome_of(col, wx, wz)
                    local def       = BIOMES[biome]
                    local slope     = max(
                        abs(get_col(wx - 1, wz).y - y), abs(get_col(wx + 1, wz).y - y),
                        abs(get_col(wx, wz - 1).y - y), abs(get_col(wx, wz + 1).y - y)
                    )
                    local top       = ground(b, biome, col, wx, wz, slope)
                    local on_ground = top == b.grass or top == b.snowy_grass or top == b.coarse
                    local kind

                    if def.trees > 0 and roll < def.trees and (on_ground or top == b.snow)
                       and not (biome == "basalt_hills" and col.vd < 1.15) then
                        kind = biome
                    elseif biome == "jungle" and on_ground and bush_roll < 0.03 then
                        kind = "bush"
                    end

                    if kind and kind ~= "bush" then
                        for dx = -2, 2 do
                            for dz = -2, 2 do
                                if (dx ~= 0 or dz ~= 0) and kind then
                                    local n = hash_rand(wx + dx, wz + dz, 3.3)
                                    if n < roll and n < def.trees then
                                        kind = nil
                                    end
                                end
                            end
                        end
                    end

                    if kind then
                        local lx, ty, lz = wx - x0, y + 1, wz - z0
                        if inside(lx, y, lz) and chunk:get_block(lx, y, lz) == b.grass then
                            chunk:set_block(lx, y, lz, b.dirt)
                        end

                        if kind == "taiga" or kind == "snowy_taiga" or kind == "snowy_plains"
                           or kind == "mountains" or kind == "snowy_grove" then
                            tree_spruce(chunk, lx, ty, lz, wx, wz, b)
                        elseif kind == "windswept_hills" then
                            if hash_rand(wx, wz, 4.7) < 0.6 then
                                tree_spruce(chunk, lx, ty, lz, wx, wz, b)
                            else
                                tree_round(chunk, lx, ty, lz, wx, wz, b.log_oak, b.leaves_oak, 4, 6)
                            end
                        elseif kind == "jungle" then
                            if hash_rand(wx, wz, 4.4) < 0.35 then
                                tree_round(chunk, lx, ty, lz, wx, wz, b.log_jungle, b.leaves_jungle, 4, 6)
                            else
                                tree_jungle(chunk, lx, ty, lz, wx, wz, b)
                            end
                        elseif kind == "bush" then
                            bush(chunk, lx, ty, lz, b.log_jungle, b.leaves_jungle)
                        elseif kind == "savanna" then
                            tree_acacia(chunk, lx, ty, lz, wx, wz, b)
                        elseif kind == "dark_forest" then
                            tree_dark(chunk, lx, ty, lz, wx, wz, b)
                        elseif kind == "birch_forest" then
                            tree_round(chunk, lx, ty, lz, wx, wz, b.log_birch, b.leaves_birch, 5, 7)
                        elseif kind == "swamp" then
                            tree_round(chunk, lx, ty, lz, wx, wz, b.log_oak, b.leaves_oak, 4, 5)
                        elseif hash_rand(wx, wz, 4.4) < 0.3 then
                            tree_round(chunk, lx, ty, lz, wx, wz, b.log_birch, b.leaves_birch, 5, 7)
                        elseif hash_rand(wx, wz, 4.8) < 0.12 then
                            tree_big(chunk, lx, ty, lz, wx, wz, b.log_oak, b.leaves_oak)
                        else
                            tree_round(chunk, lx, ty, lz, wx, wz, b.log_oak, b.leaves_oak, 4, 6)
                        end
                    end
                end
            end
        end
    end
end

print("[Mapgen] Craft & Ruin biomes and ores loaded")

-- ---------------------------------------------------------------------------
-- Biome Foliage & Grass Tinting
-- ---------------------------------------------------------------------------

local GRASS = {
    plains          = { 145, 189,  89 },
    forest          = { 121, 192,  90 },
    birch_forest    = { 136, 187, 103 },
    dark_forest     = {  80, 122,  50 },
    flower_forest   = { 121, 192,  90 },
    taiga           = { 134, 183, 117 },
    snowy_taiga     = { 128, 180, 126 },
    snowy_plains    = { 128, 180, 126 },
    snowy_grove     = { 128, 180, 126 },
    meadow          = { 131, 191,  99 },
    windswept_hills = { 138, 182, 124 },
    savanna         = { 191, 183,  85 },
    desert          = { 191, 183,  85 },
    badlands        = { 144, 129,  77 },
    jungle          = {  89, 201,  60 },
    swamp           = { 106, 112,  57 },
    mountains       = { 138, 182, 124 },
    basalt_hills    = { 120, 126,  86 },
    beach           = { 145, 189,  89 },
    ocean           = { 142, 185, 113 },
    river           = { 142, 185, 113 },
    deep_ocean      = { 142, 185, 113 },
    cold_ocean      = { 134, 183, 117 },
    warm_ocean      = { 150, 189,  95 },
    frozen_ocean    = { 128, 180, 126 },
    frozen_river    = { 128, 180, 126 },
}

local FOLIAGE = {
    taiga        = { 104, 142, 100 },
    snowy_taiga  = {  96, 161, 123 },
    snowy_plains = {  96, 161, 123 },
    snowy_grove  = {  96, 161, 123 },
    dark_forest  = {  52,  92,  30 },
    mountains    = { 104, 150, 110 },
    savanna      = { 174, 164,  42 },
    desert       = { 174, 164,  42 },
    badlands     = { 158, 129,  77 },
    jungle       = {  48, 187,  11 },
    swamp        = { 106, 112,  57 },
}

function get_grass_color(x, z)
    local c = GRASS[get_biome(x, z)] or GRASS.plains
    return c[1], c[2], c[3]
end

function get_foliage_color(x, z)
    local name = get_biome(x, z)
    local f    = FOLIAGE[name]
    if f then return f[1], f[2], f[3] end
    local r, g, b = get_grass_color(x, z)
    return r * 0.8, g * 0.85, b * 0.8
end

-- ---------------------------------------------------------------------------
-- External Mod Hooks (Weather & Aurora)
-- ---------------------------------------------------------------------------

local function is_valid_coord(x, z)
    return type(x) == "number" and type(z) == "number" and x == x and z == z
end

local function hook_weather()
    if type(weather) ~= "table" then return false end
    weather.is_cold = function(x, z)
        if not is_valid_coord(x, z) then return false end
        local col = column(floor(x), floor(z))
        return col.t_eff < 0.27 or (col.mountain > 0.55 and col.y >= 128 - (0.5 - col.temp) * 40)
    end
    return true
end

local function hook_aurora()
    if type(aurora) ~= "table" or not aurora.set_options then return false end
    aurora.set_options({
        chance = 0.12,
        where  = function(x, _, z)
            if not is_valid_coord(x, z) then return false end
            local b = get_biome(floor(x), floor(z))
            return b == "snowy_plains" or b == "snowy_taiga" or b == "snowy_grove" or b == "frozen_ocean"
        end,
    })
    return true
end

local weather_hooked = hook_weather()
local aurora_hooked  = hook_aurora()

if not (weather_hooked and aurora_hooked) then
    local prev = on_update
    local done = false
    function on_update(...)
        if not done then
            done = true
            if not weather_hooked then hook_weather() end
            if not aurora_hooked then hook_aurora() end
        end
        if prev then return prev(...) end
    end
end