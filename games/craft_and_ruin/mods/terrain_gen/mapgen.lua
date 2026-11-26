-- mapgen.lua - Craft & Ruin's terrain: Minecraft-style biomes that blend into each other
--
--   Oceans, deep oceans, beaches and rivers; plains, forests and birch forests; rolling hills
--   and tall mountains with stony slopes and snowy peaks; taiga and snowy plains with frozen
--   lakes; savannas with acacia trees; deserts with cacti; badlands with banded terracotta
--   mesas; jungles with tall trees; swamps with lily pads and cattails; and basalt hills, the
--   remains of an old volcano: dark rugged hills around a cone with a lava lake in its crater,
--   and lava still running down the slopes in channels lined with magma and obsidian.
--
-- How it works: every column gets a few smooth noise values - continentalness (land or sea),
-- erosion (flat, hilly or mountainous), temperature and humidity (which biome), and river.
-- The height comes from all of them blended smoothly, so biomes never meet at a cliff; the
-- biome then decides the surface blocks, trees and plants.
-- Caves are carved by the engine afterwards (CaveGenerator.cpp), with the same world seed.
--
-- For other mods:
--   get_biome(x, z)                 -> "plains", "forest", "birch_forest", "taiga", "snowy_plains",
--                                      "snowy_taiga", "mountains", "savanna", "desert", "badlands",
--                                      "jungle", "swamp", "basalt_hills", "beach", "ocean", "river"
--   get_biome_parameters(x, z)      -> continentalness, temperature, humidity, river (as before)
--   get_surface_height(x, z)        -> the y of the top block (sea level is 62)
--   find_volcano(x, z)              -> the nearest volcano's x, z, rim y and radius (or nil)

local world_seed = 1337

-- Which version of the terrain this makes. Raise it when saved, unedited chunks should be
-- regenerated; leave it unchanged for updates intended to affect only newly generated chunks.
-- Chunks anyone built in are always kept as they are.
--   1: the first biome generator        2: terrain shaped every 4 blocks and blended (smoother)
--   3: flowers in Minecraft-style patches (far fewer)
terrain_version = 3

local SEA = 62
local floor, abs, min, max, sin = math.floor, math.abs, math.min, math.max, math.sin

engine.set_sun_texture("cw_sun.png")

local volcano_cache = {}          -- (where the volcanoes are depends on the seed)
local grid_cache, grid_cached = {}, 0   -- the terrain's shape every 4 blocks (see grid_point)

-- Called before any generate_chunk, and again whenever another world is opened in the same
-- game session: everything worked out for the last seed is forgotten, or the new world
-- would get the old one's volcanoes (and chunks from different generator threads wouldn't match)
function set_world_seed(seed)
    if seed and seed ~= world_seed then
        world_seed = seed
        volcano_cache = {}
        grid_cache, grid_cached = {}, 0
    end
end

-- ---------------------------------------------------------------------------
-- Noise
-- ---------------------------------------------------------------------------

-- Seeded coordinate hash used by smooth noise and discrete placement.
function get_rand(x, z, seed_offset)
    local val = sin((x + world_seed * 0.1) * 12.9898 + (z + world_seed * 0.1) * 78.233 + seed_offset) * 43758.5453
    return val - floor(val)
end

-- Mix coordinates nonlinearly before thresholding, or placement forms diagonal bands.
function hash_rand(x, z, salt)
    local salt_offset = floor(salt * 100 + 0.5) * 0.618033988749895
    return get_rand(floor(x), floor(z), salt_offset)
end

local function fade(t) return t * t * t * (t * (t * 6.0 - 15.0) + 10.0) end
local function lerp(t, a, b) return a + t * (b - a) end
local function clamp01(v) return v < 0 and 0 or (v > 1 and 1 or v) end
local function smoothstep(e0, e1, x)
    local t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)
end

local function smooth_noise2d(x, z, seed_offset)
    local X, Z = floor(x), floor(z)
    local u, v = fade(x - X), fade(z - Z)
    local n00 = get_rand(X, Z, seed_offset)
    local n10 = get_rand(X + 1, Z, seed_offset)
    local n01 = get_rand(X, Z + 1, seed_offset)
    local n11 = get_rand(X + 1, Z + 1, seed_offset)
    return lerp(v, lerp(u, n00, n10), lerp(u, n01, n11))
end

local function octave_noise2d(x, z, octaves, persistence, scale, seed_offset)
    local total, frequency, amplitude, maxValue = 0, scale, 1, 0
    for i = 1, octaves do
        total = total + smooth_noise2d(x * frequency, z * frequency, seed_offset + i * 1.5) * amplitude
        maxValue = maxValue + amplitude
        amplitude = amplitude * persistence
        frequency = frequency * 2
    end
    return total / (maxValue > 0 and maxValue or 1)
end

-- ---------------------------------------------------------------------------
-- The shape of the land
-- ---------------------------------------------------------------------------

-- (kept for other mods: fog and weather use it)
function get_biome_parameters(world_x, world_z)
    local wx = world_x + (world_seed % 1000)
    local wz = world_z + (world_seed % 1000)
    local continentalness = octave_noise2d(wx, wz, 3, 0.5, 0.0011, 1.0) * 2.0 - 1.0
    local temperature     = octave_noise2d(wx, wz, 3, 0.5, 0.0017, 2.0)
    local humidity        = octave_noise2d(wx, wz, 3, 0.5, 0.0017, 4.0)
    local river           = abs(octave_noise2d(wx, wz, 2, 0.5, 0.0055, 3.0) * 2.0 - 1.0)
    return continentalness, temperature, humidity, river
end

