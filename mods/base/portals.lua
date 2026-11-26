-- portals.lua: an API for portals between dimensions (Dimensions.h). Part of the base mod.
-- Nothing about how a portal works is fixed: its frame, which way it faces, where it takes you,
-- whether it links to a portal at the other end or builds one, how long you stand in it, who may
-- use it - each is a setting or a function of your own. For example, a classic underworld
-- portal:
--
--   portals.register("mymod:portal", {
--       frame = "obsidian", block = "mymod:portal",
--       from = "overworld", to = "mymod:depths",          -- (its coordinate_scale = 8)
--       igniters = { "flint_and_steel" },
--       delay = 4, creative_delay = 0, cooldown = 0,       -- 4 seconds in it, at once in creative
--       destination = "scaled", arrive = "find_or_build",  -- 1:8, the nearest portal or a new one
--       mob_cooldown = 15,
--   })
--
-- And something completely different - a flat pad that drops you at the sky dimension's spawn
-- after a second, one way, players only:
--
--   portals.register("mymod:sky_pad", {
--       frame = "gold_block", block = "mymod:pad", orientations = { "y" },
--       min_size = { 3, 3 }, max_size = { 3, 3 },
--       from = "overworld", to = "mymod:sky", one_way = true,
--       destination = "spawn", arrive = "exact", delay = 1, mobs = false,
--   })
--
-- Settings (everything is optional except block, from and to):
--   block           the portal block (your game registers it: transparent, walkable = false).
--                   Its metadata is which way it faces: 0 along x, 1 along z, 2 flat.
--   from, to        the two dimensions it links. one_way = true: it only goes from -> to.
--   frame           a block name, or a list of them: what a lit frame is made of. Without a frame,
--                   portals only appear where your code puts them (portals.create).
--   orientations    { "x", "z" } (default: standing, either way), { "y" } (flat on the ground), or any mix
--   min_size, max_size   the inside, { width, height }: default { 2, 3 } .. { 21, 21 }
--   igniters        items that light a frame when used on its inside (or call portals.light)
--   delay           seconds standing in it before it takes you (0 = at once); creative_delay for
--                   creative players (default: delay)
--   cooldown        seconds after a trip before any portal takes you again (default 1)
--   step_out        true (default): you have to leave the portal you arrive in before it works
--   destination     where a trip aims in the other dimension:
--                     "scaled" (default) - your position x the two dimensions' coordinate_scale
--                     "same"   - the same coordinates
--                     "spawn"  - the other dimension's spawn (dimensions.register's `spawn`)
--                     function(trip) -> x, y, z   (trip: see below)
--   scale           a fixed ratio for "scaled" instead of the dimensions' (here : there)
--   arrive          "find_or_build" (default) - a portal of this kind near the aim, else a new one;
--                   "find" - a portal near the aim, else the aim itself; "exact" - the aim itself;
--                   function(trip) -> x, y, z
--   search_radius   how far to look for a portal there: a number, or "scaled" (default: 128 / the
--                   target's coordinate_scale, at least 16)
--   build           true (default): the built-in builder (a frame of the first frame block, the
--                   smallest size, on solid ground within build_radius = 16 blocks, else on a
--                   platform), false, or function(trip) -> the shape it made ({ x, y, z, axis, w, h })
--   mobs            true (default): mobs go through too, every mob_cooldown seconds (15)
--   can_use(name, trip) -> false to refuse a player (keys, levels, a boss beaten...)
--   on_travel(name, trip) after a player went through
-- A trip is { name = the portal kind, player = name or nil, entity = the mob or nil, from, to,
--   x, y, z (where it started), axis, aim_x, aim_y, aim_z (once worked out) }.
--
-- Functions: portals.register(name, def), portals.light(x, y, z [, name]) -> true if a frame there was lit (in dimensions.current()),
--   portals.create(name, dimension, x, y, z, axis, w, h) -> shape (makes a portal - and its frame
--   if it has one - by code), portals.remove(dimension, x, y, z), portals.find(dimension, x, y, z,
--   radius [, name]), portals.list(dimension), portals.travel(player, name) (through now),
--   portals.shape_at(name, x, y, z [, axis]) -> the frame's inside there, or nil.
-- Hook: on_portal_travel(player, from, to, kind).

portals = portals or {}
local P = portals
P.defs = P.defs or {}

local STORE = "portals:index"        -- each world's portals (its mod storage)
local floor = math.floor

local function dim_name(d) return (d == nil or d == "" or d == "overworld") and "overworld" or d end
local function block_at(x, y, z) return engine.get_block_name(x, y, z) end
local function is_authority() return not engine.is_authority or engine.is_authority() end
local function copy(t)
    if type(t) ~= "table" then return t end
    local c = {}
    for k, v in pairs(t) do c[k] = copy(v) end
    return c
end

-- ===========================================================================
-- Registering
-- ===========================================================================
function P.register(name, def)
    assert(type(name) == "string" and name ~= "", "portals.register: a name")
    assert(type(def) == "table" and type(def.block) == "string", "portals.register: block (the portal block's name)")
    assert(type(def.from) == "string" and type(def.to) == "string", "portals.register: from and to (dimensions)")
    def = copy(def)   -- (functions are kept as they are)
    def.name = name
    def.from, def.to = dim_name(def.from), dim_name(def.to)
    if type(def.frame) == "string" then def.frames = { def.frame }
    elseif type(def.frame) == "table" then def.frames = def.frame; def.frame = def.frame[1] end
    def.frame_set = {}
    for _, f in ipairs(def.frames or {}) do def.frame_set[f] = true end
    def.orientations = def.orientations or { "x", "z" }
    def.min_size = def.min_size or { 2, 3 }
    def.max_size = def.max_size or { 21, 21 }
    def.delay = tonumber(def.delay) or 0
    def.creative_delay = tonumber(def.creative_delay) or def.delay
    def.cooldown = tonumber(def.cooldown) or 1
    if def.step_out == nil then def.step_out = true end
    def.destination = def.destination or "scaled"
    def.arrive = def.arrive or "find_or_build"
    def.search_radius = def.search_radius or "scaled"
    if def.build == nil then def.build = true end
    def.build_radius = tonumber(def.build_radius) or 16
    if def.mobs == nil then def.mobs = true end
    def.mob_cooldown = tonumber(def.mob_cooldown) or 15
    P.defs[name] = def
    for _, item in ipairs(def.igniters or {}) do
        if engine.register_item_use then
            engine.register_item_use(item, function(pointed)
                local at = pointed and (pointed.above or pointed.under)
                if not at then return false end
                if engine.send_to_server then
                    engine.send_to_server("portals:light", string.format("%d %d %d %s", at.x, at.y, at.z, name))
                end
                return true
            end)
        end
    end
    return def
end

-- ===========================================================================
-- Each world's portals (mod storage), by dimension
-- ===========================================================================
local function load_index()
    local text = engine.get_storage and engine.get_storage(STORE)
    if type(text) == "string" and hud and hud.decode then
        local t = hud.decode(text)
        if type(t) == "table" then return t end
    end
    return {}
end
local function save_index(idx)
    if engine.set_storage and hud and hud.encode then engine.set_storage(STORE, hud.encode(idx)) end
end
local function remember(dim, s)
    local idx = load_index()
    local list = idx[dim] or {}
    idx[dim] = list
    for i = #list, 1, -1 do
        local p = list[i]
        if p.x == s.x and p.y == s.y and p.z == s.z and p.axis == s.axis then table.remove(list, i) end
    end
    list[#list + 1] = { x = s.x, y = s.y, z = s.z, axis = s.axis, w = s.w, h = s.h, name = s.name }
    save_index(idx)
end

-- The directions of a portal's inside: u (width), v (height), n (out of it)
local AXES = {
    x = { u = { 1, 0, 0 }, v = { 0, 1, 0 }, n = { 0, 0, 1 }, meta = 0 },
    z = { u = { 0, 0, 1 }, v = { 0, 1, 0 }, n = { 1, 0, 0 }, meta = 1 },
    y = { u = { 1, 0, 0 }, v = { 0, 0, 1 }, n = { 0, 1, 0 }, meta = 2 },
}
local function at(s, i, j) -- the cell i along, j up, of a shape
    local a = AXES[s.axis]
    return s.x + a.u[1] * i + a.v[1] * j, s.y + a.u[2] * i + a.v[2] * j, s.z + a.u[3] * i + a.v[3] * j
end
local function inside(s, x, y, z)
    for i = 0, s.w - 1 do
        for j = 0, s.h - 1 do
            local cx, cy, cz = at(s, i, j)
            if cx == x and cy == y and cz == z then return true end
        end
    end
    return false
end

local function forget_at(dim, x, y, z)
    local idx = load_index()
    local list = idx[dim]
    if not list then return end
    for i = #list, 1, -1 do
        if list[i].axis and AXES[list[i].axis] and inside(list[i], x, y, z) then table.remove(list, i) end
    end
    save_index(idx)
end
function P.list(dim) return load_index()[dim_name(dim)] or {} end

-- ===========================================================================
-- Shapes: the inside of a frame (any size the kind allows, its orientations)
-- ===========================================================================
local function shape_on(def, x, y, z, axis)
    local a = AXES[axis]
    if not a or not def.frames then return nil end
    local u, v = a.u, a.v
    local function open(n) return n == "air" or n == def.block or n == "fire" end
    local function frame(n) return def.frame_set[n] == true end
    local maxw, maxh = def.max_size[1], def.max_size[2]
    if not open(block_at(x, y, z)) then return nil end
    -- back along v to the frame
    local bx, by, bz, n = x, y, z, 0
    while open(block_at(bx - v[1], by - v[2], bz - v[3])) do
        bx, by, bz = bx - v[1], by - v[2], bz - v[3]
        n = n + 1
        if n > maxh then return nil end
    end
    if not frame(block_at(bx - v[1], by - v[2], bz - v[3])) then return nil end
    -- back along u to the frame
    n = 0
    while open(block_at(bx - u[1], by - u[2], bz - u[3])) do
        bx, by, bz = bx - u[1], by - u[2], bz - u[3]
        n = n + 1
        if n > maxw then return nil end
    end
    if not frame(block_at(bx - u[1], by - u[2], bz - u[3])) then return nil end
    local s = { x = bx, y = by, z = bz, axis = axis, w = 0, h = 0, name = def.name }
    -- width: open cells with frame behind them (along -v)
    while s.w <= maxw do
        local cx, cy, cz = at(s, s.w, 0)
        if not open(block_at(cx, cy, cz)) or not frame(block_at(cx - v[1], cy - v[2], cz - v[3])) then break end
        s.w = s.w + 1
    end
    do
        local cx, cy, cz = at(s, s.w, 0)
        if s.w > maxw or not frame(block_at(cx, cy, cz)) then return nil end
    end
    -- height: rows open inside, frame at both ends, up to a row of frame
    while true do
        if s.h > maxh then return nil end
        local closed = true
        for i = 0, s.w - 1 do
            local cx, cy, cz = at(s, i, s.h)
            if not frame(block_at(cx, cy, cz)) then closed = false break end
        end
        if closed then break end
        for i = 0, s.w - 1 do
            local cx, cy, cz = at(s, i, s.h)
            if not open(block_at(cx, cy, cz)) then return nil end
        end
        local lx, ly, lz = at(s, -1, s.h)
        local rx, ry, rz = at(s, s.w, s.h)
        if not frame(block_at(lx, ly, lz)) or not frame(block_at(rx, ry, rz)) then return nil end
        s.h = s.h + 1
    end
    if s.w < def.min_size[1] or s.h < def.min_size[2] then return nil end
    return s
end

-- portals.shape_at(name, x, y, z [, axis]) -> the inside of a frame of that kind around x, y, z
function P.shape_at(name, x, y, z, axis)
    local def = P.defs[name]
    if not def then return nil end
    x, y, z = floor(x), floor(y), floor(z)
    for _, a in ipairs(axis and { axis } or def.orientations) do
        local s = shape_on(def, x, y, z, a)
        if s then return s end
    end
    return nil
end

local function fill(def, s, blockname)
    local meta = AXES[s.axis].meta
    for i = 0, s.w - 1 do
        for j = 0, s.h - 1 do
            local x, y, z = at(s, i, j)
            if blockname == def.block and engine.set_block_meta then engine.set_block_meta(x, y, z, blockname, meta)
            else engine.set_block(x, y, z, blockname) end
        end
    end
end

-- portals.light(x, y, z [, name]) -> true: a frame around x, y, z was lit (where the world runs)
function P.light(x, y, z, name)
    if not is_authority() then return false end
    local here = dim_name(dimensions.current())
    for kind, def in pairs(P.defs) do
        if (not name or kind == name) and def.frames and (def.from == here or (def.to == here and not def.one_way)) then
            local s = P.shape_at(kind, x, y, z)
            if s then
                fill(def, s, def.block)
                remember(here, s)
                return true
            end
        end
    end
    return false
end

-- portals.create(name, dimension, x, y, z, axis, w, h) -> the shape: a portal made by code at
-- x, y, z (its first inside cell), with its frame if the kind has one
function P.create(name, dim, x, y, z, axis, w, h)
    local def = P.defs[name]
    if not def then return nil end
    dim = dim_name(dim)
    axis = axis or def.orientations[1]
    w, h = w or def.min_size[1], h or def.min_size[2]
    local s = { x = floor(x), y = floor(y), z = floor(z), axis = axis, w = w, h = h, name = name }
    dimensions.load_area(dim, s.x, s.z, math.max(w, h) + 2)
    dimensions.run_in(dim, function()
        if def.frame then
            for i = -1, w do
                local x1, y1, z1 = at(s, i, -1)
                local x2, y2, z2 = at(s, i, h)
                engine.set_block(x1, y1, z1, def.frame)
                engine.set_block(x2, y2, z2, def.frame)
            end
            for j = 0, h - 1 do
                local x1, y1, z1 = at(s, -1, j)
                local x2, y2, z2 = at(s, w, j)
                engine.set_block(x1, y1, z1, def.frame)
                engine.set_block(x2, y2, z2, def.frame)
            end
        end
        fill(def, s, def.block)
    end)
    remember(dim, s)
    return s
end

-- portals.remove(dimension, x, y, z): the portal with a block at x, y, z goes (its frame stays)
function P.remove(dim, x, y, z)
    dim = dim_name(dim)
    for _, s in ipairs(P.list(dim)) do
        if AXES[s.axis] and inside(s, x, y, z) then
            local def = P.defs[s.name]
            if def then dimensions.run_in(dim, function() fill(def, s, "air") end) end
        end
    end
    forget_at(dim, x, y, z)
end

-- ===========================================================================
-- Trips
-- ===========================================================================
local function dtype(d) return dimensions.get(d) or {} end
local function scale_of(d) return tonumber(dtype(d).coordinate_scale) or 1 end
local function height_of(d) return tonumber(dtype(d).logical_height) or 256 end

-- Where a trip aims in the other dimension (trip.aim_x/y/z)
local function aim(def, trip)
    local dest = def.destination
    local x, y, z
    if type(dest) == "function" then
        x, y, z = dest(trip)
    elseif dest == "same" then
        x, y, z = trip.x, trip.y, trip.z
    elseif dest == "spawn" then
        local sp = dtype(trip.to).spawn
        x, y, z = sp and sp.x or 0.5, sp and sp.y or 80, sp and sp.z or 0.5
    else   -- "scaled"
        local k = tonumber(def.scale)
        if k and trip.from == def.to then k = 1 / k end
        k = k or (scale_of(trip.from) / scale_of(trip.to))
        x, y, z = trip.x * k, trip.y, trip.z * k
    end
    if not x then return false end
    local limit = 29999984
    trip.aim_x = math.max(-limit, math.min(limit, x))
    trip.aim_z = math.max(-limit, math.min(limit, z))
    trip.aim_y = math.max(1, math.min(height_of(trip.to) - 1, y))
    return true
end

-- portals.find(dimension, x, y, z, radius [, name]) -> the nearest portal there, checked
function P.find(dim, x, y, z, radius, name)
    dim = dim_name(dim)
    local best, bestd
    for _, p in ipairs(P.list(dim)) do
        if (not name or p.name == name) and AXES[p.axis] then
            local cx, _, cz = at(p, p.w / 2, 0)
            local d = math.max(math.abs(cx - x), math.abs(cz - z))
            if d <= radius then
                local dist = (cx - x) ^ 2 + (cz - z) ^ 2 + ((p.y - y) ^ 2) * 0.01
                if not bestd or dist < bestd then best, bestd = p, dist end
            end
        end
    end
    if not best then return nil end
    dimensions.load_area(dim, best.x, best.z, 8)
    local def = P.defs[best.name]
    local ok = dimensions.run_in(dim, function() return def and block_at(best.x, best.y, best.z) == def.block end)
    if not ok then   -- broken while nobody was near
        forget_at(dim, best.x, best.y, best.z)
        return P.find(dim, x, y, z, radius, name)
    end
    return best
end

-- The built-in builder: the kind's smallest size, standing the same way as the portal you came
-- through (then the other way), on solid ground with room within build_radius, else on a platform
local function solid_at(x, y, z)
    local n = block_at(x, y, z)
    if n == "air" or n == "fire" then return false end
    return not (engine.get_fluid and engine.get_fluid(x, y, z))
end
local function default_build(def, trip)
    local x, y, z = floor(trip.aim_x), floor(trip.aim_y), floor(trip.aim_z)
    local w, h = def.min_size[1], def.min_size[2]
    local top = height_of(trip.to) - 5
    local axes = {}
    if trip.axis then axes[1] = trip.axis end
    for _, a in ipairs(def.orientations) do if a ~= trip.axis then axes[#axes + 1] = a end end
    local function room(axis, px, py, pz)
        if axis == "y" then return false end   -- (flat ones: always at the aim, below)
        local s = { x = px, y = py, z = pz, axis = axis, w = w, h = h }
        local n = AXES[axis].n
        for i = -1, w do
            for k = -1, 1 do
                local bx, by, bz = at(s, i, 0)
                bx, bz = bx + n[1] * k, bz + n[3] * k
                if not solid_at(bx, by - 1, bz) then return false end
                for j = 0, h + 1 do if block_at(bx, by + j, bz) ~= "air" then return false end end
            end
        end
        return true
    end
    local spot
    for _, axis in ipairs(axes) do
        for r = 0, def.build_radius do
            for ix = -r, r do
                for iz = -r, r do
                    if math.max(math.abs(ix), math.abs(iz)) == r then
                        for py = top, 2, -1 do
                            local px, pz = x + ix, z + iz
                            if block_at(px, py, pz) == "air" and solid_at(px, py - 1, pz) then
                                if room(axis, px, py, pz) then
                                    local d = ix * ix + iz * iz + (py - y) ^ 2 * 0.1
                                    if not spot or d < spot.d then spot = { x = px, y = py, z = pz, axis = axis, d = d } end
                                end
                                break
                            end
                        end
                    end
                end
            end
            if spot then break end
        end
        if spot then break end
    end
    if not spot then
        -- a platform of frame blocks with room above (y kept between 70 and the logical height - 10)
        local axis = axes[1] or "x"
        local py = math.max(math.min(70, top - 5), math.min(y, top - 5))
        spot = { x = x, y = py, z = z, axis = axis }
        if axis ~= "y" and def.frame then
            local s = { x = x, y = py, z = z, axis = axis, w = w, h = h }
            local n = AXES[axis].n
            for i = -1, w do
                for k = -1, 1 do
                    local bx, by, bz = at(s, i, 0)
                    bx, bz = bx + n[1] * k, bz + n[3] * k
                    engine.set_block(bx, by - 1, bz, def.frame)
                    for j = 0, h + 1 do engine.set_block(bx, by + j, bz, "air") end
                end
            end
        end
    end
    local s = P.create(def.name, trip.to, spot.x, spot.y, spot.z, spot.axis, w, h)
    return s
end

local function arrival_of(s)
    local x, y, z = at(s, (s.w - 1) / 2, 0)
    if s.axis == "y" then return x + 0.5, y + 1, z + (s.h - 1) / 2 + 0.5 end
    if s.axis == "x" then return x + 0.5, y, z + 0.5 end
    return x + 0.5, y, z + 0.5
end

-- Works out where a trip ends: true, with trip.to / aim set and trip.arrive_x/y/z
local function plan(def, trip)
    if not dimensions.exists(trip.to) then return false end
    if not aim(def, trip) then return false end
    local mode = def.arrive
    if type(mode) == "function" then
        local x, y, z = mode(trip)
        if not x then return false end
        trip.arrive_x, trip.arrive_y, trip.arrive_z = x, y, z
        return true
    end
    if mode ~= "exact" then
        local radius = def.search_radius
        if radius == "scaled" then radius = math.max(16, floor(128 / scale_of(trip.to))) end
        local found = P.find(trip.to, trip.aim_x, trip.aim_y, trip.aim_z, tonumber(radius) or 16, def.name)
        if not found and mode == "find_or_build" and def.build then
            dimensions.load_area(trip.to, trip.aim_x, trip.aim_z, (def.build_radius or 16) + 4)
            if type(def.build) == "function" then
                found = dimensions.run_in(trip.to, function() return def.build(trip) end)
            else
                found = dimensions.run_in(trip.to, function() return default_build(def, trip) end)
            end
        end
        if found then
            trip.arrive_x, trip.arrive_y, trip.arrive_z = arrival_of(found)
            return true
        end
    end
    trip.arrive_x, trip.arrive_y, trip.arrive_z = trip.aim_x, trip.aim_y, trip.aim_z
    return true
end

local function other_side(def, from)
    if from == def.from then return def.to end
    if from == def.to and not def.one_way then return def.from end
    return nil
end

-- The portal kind at a block in this dimension, and which way it faces
local function portal_at(x, y, z)
    x, y, z = floor(x), floor(y), floor(z)
    local n = block_at(x, y, z)
    for _, def in pairs(P.defs) do
        if def.block == n then
            local meta = engine.get_metadata and engine.get_metadata(x, y, z) or 0
            return def, (meta == 2 and "y") or (meta == 1 and "z") or "x"
        end
    end
    return nil
end

local standing = {}      -- player -> { time, ready (stepped out since the last trip), cooldown }

-- portals.travel(player, name) -> true: sends a player through a portal of that kind now
function P.travel(player, name)
    local def = P.defs[name]
    local info = engine.get_player_info and engine.get_player_info(player)
    if not def or not info then return false end
    local from = dim_name(info.dimension)
    local to = other_side(def, from)
    if not to then return false end
    local found = dimensions.run_in(from, function()
        local d, a = portal_at(info.x, info.y + 0.2, info.z)
        if not d then d, a = portal_at(info.x, info.y + 1.2, info.z) end
        return { d, a }
    end)
    local trip = { name = name, player = player, from = from, to = to, x = info.x, y = info.y, z = info.z,
                   axis = found and found[2] }
    if def.can_use and def.can_use(player, trip) == false then return false end
    if not plan(def, trip) then return false end
    local ok = engine.set_player_dimension(player, to, trip.arrive_x, trip.arrive_y, trip.arrive_z)
    if ok then
        -- (arriving in a portal: it waits until they step out, or for the cooldown)
        standing[player] = { time = 0, ready = false, cooldown = def.cooldown }
        if def.on_travel then pcall(def.on_travel, player, trip) end
        if on_portal_travel then pcall(on_portal_travel, player, from, to, name) end
    end
    return ok
end

-- ===========================================================================
-- Every tick, where the world runs: players and mobs in portals
-- ===========================================================================
local mob_cooldown = {}
local mob_timer = 0

local prev_update = on_update
function on_update(dt, ...)
    if next(P.defs) and is_authority() and engine.get_online_players then
        dt = tonumber(dt) or 0
        for _, p in ipairs(engine.get_online_players() or {}) do
            local dim = dim_name(p.dimension)
            local found = dimensions.run_in(dim, function()
                local d = portal_at(p.x, p.y + 0.2, p.z) or portal_at(p.x, p.y + 1.2, p.z)
                if not d then d = portal_at(p.x, p.y - 0.5, p.z) end   -- (flat ones: standing on it)
                return d and { d } or nil
            end)
            local def = found and found[1]
            local st = standing[p.name] or { time = 0, ready = true, cooldown = 0 }
            standing[p.name] = st
            st.cooldown = math.max(0, st.cooldown - dt)
            if def and not other_side(def, dim) then def = nil end
            if not def then
                st.time, st.ready = 0, true
            elseif (st.ready or not def.step_out) and st.cooldown <= 0 then
                st.time = st.time + dt
                local need = (p.mode == "creative") and def.creative_delay or def.delay
                if st.time >= need then
                    st.time, st.ready, st.cooldown = 0, false, def.cooldown
                    P.travel(p.name, def.name)
                end
            end
        end
        mob_timer = mob_timer + dt
        if mob_timer >= 0.5 and entities and entities.get_all then
            local step = mob_timer
            mob_timer = 0
            for id, left in pairs(mob_cooldown) do mob_cooldown[id] = left > step and left - step or nil end
            for _, e in ipairs(entities.get_all()) do
                if e.get_dimension and not mob_cooldown[e.id] then
                    local dim = e:get_dimension()
                    local x, y, z = e:get_pos()
                    if x then
                        local found = dimensions.run_in(dim, function() return { portal_at(x, y + 0.2, z) } end)
                        local def, axis = found and found[1], found and found[2]
                        local to = def and def.mobs and other_side(def, dim)
                        if to then
                            local trip = { name = def.name, entity = e, from = dim, to = to, x = x, y = y, z = z, axis = axis }
                            if (not def.can_use or def.can_use(nil, trip) ~= false) and plan(def, trip) then
                                if e:set_dimension(to, trip.arrive_x, trip.arrive_y, trip.arrive_z) then
                                    mob_cooldown[e.id] = def.mob_cooldown
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    if prev_update then return prev_update(dt, ...) end
end

-- Lighting, asked by a player's game (the igniter items)
local prev_msg = on_mod_message
function on_mod_message(name, channel, data, ...)
    if channel == "portals:light" then
        local x, y, z, kind = tostring(data):match("^(%-?%d+) (%-?%d+) (%-?%d+) ?(%S*)$")
        x, y, z = tonumber(x), tonumber(y), tonumber(z)
        local info = engine.get_player_info and engine.get_player_info(name)
        if x and info and (x - info.x) ^ 2 + (y - info.y) ^ 2 + (z - info.z) ^ 2 < 10 * 10 then
            P.light(x, y, z, kind ~= "" and kind or nil)
        end
        return
    end
    if prev_msg then return prev_msg(name, channel, data, ...) end
end

-- Breaking a frame or portal block breaks the portal (when its frame isn't whole any more)
local function check_around(x, y, z)
    local here = dim_name(dimensions.current())
    for _, s in ipairs(P.list(here)) do
        local def = P.defs[s.name]
        if def and AXES[s.axis] then
            -- is x, y, z in or around it?
            local near = false
            for i = -1, s.w do
                for j = -1, s.h do
                    local cx, cy, cz = at(s, i, j)
                    if cx == x and cy == y and cz == z then near = true end
                end
            end
            if near then
                local cx, cy, cz = at(s, 0, 0)
                local whole = def.frames and shape_on(def, cx, cy, cz, s.axis)
                local still = whole and whole.x == s.x and whole.y == s.y and whole.z == s.z and whole.w == s.w and whole.h == s.h
                if not def.frames then still = block_at(x, y, z) == def.block or not inside(s, x, y, z) end
                if not still then
                    fill(def, s, "air")
                    forget_at(here, cx, cy, cz)
                end
            end
        end
    end
end

local prev_broken = on_block_broken
function on_block_broken(x, y, z, block, ...)
    if is_authority() then
        for _, def in pairs(P.defs) do
            if block == def.block or (def.frame_set and def.frame_set[block]) then check_around(x, y, z) break end
        end
    end
    if prev_broken then return prev_broken(x, y, z, block, ...) end
end