-- ---------------------------------------------------------------------------
-- The terrain's shape is worked out every 4 blocks and blended in between, like Minecraft (which
-- samples its noise on a coarse grid and interpolates). That's what makes slopes smooth and
-- even, instead of every column making its own small bumps - and it's much less work.
-- The sharp things are made per column, after blending: river banks, coasts, the mesas'
-- stepped cliffs (and the volcanoes, in column()).
-- ---------------------------------------------------------------------------
local GRID = 4

-- Everything the shape needs, at one grid point
local function grid_point(gx, gz)
    local key = gx * 1000003 + gz
    local g = grid_cache[key]
    if g then return g end
    local world_x, world_z = gx * GRID, gz * GRID
    local c, temp, hum, river = get_biome_parameters(world_x, world_z)
    -- the noise stays near the middle; spread it out so hot, cold, dry and wet places all happen
    temp = clamp01((temp - 0.5) * 1.6 + 0.5)
    hum = clamp01((hum - 0.5) * 1.6 + 0.5)
    local wx = world_x + (world_seed % 1000)
    local wz = world_z + (world_seed % 1000)

    -- erosion: low = mountains, middle = hills, high = flat
    local erosion = octave_noise2d(wx, wz, 2, 0.5, 0.0024, 7.0)
    local inland = smoothstep(-0.12, 0.10, c)                 -- 0 at sea, 1 well inland
    local mountain = smoothstep(0.42, 0.30, erosion) * inland
    local hills = smoothstep(0.55, 0.40, erosion) * inland     -- (a wide band: hills rise gently out of the flats)

    -- warm and dry, with its own noise: the badlands' mesas
    local bad = smoothstep(0.62, 0.70, temp) * smoothstep(0.40, 0.32, hum)
              * smoothstep(0.46, 0.54, octave_noise2d(wx, wz, 2, 0.5, 0.004, 11.0)) * inland
    -- warm, wet and low: swamps
    local swamp = smoothstep(0.62, 0.72, hum) * smoothstep(0.36, 0.44, temp) * smoothstep(0.64, 0.56, temp)
                * (1 - mountain) * (1 - hills * 0.7)

    -- the land itself: continents, gentle rolls, a little detail
    local height = SEA + 2 + c * 16
    height = height + (octave_noise2d(wx, wz, 4, 0.5, 0.008, 5.0) * 2.0 - 1.0) * 7.0
                    + (octave_noise2d(wx, wz, 2, 0.5, 0.03, 6.0) * 2.0 - 1.0) * 1.2

    if hills > 0 then
        height = height + hills * (8 + (octave_noise2d(wx, wz, 3, 0.5, 0.011, 8.0) * 2.0 - 1.0) * 13.0)
    end
    if mountain > 0 then
        -- ridged noise: sharp crests and valleys between them
        local ridge = 1 - abs(octave_noise2d(wx, wz, 4, 0.5, 0.0055, 9.0) * 2.0 - 1.0)
        local crag = (octave_noise2d(wx, wz, 2, 0.5, 0.07, 10.0) * 2.0 - 1.0) * 3.0
        height = height + mountain * (22 + ridge * ridge * 95 + crag)
    end
    local plateau = 0
    if bad > 0 then plateau = smoothstep(0.42, 0.58, octave_noise2d(wx, wz, 3, 0.5, 0.011, 12.0)) end

    g = { c = c, temp = temp, hum = hum, river = river, inland = inland, mountain = mountain,
          hills = hills, bad = bad, swamp = swamp, height = height, plateau = plateau }
    if grid_cached > 60000 then grid_cache, grid_cached = {}, 0 end   -- (keeps the memory small)
    grid_cache[key] = g
    grid_cached = grid_cached + 1
    return g
end

local SHAPE_FIELDS = { "c", "temp", "hum", "river", "inland", "mountain", "hills", "bad", "swamp", "height", "plateau" }

-- Everything about one column: its height and what decides its biome
local function base_column(world_x, world_z)
    -- blend the four grid points around this column
    local gx, gz = floor(world_x / GRID), floor(world_z / GRID)
    local fx, fz = (world_x - gx * GRID) / GRID, (world_z - gz * GRID) / GRID
    local g00, g10 = grid_point(gx, gz), grid_point(gx + 1, gz)
    local g01, g11 = grid_point(gx, gz + 1), grid_point(gx + 1, gz + 1)
    local v = {}
    for _, k in ipairs(SHAPE_FIELDS) do
        v[k] = lerp(fz, lerp(fx, g00[k], g10[k]), lerp(fx, g01[k], g11[k]))
    end
    local c, temp, hum, river = v.c, v.temp, v.hum, v.river
    local inland, mountain, hills, bad, swamp = v.inland, v.mountain, v.hills, v.bad, v.swamp
    local height = v.height

    if bad > 0 then
        -- mesas: flat-topped plateaus with steep, stepped sides, standing up out of the flats
        local raised = SEA + 4 + v.plateau * 42 + (height - SEA) * 0.3
        local step = 6
        local t = raised / step
        local terraced = (floor(t) + smoothstep(0.86, 1.0, t - floor(t))) * step
        height = lerp(bad, height, terraced)
    end
    if swamp > 0 then
        height = lerp(swamp, height, SEA - 0.4 + (height - SEA) * 0.12)
    end

    -- Oceans
    if c < -0.2 then
        local d = -0.2 - c
        height = height - (d * d * 70.0 + d * 15.0)
    end

    -- Rivers (not up through the mountains: those keep their ridges)
    local is_river = false
    if c > -0.08 and river < 0.045 and mountain < 0.6 and bad < 0.5 then
        local f = (0.045 - river) / 0.045
        local bed = SEA - 1 - f * 3
        height = lerp(clamp01(f * 1.6), height, bed)
        if height < SEA - 3 then height = SEA - 3 end
        is_river = height < SEA
    end

    local y = min(floor(height), 240)
    -- higher is colder (snow on the peaks)
    local t_eff = temp - max(0, y - 88) * 0.0045
    return {
        y = y, c = c, temp = temp, t_eff = t_eff, hum = hum, mountain = mountain, hills = hills,
        bad = bad, swamp = swamp, river = is_river, inland = inland,
        volcanic = 0, vd = 99, lava_to = nil, height = height,
    }
end

-- ---------------------------------------------------------------------------
-- Volcanoes: what's left of them, the basalt hills. One may stand in each 640 x 640 area, on
-- low inland ground. A steep cone whose top was blown off - a broken, jagged rim around a
-- crater with a lava pool, the rim broken open on one side where a lava river pours out and
-- runs down the slope, spreading at the foot. Around it, the basalt hills.
-- ---------------------------------------------------------------------------
local VCELL = 640
local function volcano_of(ci, cj)
    local key = ci * 100003 + cj
    local v = volcano_cache[key]
    if v ~= nil then return v or nil end
    v = false
    if hash_rand(ci, cj, 50.1) < 0.5 then
        local cx = floor(ci * VCELL + 120 + hash_rand(ci, cj, 50.3) * (VCELL - 240))
        local cz = floor(cj * VCELL + 120 + hash_rand(ci, cj, 50.7) * (VCELL - 240))
        local b = base_column(cx, cz)
        local ok = b.c > 0.0 and b.mountain < 0.1 and b.bad < 0.1 and b.swamp < 0.3 and not b.river
                   and b.y >= SEA + 2 and b.y <= SEA + 30
        -- clear ground all round its foot: no mesas, mountains or sea right next to it
        if ok then
            for k = 0, 7 do
                local a = k * math.pi / 4
                local n = base_column(floor(cx + math.cos(a) * 70), floor(cz + math.sin(a) * 70))
                if n.bad > 0.1 or n.mountain > 0.15 or n.y < SEA then ok = false; break end
            end
        end
        if ok then
            local ang = hash_rand(ci, cj, 51.1) * math.pi * 2
            local R = 24 + floor(hash_rand(ci, cj, 51.5) * 10)            -- the cone's radius at its foot
            local H = 34 + floor(hash_rand(ci, cj, 51.9) * 12)            -- ...and how high it would have been
            local rc = R * 0.32                                            -- the crater
            local cap = floor(H * (1 - rc / R) ^ 0.8)                      -- where the top was blown off
            v = { x = cx, z = cz, R = R, H = H, rc = rc, base = b.y, cap = cap,
                  dx = math.cos(ang), dz = math.sin(ang),                  -- which way the lava river runs
                  lava = b.y + cap - 4 }                                   -- the crater's lava pool
        end
    end
    volcano_cache[key] = v
    return v or nil
end

-- The nearest volcano close enough to shape this column, and how far it is (in its radii)
local function near_volcano(x, z)
    local ci, cj = floor(x / VCELL), floor(z / VCELL)
    local best, bestd
    for i = ci - 1, ci + 1 do
        for j = cj - 1, cj + 1 do
            local v = volcano_of(i, j)
            if v then
                local d = math.sqrt((x - v.x) ^ 2 + (z - v.z) ^ 2) / v.R
                if d < 1.7 and (not bestd or d < bestd) then best, bestd = v, d end
            end
        end
    end
    return best, bestd
end

-- find_volcano(x, z) -> the nearest volcano's centre x, z, the y of its rim and its radius, or
-- nil if there's none within about 1000 blocks (for mods: structures, quests, maps...)
function find_volcano(x, z)
    local ci, cj = floor(x / VCELL), floor(z / VCELL)
    local best, bestd
    for i = ci - 2, ci + 2 do
        for j = cj - 2, cj + 2 do
            local v = volcano_of(i, j)
            if v then
                local d = (x - v.x) ^ 2 + (z - v.z) ^ 2
                if not bestd or d < bestd then best, bestd = v, d end
            end
        end
    end
    if not best then return nil end
    return best.x, best.z, best.base + best.cap, best.R
end

local function column(x, z)
    local col = base_column(x, z)
    local v, d = near_volcano(x, z)
    if not v then return col end
    -- the ground around it rises to meet the volcano (its own hills fade out)
    local around = smoothstep(1.7, 1.0, d)
    local h = lerp(around, col.height, v.base + (col.height - v.base) * 0.35)
    -- the cone: steep sides, a low apron at the foot
    local cone = 0
    if d < 1 then cone = v.H * (1 - d) ^ 0.8 end
    if d < 1.45 then cone = cone + 3 * smoothstep(1.45, 0.9, d) end
    -- the top blown off: cut flat at `cap`, broken up into jagged columns along the rim
    local rough = hash_rand(x, z, 52.3)
    local cap = v.cap + ((rough < 0.3) and floor(hash_rand(x, z, 52.7) * 5) or 0)
    local r = d * v.R
    if cone > cap then cone = cap end
    h = h + cone
    if r < v.rc then                                               -- the crater: a bowl inside the rim
        h = v.base + v.cap - 9 * (1 - (r / v.rc) ^ 2) - 1
    end
    -- the lava river: from the crater out through a gap in the rim, down the slope
    local px, pz = x - v.x, z - v.z
    local along = px * v.dx + pz * v.dz
    local across = px * v.dz - pz * v.dx + math.sin(along * 0.13) * 2.5    -- (it winds a little)
    local width = 2.2 + hash_rand(v.x, v.z, 53.1) * 1.2
    local lava_to, flow = nil, false
    if r < v.rc then
        if h < v.lava then lava_to = v.lava end                     -- the pool in the crater
    elseif along > 0 and along < v.R * 1.3 and abs(across) < width then
        local bed = floor(h) - 2
        if r < v.rc + 3 then bed = min(bed, v.lava - 1) end       -- the gap in the rim
        h = bed
        lava_to = bed + 1
        flow = true
    end
    col.height = h
    col.y = min(floor(h), 240)
    col.volcanic = 1
    col.vd = d
    col.flow = flow
    if lava_to and lava_to > col.y then col.lava_to = lava_to end
    -- no rivers or swamps through a volcano
    col.river = false
    col.swamp = 0
    return col
end

function get_surface_height(x, z) return column(x, z).y end
-- (the old name)
function get_terrain_height(x, z) return column(x, z).y end

-- Which biome a column is
local function biome_of(col, x, z)
    local y = col.y
    if y < SEA then
        if col.river then return "river" end
        return "ocean"
    end
    if col.vd < 1.6 then return "basalt_hills" end
    if col.c < -0.04 and y <= SEA + 1 and col.mountain < 0.2 and col.bad < 0.4 then return "beach" end
    if col.mountain > 0.55 and y > 92 then return "mountains" end
    if col.bad > 0.4 then return "badlands" end
    if col.swamp > 0.5 then return "swamp" end
    local t, h = col.t_eff, col.hum
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
            return (octave_noise2d(x, z, 1, 0.5, 0.0035, 31.0) > 0.62) and "birch_forest" or "forest"
        end
        return "plains"
    end
end

function get_biome(x, z)
    return biome_of(column(x, z), x, z)
end

-- ---------------------------------------------------------------------------
-- Blocks (ids looked up once; a missing block falls back to a basic one)
-- ---------------------------------------------------------------------------
local B
local function ids()
    if B then return B end
    local function id(name, fallback)
        local v = engine.get_block_id(name) or 0
        if v == 0 and fallback then v = engine.get_block_id(fallback) or 0 end
        return v
    end
    B = {
        stone = id("stone"), bedrock = id("bedrock", "stone"), grass = id("grass"), dirt = id("dirt"),
        sand = id("sand"), gravel = id("gravel", "stone"), water = id("water"), clay = id("clay", "dirt"),
        snow = id("snow_block", "stone"), snowy_grass = id("snowy_grass", "grass"), ice = id("ice", "water"),
        red_sand = id("red_sand", "sand"), coarse = id("coarse_dirt", "dirt"), andesite = id("andesite", "stone"),
        dead_bush = id("dead_bush"), cactus = id("cactus"), short_grass = id("short_grass"),
        lily = id("lily_pad"), reed = id("reed"), reed_top = id("reed_top"),
        cattail = id("cattail"), cattail_top = id("cattail_top"),
        log_oak = id("log_oak"), leaves_oak = id("leaves_oak"), log_birch = id("log_birch", "log_oak"),
        leaves_birch = id("leaves_birch", "leaves_oak"), log_spruce = id("log_spruce", "log_oak"),
        leaves_spruce = id("leaves_spruce", "leaves_oak"), log_jungle = id("log_jungle", "log_oak"),
        leaves_jungle = id("leaves_jungle", "leaves_oak"), log_acacia = id("log_acacia", "log_oak"),
        leaves_acacia = id("leaves_acacia", "leaves_oak"),
        mushroom_red = id("mushroom_red"), mushroom_brown = id("mushroom_brown"),
        basalt = id("basalt", "stone"), obsidian = id("obsidian", "stone"), magma = id("magma_block", "stone"),
        ash = id("volcanic_ash", "gravel"), lava = id("lava", "magma_block"),
        flowers = {}, terracotta = {},
    }
    -- flowers, by how common they are (like Minecraft: poppies and the yellow ones everywhere,
    -- tulips, bluets and the rest now and then); a name listed twice is twice as likely
    B.flower_by_name = {}
    for _, f in ipairs({ { "poppy", 4 }, { "flower_poppy", 4 }, { "flower_daisy", 4 }, { "flower_azure_bluet", 2 },
                         { "flower_tulip_red", 1 }, { "flower_tulip_pink", 1 }, { "flower_tulip_white", 1 },
                         { "flower_bluebell", 1 }, { "flower_allium", 0.5 } }) do
        local v = engine.get_block_id(f[1]) or 0
        if v ~= 0 and not B.flower_by_name[f[1]] then
            B.flower_by_name[f[1]] = v
            B.flowers[#B.flowers + 1] = { id = v, weight = f[2] }
        end
    end
    local total = 0
    for _, f in ipairs(B.flowers) do total = total + f.weight end
    B.flower_weight = total
    -- the badlands' bands, bottom to top, repeating
    for _, n in ipairs({ "terracotta", "terracotta_orange", "terracotta_orange", "terracotta_yellow",
                         "terracotta", "terracotta_white", "terracotta_light_gray", "terracotta_red",
                         "terracotta", "terracotta_brown", "terracotta_orange", "terracotta" }) do
        local v = engine.get_block_id(n) or 0
        B.terracotta[#B.terracotta + 1] = (v ~= 0) and v or B.red_sand
    end
    return B
end

-- ---------------------------------------------------------------------------
-- Trees (only inside the chunk being made: they're planted away from its edges)
-- ---------------------------------------------------------------------------
local function put(chunk, x, y, z, id)
    if x < 0 or x > 15 or z < 0 or z > 15 or y < 1 or y > 255 then return false end
    if chunk:get_block(x, y, z) == 0 then chunk:set_block(x, y, z, id); return true end
    return false
end
local function log(chunk, x, y, z, id)
    if x >= 0 and x <= 15 and z >= 0 and z <= 15 and y >= 1 and y <= 255 then chunk:set_block(x, y, z, id) end
end
local function disc(chunk, cx, y, cz, r, id, round)
    for dx = -r, r do
        for dz = -r, r do
            if not (round and abs(dx) == r and abs(dz) == r) then put(chunk, cx + dx, y, cz + dz, id) end
        end
    end
end

-- Oak / birch: a trunk and a rounded canopy
local function tree_round(chunk, x, y, z, wx, wz, logid, leafid, tmin, tmax)
    local h = tmin + floor(hash_rand(wx, wz, 21.3) * (tmax - tmin + 1))
    for i = 0, h - 1 do log(chunk, x, y + i, z, logid) end
    disc(chunk, x, y + h - 3, z, 2, leafid, true)
    disc(chunk, x, y + h - 2, z, 2, leafid, true)
    disc(chunk, x, y + h - 1, z, 1, leafid, false)
    disc(chunk, x, y + h, z, 1, leafid, true)
end

-- Spruce: tall and pointed, layers getting smaller towards the top
local function tree_spruce(chunk, x, y, z, wx, wz)
    local b = ids()
    local h = 7 + floor(hash_rand(wx, wz, 22.7) * 5)
    for i = 0, h - 1 do log(chunk, x, y + i, z, b.log_spruce) end
    local r = 2 + ((hash_rand(wx, wz, 23.1) < 0.4) and 1 or 0)
    local layer = 0
    for ly = y + 3, y + h - 1 do
        local rr = (layer % 2 == 0) and r or max(1, r - 1)
        if ly > y + h - 3 then rr = 1 end
        disc(chunk, x, ly, z, rr, b.leaves_spruce, rr > 1)
        layer = layer + 1
        if layer % 3 == 2 and r > 1 then r = r - 1 end
    end
    put(chunk, x, y + h, z, b.leaves_spruce)
    put(chunk, x, y + h + 1, z, b.leaves_spruce)
end

-- Jungle: very tall, a wide canopy on top, sometimes leafy branches lower down
local function tree_jungle(chunk, x, y, z, wx, wz)
    local b = ids()
    local h = 9 + floor(hash_rand(wx, wz, 24.3) * 6)
    for i = 0, h - 1 do log(chunk, x, y + i, z, b.log_jungle) end
    disc(chunk, x, y + h - 2, z, 3, b.leaves_jungle, true)
    disc(chunk, x, y + h - 1, z, 3, b.leaves_jungle, true)
    disc(chunk, x, y + h, z, 2, b.leaves_jungle, true)
    disc(chunk, x, y + h + 1, z, 1, b.leaves_jungle, false)
    if hash_rand(wx, wz, 24.9) < 0.6 then
        local by = y + floor(h * 0.55)
        local d = (hash_rand(wx, wz, 25.3) < 0.5) and 1 or -1
        log(chunk, x + d, by, z, b.log_jungle)
        disc(chunk, x + d * 2, by + 1, z, 1, b.leaves_jungle, false)
    end
end

-- Jungle bush: a log with leaves around it
local function bush(chunk, x, y, z, logid, leafid)
    log(chunk, x, y, z, logid)
    disc(chunk, x, y, z, 2, leafid, true)
    disc(chunk, x, y + 1, z, 1, leafid, true)
end

-- Acacia: a trunk that leans off to one side, with a flat, wide top
local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
local function tree_acacia(chunk, x, y, z, wx, wz)
    local b = ids()
    local d = DIRS[1 + floor(hash_rand(wx, wz, 26.1) * 4)]
    local straight = 3 + floor(hash_rand(wx, wz, 26.7) * 2)
    local bend = 2 + floor(hash_rand(wx, wz, 27.3) * 2)
    for i = 0, straight - 1 do log(chunk, x, y + i, z, b.log_acacia) end
    local tx, ty, tz = x, y + straight, z
    for i = 1, bend do
        tx, tz = tx + d[1], tz + d[2]
        log(chunk, tx, ty, tz, b.log_acacia)
        ty = ty + 1
    end
    log(chunk, tx, ty, tz, b.log_acacia)
    disc(chunk, tx, ty + 1, tz, 3, b.leaves_acacia, true)
    disc(chunk, tx, ty + 2, tz, 2, b.leaves_acacia, true)
    -- sometimes a second, smaller top off the trunk
    if hash_rand(wx, wz, 27.9) < 0.4 then
        local ox, oz = x - d[1] * 2, z - d[2] * 2
        log(chunk, x - d[1], y + straight, z - d[2], b.log_acacia)
        log(chunk, ox, y + straight + 1, oz, b.log_acacia)
        disc(chunk, ox, y + straight + 2, oz, 2, b.leaves_acacia, true)
    end
end

-- ---------------------------------------------------------------------------
-- Flowers grow in patches, like Minecraft's: each 16x16 area may have one small patch
-- (radius 2-4) of a single kind, thick in the middle and thinning out; outside the patches,
-- now and then a lone flower. Same answer from any chunk (it only depends on world x/z).
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
    local c0x, c1x = floor((wx - 4) / FLOWER_CELL), floor((wx + 4) / FLOWER_CELL)
    local c0z, c1z = floor((wz - 4) / FLOWER_CELL), floor((wz + 4) / FLOWER_CELL)
    for ci = c0x, c1x do
        for cj = c0z, c1z do
            if hash_rand(ci, cj, 61.1) < def.flowers then
                local px = ci * FLOWER_CELL + 4 + hash_rand(ci, cj, 61.3) * (FLOWER_CELL - 8)
                local pz = cj * FLOWER_CELL + 4 + hash_rand(ci, cj, 61.5) * (FLOWER_CELL - 8)
                local radius = 2 + hash_rand(ci, cj, 61.9) * 2
                local dx, dz = wx - px, wz - pz
                local d = math.sqrt(dx * dx + dz * dz)
                if d < radius and hash_rand(wx, wz, 62.3) < 0.45 * (1 - d / radius) + 0.08 then
                    if def.flower and b.flower_by_name[def.flower] then return b.flower_by_name[def.flower] end
                    return pick_flower(b, hash_rand(ci, cj, 61.7))
                end
            end
        end
    end
    if hash_rand(wx, wz, 62.9) < 0.0015 * def.flowers / 0.3 then   -- a lone one
        return pick_flower(b, hash_rand(wx, wz, 63.1))
    end
    return nil
end

-- What grows where: trees per column, plants, and the surface
-- ---------------------------------------------------------------------------
local BIOMES = {
    -- tree chance per column, ground cover (grass) chance, and flowers: the chance that a
    -- 16x16 area has a patch of them (Minecraft-style: a few small patches of one kind, not
    -- flowers spread everywhere)
    plains       = { trees = 0.0012, cover = 0.30, flowers = 0.30 },
    forest       = { trees = 0.030, cover = 0.22, flowers = 0.22 },
    birch_forest = { trees = 0.030, cover = 0.22, flowers = 0.22 },
    taiga        = { trees = 0.030, cover = 0.20, flowers = 0.0 },
    snowy_taiga  = { trees = 0.016, cover = 0.0, flowers = 0.0 },
    snowy_plains = { trees = 0.0008, cover = 0.0, flowers = 0.0 },
    savanna      = { trees = 0.0035, cover = 0.45, flowers = 0.06 },
    jungle       = { trees = 0.060, cover = 0.45, flowers = 0.08 },
    swamp        = { trees = 0.024, cover = 0.18, flowers = 0.035 },
    desert       = { trees = 0, cover = 0.012, flowers = 0 },
    badlands     = { trees = 0, cover = 0.010, flowers = 0 },
    mountains    = { trees = 0.004, cover = 0.10, flowers = 0.06 },
    basalt_hills = { trees = 0.006, cover = 0.12, flowers = 0.02 },
    beach        = { trees = 0, cover = 0, flowers = 0 },
    ocean        = { trees = 0, cover = 0, flowers = 0 },
    river        = { trees = 0, cover = 0, flowers = 0 },
}

local function band(b, y, wx, wz)
    -- the bands wave slightly up and down across the badlands
    local shift = floor(octave_noise2d(wx, wz, 1, 0.5, 0.02, 41.0) * 4)
    local n = #b.terracotta
    return b.terracotta[((y + shift) % n) + 1]
end

function generate_chunk(chunk, chunk_x, chunk_z)
    local b = ids()
    local trees = {}

    -- every column of the chunk, plus a ring around it (for "is there water next to me")
    local cols = {}
    for lx = -1, 16 do
        cols[lx] = {}
        for lz = -1, 16 do
            cols[lx][lz] = column(chunk_x * 16 + lx, chunk_z * 16 + lz)
        end
    end

    for lx = 0, 15 do
        for lz = 0, 15 do
            local wx, wz = chunk_x * 16 + lx, chunk_z * 16 + lz
            local col = cols[lx][lz]
            local y = col.y
            local biome = biome_of(col, wx, wz)
            local water = y < SEA
            local frozen = col.t_eff < 0.27

            -- ---- the ground: top block, a few blocks under it, then stone
            local top, under, depth = b.grass, b.dirt, 3
            if biome == "desert" then top, under, depth = b.sand, b.sand, 5
            elseif biome == "beach" then top, under, depth = b.sand, b.sand, 3
                if frozen then top = b.snowy_grass; under = b.dirt end
            elseif biome == "badlands" then top, under, depth = b.red_sand, b.red_sand, 2
            elseif biome == "snowy_plains" or biome == "snowy_taiga" then top = b.snowy_grass
            elseif biome == "savanna" and octave_noise2d(wx, wz, 1, 0.5, 0.12, 45.0) > 0.88 then top = b.coarse
            elseif biome == "mountains" then
                local snowline = 128 - (0.5 - col.temp) * 40
                local rocky = octave_noise2d(wx, wz, 2, 0.5, 0.08, 43.0)
                if y >= snowline then top, under = b.snow, b.snow
                elseif y > 104 or rocky > 0.62 then
                    top = (rocky > 0.5) and b.stone or b.andesite
                    under = b.stone
                    if rocky < 0.32 then top = b.gravel end
                elseif frozen then top = b.snowy_grass end
            elseif biome == "basalt_hills" then
                local patch = octave_noise2d(wx, wz, 2, 0.5, 0.09, 46.0)
                if col.vd < 1.0 then
                    -- the cone: basalt and stone, green creeping up the lower slopes
                    top, under, depth = b.basalt, b.basalt, 9
                    if patch > 0.66 then top = b.stone
                    elseif patch < 0.30 then top = b.andesite end
                    if col.vd > 0.55 and octave_noise2d(wx, wz, 2, 0.5, 0.11, 47.0) > 0.6 then top = b.grass end
                else
                    -- around it: grassland strewn with basalt, ash and gravel
                    top, under, depth = b.grass, b.dirt, 3
                    if patch > 0.66 then top = b.basalt elseif patch < 0.26 then top = b.coarse
                    elseif patch < 0.31 then top = b.gravel end
                end
                if col.lava_to then top = b.magma end            -- the bed of the lava, glowing
            elseif biome == "taiga" and frozen then top = b.snowy_grass
            elseif biome == "plains" and frozen then top = b.snowy_grass end

            if water then
                local bed = octave_noise2d(wx * 0.05, wz * 0.05, 2, 0.5, 1.0, 99.0)
                local wdepth = SEA - y
                if col.swamp > 0.5 then
                    top = (bed < 0.6) and b.dirt or ((bed < 0.85) and b.gravel or b.clay); under = b.dirt
                elseif biome == "river" then
                    top = (bed < 0.55) and b.sand or ((bed < 0.8) and b.gravel or b.clay); under = b.sand
                elseif wdepth <= 3 then top, under = b.sand, b.sand
                else
                    top = (bed < 0.45) and b.gravel or ((bed < 0.75) and b.sand or b.stone)
                    under = (top == b.stone) and b.stone or b.dirt
                end
            end

            chunk:set_block(lx, 0, lz, b.bedrock)
            for ly = 1, y do
                local id
                if ly == y then id = top
                elseif ly > y - depth then id = under
                else id = b.stone end
                -- badlands: the whole cliff is banded terracotta, down to a little below sea level
                if biome == "badlands" and ly > SEA - 6 and (ly < y or y > SEA + 12) then
                    id = band(b, ly, wx, wz)
                end
                chunk:set_block(lx, ly, lz, id)
            end
            if biome == "badlands" and y > SEA + 12 then
                -- mesa tops: red sand, now and then a patch of coarse dirt
                chunk:set_block(lx, y, lz, (octave_noise2d(wx, wz, 1, 0.5, 0.15, 44.0) > 0.62) and b.coarse or b.red_sand)
            end

            -- ---- the basalt hills' lava: the crater lake and the channels down the slopes.
            -- Only where it's held in (the ground on every side is as high, or is more of the
            -- flow): elsewhere the channel is a glowing magma bed, so lava never pours out
            -- over the hillside.
            local held = false
            if col.lava_to and not water then
                held = true
                for _, d in ipairs({ { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }) do
                    local n = cols[lx + d[1]][lz + d[2]]
                    -- a lower neighbour is fine if it's more of the same river (it carries it on down)
                    if n.y < col.lava_to and not (n.lava_to and (n.flow or n.lava_to >= col.lava_to - 1)) then held = false end
                end
            end
            if held then
                for ly = y + 1, col.lava_to do chunk:set_block(lx, ly, lz, b.lava) end
            elseif biome == "basalt_hills" and not water then
                -- ground right next to lava has crusted into obsidian here and there
                local by_lava = cols[lx - 1][lz].lava_to or cols[lx + 1][lz].lava_to
                             or cols[lx][lz - 1].lava_to or cols[lx][lz + 1].lava_to
                if by_lava and hash_rand(wx, wz, 13.3) < 0.45 then chunk:set_block(lx, y, lz, b.obsidian) end
            end

            -- ---- water, frozen over where it's cold
            if water then
                for wy = y + 1, SEA do chunk:set_block(lx, wy, lz, b.water) end
                if frozen and b.ice ~= 0 then chunk:set_block(lx, SEA, lz, b.ice) end
                if lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 and not frozen then
                    -- cattails in shallow water next to the bank
                    if SEA - y == 1 and col.c > -0.3 and hash_rand(wx, wz, 11.2) < 0.25 and b.cattail ~= 0 then
                        local bank = cols[lx - 1][lz].y >= SEA or cols[lx + 1][lz].y >= SEA
                                  or cols[lx][lz - 1].y >= SEA or cols[lx][lz + 1].y >= SEA
                        if bank then
                            if chunk.set_block_with_meta then chunk:set_block_with_meta(lx, y + 1, lz, b.cattail, 1)
                            else chunk:set_block(lx, y + 1, lz, b.cattail) end
                            chunk:set_block(lx, y + 2, lz, b.cattail_top ~= 0 and b.cattail_top or b.cattail)
                        end
                    end
                    if col.swamp > 0.5 and b.lily ~= 0 and hash_rand(wx, wz, 12.7) < 0.09 then
                        if chunk.set_block_with_meta then chunk:set_block_with_meta(lx, SEA, lz, b.lily, 1)
                        else chunk:set_block(lx, SEA, lz, b.lily) end
                    end
                end
            elseif lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 and y < 250 then
                -- ---- plants
                local def = BIOMES[biome]
                local r = hash_rand(wx, wz, 1.1)
                local on_ground = top == b.grass or top == b.snowy_grass or top == b.coarse
                if biome == "basalt_hills" and top ~= b.grass then
                    if r < 0.01 and not col.lava_to and b.dead_bush ~= 0 then chunk:set_block(lx, y + 1, lz, b.dead_bush) end
                elseif biome == "desert" or biome == "badlands" then
                    if r < def.cover then
                        if biome == "desert" and hash_rand(wx, wz, 9.1) < 0.35 and b.cactus ~= 0 then
                            local ch = 1 + floor(hash_rand(wx, wz, 9.9) * 3)
                            for h = 1, ch do chunk:set_block(lx, y + h, lz, b.cactus) end
                        elseif b.dead_bush ~= 0 then
                            chunk:set_block(lx, y + 1, lz, b.dead_bush)
                        end
                    end
                elseif on_ground and top ~= b.snowy_grass then
                    local pick = flower_at(wx, wz, def, b)
                    if not pick and r < def.cover then
                        pick = b.short_grass
                        local f = hash_rand(wx, wz, 2.2)
                        if (biome == "swamp" or biome == "taiga") and f < 0.03 then
                            pick = (f < 0.015 and b.mushroom_brown ~= 0) and b.mushroom_brown or b.mushroom_red
                        end
                    end
                    if pick and pick ~= 0 then chunk:set_block(lx, y + 1, lz, pick) end
                end
                -- reeds at the water's edge
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

                -- ---- trees (away from the chunk's edges, so they're never cut in half)
                local edge = (biome == "savanna") and 5 or ((biome == "jungle") and 4 or 3)
                if def.trees > 0 and lx >= edge and lx <= 15 - edge and lz >= edge and lz <= 15 - edge
                   and not (biome == "basalt_hills" and col.vd < 1.15)
                   and (on_ground or top == b.snow) and y + 18 < 256 and hash_rand(wx, wz, 3.3) < def.trees then
                    trees[#trees + 1] = { lx, y + 1, lz, wx, wz, biome }
                elseif biome == "jungle" and on_ground and lx >= 2 and lx <= 13 and lz >= 2 and lz <= 13
                       and hash_rand(wx, wz, 3.9) < 0.03 then
                    trees[#trees + 1] = { lx, y + 1, lz, wx, wz, "bush" }
                end
            end
        end
    end

    for _, t in ipairs(trees) do
        local x, y, z, wx, wz, biome = t[1], t[2], t[3], t[4], t[5], t[6]
        -- grass under a tree turns to dirt, like Minecraft
        if chunk:get_block(x, y - 1, z) == b.grass then chunk:set_block(x, y - 1, z, b.dirt) end
        if biome == "taiga" or biome == "snowy_taiga" or biome == "snowy_plains" or biome == "mountains" then
            tree_spruce(chunk, x, y, z, wx, wz)
        elseif biome == "jungle" then
            if hash_rand(wx, wz, 4.4) < 0.35 then tree_round(chunk, x, y, z, wx, wz, b.log_jungle, b.leaves_jungle, 4, 6)
            else tree_jungle(chunk, x, y, z, wx, wz) end
        elseif biome == "bush" then
            bush(chunk, x, y, z, b.log_jungle, b.leaves_jungle)
        elseif biome == "savanna" then
            tree_acacia(chunk, x, y, z, wx, wz)
        elseif biome == "birch_forest" then
            tree_round(chunk, x, y, z, wx, wz, b.log_birch, b.leaves_birch, 5, 7)
        elseif biome == "swamp" then
            tree_round(chunk, x, y, z, wx, wz, b.log_oak, b.leaves_oak, 4, 5)
        elseif hash_rand(wx, wz, 4.4) < 0.3 then
            tree_round(chunk, x, y, z, wx, wz, b.log_birch, b.leaves_birch, 5, 7)
        else
            tree_round(chunk, x, y, z, wx, wz, b.log_oak, b.leaves_oak, 4, 6)
        end
    end
end

print("[Mapgen] Craft & Ruin biomes: plains, forests, taiga, snow, hills, mountains, savanna, desert, badlands, jungle, swamp, basalt hills, oceans and rivers")

-- ===========================================================================
-- Biome colours (Minecraft-style). The engine asks every few blocks of each new chunk and
-- blends in between; grass and tinted leaves take the colour of the place.
-- ===========================================================================
local GRASS = {
    plains = { 145, 189, 89 }, forest = { 121, 192, 90 }, birch_forest = { 136, 187, 103 },
    taiga = { 134, 183, 117 }, snowy_taiga = { 128, 180, 126 }, snowy_plains = { 128, 180, 126 },
    savanna = { 191, 183, 85 }, desert = { 191, 183, 85 }, badlands = { 144, 129, 77 },
    jungle = { 89, 201, 60 }, swamp = { 106, 112, 57 }, mountains = { 138, 182, 124 },
    basalt_hills = { 120, 126, 86 },
    beach = { 145, 189, 89 }, ocean = { 142, 185, 113 }, river = { 142, 185, 113 },
}
local FOLIAGE = {
    taiga = { 104, 142, 100 }, snowy_taiga = { 96, 161, 123 }, snowy_plains = { 96, 161, 123 },
    mountains = { 104, 150, 110 }, savanna = { 174, 164, 42 }, desert = { 174, 164, 42 },
    badlands = { 158, 129, 77 }, jungle = { 48, 187, 11 }, swamp = { 106, 112, 57 },
}

function get_grass_color(x, z)
    local col = column(x, z)
    local c = GRASS[biome_of(col, x, z)] or GRASS.plains
    return c[1], c[2], c[3]
end

function get_foliage_color(x, z)
    local col = column(x, z)
    local name = biome_of(col, x, z)
    local f = FOLIAGE[name]
    if f then return f[1], f[2], f[3] end
    local r, g, bb = get_grass_color(x, z)
    return r * 0.8, g * 0.85, bb * 0.8
end

-- Rain falls as snow in snowy biomes and high on the mountains (the weather mod asks this)
local function hook_weather()
    if type(weather) ~= "table" then return false end
    weather.is_cold = function(x, z)
        -- (no position - e.g. asked on the dedicated server, which has no player of its own)
        if type(x) ~= "number" or type(z) ~= "number" or x ~= x or z ~= z then return false end
        local col = column(floor(x), floor(z))
        return col.t_eff < 0.27 or (col.mountain > 0.55 and col.y >= 128 - (0.5 - col.temp) * 40)
    end
    return true
end
-- The aurora mod (if it's on): now and then on a clear night, seen in the snowy biomes only
local function hook_aurora()
    if type(aurora) ~= "table" or not aurora.set_options then return false end
    aurora.set_options({
        chance = 0.12,                -- about one night in eight
        where = function(x, _, z)
            if type(x) ~= "number" or type(z) ~= "number" or x ~= x or z ~= z then return false end
            local b = get_biome(floor(x), floor(z))
            return b == "snowy_plains" or b == "snowy_taiga"
        end,
    })
    return true
end
local weather_hooked, aurora_hooked = hook_weather(), hook_aurora()
if not (weather_hooked and aurora_hooked) then
    -- the weather and aurora mods load after this one: hook them on the first update
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
