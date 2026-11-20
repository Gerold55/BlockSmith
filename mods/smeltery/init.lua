-- Smeltery: melt ores and metal into liquid layers, alloy them, and cast them - like Tinkers'
-- Construct's smeltery. An example of what the engine's "tank" blocks can do.
--
-- Build it:
--   * a floor of Seared Bricks, and walls of Seared Bricks (or Seared Glass) around an empty
--     space - anything from 1x1 to 7x7 inside, and as tall as you like (up to 8);
--   * a Smeltery Controller in one of the walls (it lights up when the smeltery is working);
--   * fuel: a lava tank (the tanks mod) set into a wall, or right-click the controller with a
--     lava bucket;
--   * a Smeltery Drain in a wall, a Faucet on the outside of the drain, and a Casting Table or
--     Casting Basin under the faucet.
-- Use it:
--   * right-click the controller holding ore, ingots or metal blocks to put them in (sneak:
--     the whole stack); with an empty hand, its screen: what's melting, the liquid layers and
--     the fuel. Each block of the inside holds one item while it melts and 288 mB of metal.
--   * the metals settle in LAYERS, in the order they melted, and you see them inside; click a
--     layer on the screen to send it to the bottom - the bottom layer is what the drain pours.
--   * copper and gold together make rose gold (3 : 1), on their own.
--   * right-click the faucet: it pours the bottom layer into the table (an ingot, 144 mB) or
--     basin (a block, 1296 mB) below it; it cools solid, then right-click to take it.
--
-- How the layers are drawn: the inside of the smeltery is filled with "tank" blocks with no
-- shell (tank.shell = false). A block holds one liquid, or two in layers (tank.fluid2, with
-- both heights in its metadata) - see docs/MODDING.md, "Liquids in tanks".
-- It runs where the world is decided: singleplayer, or the host of a LAN game.

smeltery = smeltery or {}
local kit = require("ui_kit")
local MOD = "smeltery"

local CELL_MB, INGOT_MB, BLOCK_MB = 288, 144, 1296
local MAX_W, MAX_H = 7, 8
local TICK = 0.25

local METALS = {
    { id = "iron",      name = "Iron",      color = { 0.95, 0.42, 0.18 }, ore = true },
    { id = "gold",      name = "Gold",      color = { 1.00, 0.80, 0.20 }, ore = true },
    { id = "copper",    name = "Copper",    color = { 1.00, 0.52, 0.24 }, ore = true },
    { id = "rose_gold", name = "Rose Gold", color = { 1.00, 0.66, 0.58 }, ore = false },
}
local METAL = {}
for _, m in ipairs(METALS) do METAL[m.id] = m end

-- What melts into what: item -> { metal, mB, seconds }
local MELTS = {}

local function S(name) return "smeltery_" .. name end
local function block(name, tex, opts) engine.register_block(S(name), 1, 1, 1, tex, nil, opts or {}) end

-- ---------------------------------------------------------------------------
-- Blocks and items
-- ---------------------------------------------------------------------------
block("seared_bricks", "smeltery_seared_bricks.png", { hardness = 3, tool = "pickaxe" })
block("seared_glass", "smeltery_seared_glass.png", { hardness = 2, tool = "pickaxe", transparent = true })
block("controller", "smeltery_controller.png", { hardness = 3, tool = "pickaxe" })
block("controller_lit", "smeltery_controller_lit.png", {
    hardness = 3, tool = "pickaxe", in_creative = false, drops = S("controller"), light = 10, light_color = "#ff9040",
})
block("drain", "smeltery_drain.png", { hardness = 3, tool = "pickaxe" })

local FAUCET_BOX = { 5, 11, 5, 11, 15, 11 }
block("faucet", "smeltery_faucet.png", {
    pattern = "tank", tank = { box = FAUCET_BOX, inset = 0 }, walkable = false, hardness = 1, tool = "pickaxe",
})
local TABLE = { top = "smeltery_table_top.png", sides = "smeltery_table_side.png", bottom = "smeltery_table_side.png" }
local BASIN = { top = "smeltery_basin_top.png", sides = "smeltery_basin_side.png", bottom = "smeltery_basin_side.png" }
local TABLE_TANK = { box = { 0, 0, 0, 16, 16, 16 }, inset = 1, floor = 13 }   -- a shallow ingot mould
local BASIN_TANK = { box = { 0, 0, 0, 16, 16, 16 }, inset = 2, floor = 2 }    -- a deep block mould
block("table", TABLE, { pattern = "tank", tank = TABLE_TANK, hardness = 2, tool = "pickaxe" })
block("basin", BASIN, { pattern = "tank", tank = BASIN_TANK, hardness = 2, tool = "pickaxe" })

local function with(t, extra) local c = {} for k, v in pairs(t) do c[k] = v end for k, v in pairs(extra) do c[k] = v end return c end

for _, m in ipairs(METALS) do
    local molten = S("molten_" .. m.id)
    -- the liquid's picture (never placed: tanks and layers use it)
    block("molten_" .. m.id, "smeltery_molten_" .. m.id .. ".png", { in_creative = false, light = 12, light_color = "#ff9a50" })
    block(m.id .. "_block", "smeltery_" .. m.id .. "_block.png", { hardness = 5, tool = "pickaxe" })
    block(m.id .. "_ingot", "smeltery_" .. m.id .. "_ingot.png", { inventory_image = "smeltery_" .. m.id .. "_ingot.png" })
    if m.ore then block(m.id .. "_ore", "smeltery_" .. m.id .. "_ore.png", { hardness = 3, tool = "pickaxe" }) end
    MELTS[S(m.id .. "_ingot")] = { m.id, INGOT_MB, 3 }
    MELTS[S(m.id .. "_block")] = { m.id, BLOCK_MB, 9 }
    if m.ore then MELTS[S(m.id .. "_ore")] = { m.id, INGOT_MB * 2, 5 } end   -- ore doubling, as in Tinkers

    -- the inside of the smeltery: one layer...
    block("layer_" .. m.id, "smeltery_seared_glass.png", {
        pattern = "tank", tank = { fluid = molten, box = { 0, 0, 0, 16, 16, 16 }, inset = 0, floor = 0, shell = false },
        walkable = false, selectable = false, in_creative = false, drops = "", light = 12, light_color = "#ff9a50",
    })
    -- the faucet pouring it, and the table / basin holding it (liquid, then cast solid)
    block("faucet_" .. m.id, "smeltery_faucet.png", {
        pattern = "tank", tank = { fluid = molten, box = FAUCET_BOX, inset = 0, stream = true },
        walkable = false, in_creative = false, drops = S("faucet"), light = 8, light_color = "#ff9a50",
    })
    block("table_" .. m.id, TABLE, { pattern = "tank", tank = with(TABLE_TANK, { fluid = molten }),
        in_creative = false, drops = S("table"), light = 8, light_color = "#ff9a50" })
    block("table_" .. m.id .. "_cast", TABLE, { pattern = "tank", tank = with(TABLE_TANK, { fluid = S(m.id .. "_block") }),
        in_creative = false, drops = S("table") })
    block("basin_" .. m.id, BASIN, { pattern = "tank", tank = with(BASIN_TANK, { fluid = molten }),
        in_creative = false, drops = S("basin"), light = 10, light_color = "#ff9a50" })
    block("basin_" .. m.id .. "_cast", BASIN, { pattern = "tank", tank = with(BASIN_TANK, { fluid = S(m.id .. "_block") }),
        in_creative = false, drops = S("basin") })
end
-- ...or two layers in one block, where one metal ends and the next begins
for _, a in ipairs(METALS) do
    for _, b in ipairs(METALS) do
        if a ~= b then
            block("layer_" .. a.id .. "__" .. b.id, "smeltery_seared_glass.png", {
                pattern = "tank",
                tank = { fluid = S("molten_" .. a.id), fluid2 = S("molten_" .. b.id), box = { 0, 0, 0, 16, 16, 16 },
                         inset = 0, floor = 0, shell = false },
                walkable = false, selectable = false, in_creative = false, drops = "", light = 12, light_color = "#ff9a50",
            })
        end
    end
end

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local function authority() return not engine.is_authority or engine.is_authority() end
local function say(t) if chat and chat.send then chat.send(t) end end
local function key(x, y, z) return x .. "," .. y .. "," .. z end
local function name_at(x, y, z) return engine.get_block_name(x, y, z) end
local function starts(s, p) return s:sub(1, #p) == p end
local function is_interior(n) return n == "air" or starts(n, S("layer_")) end
local function is_controller(n) return n == S("controller") or n == S("controller_lit") end
local function is_wall(n)
    return n == S("seared_bricks") or n == S("seared_glass") or n == S("drain") or is_controller(n)
        or starts(n, "tanks_tank")                                   -- fuel tanks (the tanks mod)
end
local function mb_text(mb)
    local ingots = mb / INGOT_MB
    if ingots >= 1 and mb % INGOT_MB == 0 then return string.format("%d mB (%d ingot%s)", mb, ingots, ingots == 1 and "" or "s") end
    return mb .. " mB"
end

-- ---------------------------------------------------------------------------
-- Smelteries: state per controller, saved with the world
-- ---------------------------------------------------------------------------
local smelteries = {}   -- controller key -> state
local casts = {}        -- table/basin key -> { metal, mb, cool }
local pours = {}        -- faucet key -> { ... }

local function save_one(s)
    local layers, items = {}, {}
    for _, l in ipairs(s.layers) do layers[#layers + 1] = l[1] .. "=" .. l[2] end
    for _, it in ipairs(s.items) do items[#items + 1] = it.item .. "=" .. string.format("%.3f", it.progress) end
    engine.set_storage(MOD .. ":s:" .. s.key, table.concat(layers, ";") .. "|" .. table.concat(items, ";") .. "|" .. s.fuel)
end
local function save_list()
    local keys = {}
    for k in pairs(smelteries) do keys[#keys + 1] = k end
    engine.set_storage(MOD .. ":list", table.concat(keys, " "))
    local c = {}
    for k, v in pairs(casts) do c[#c + 1] = k .. "=" .. v.metal .. ":" .. v.mb .. ":" .. string.format("%.2f", v.cool or 0) .. ":" .. v.kind end
    engine.set_storage(MOD .. ":casts", table.concat(c, " "))
end

local function new_state(x, y, z)
    return { key = key(x, y, z), x = x, y = y, z = z, layers = {}, items = {}, fuel = 0, valid = false,
             check = 0, shown = {}, dirty = false }
end

local function load_all()
    if not engine.get_storage then return end
    for k in (engine.get_storage(MOD .. ":list") or ""):gmatch("%S+") do
        local x, y, z = k:match("^(%-?%d+),(%-?%d+),(%-?%d+)$")
        if x then
            local s = new_state(tonumber(x), tonumber(y), tonumber(z))
            local data = engine.get_storage(MOD .. ":s:" .. k) or ""
            local layers, items, fuel = data:match("^([^|]*)|([^|]*)|(%d+)$")
            for m, mb in (layers or ""):gmatch("([%w_]+)=(%d+)") do if METAL[m] then s.layers[#s.layers + 1] = { m, tonumber(mb) } end end
            for item, p in (items or ""):gmatch("([%w_]+)=([%d%.]+)") do
                if MELTS[item] then s.items[#s.items + 1] = { item = item, progress = tonumber(p) } end
            end
            s.fuel = tonumber(fuel) or 0
            smelteries[k] = s
        end
    end
    for k, m, mb, cool, kind in (engine.get_storage(MOD .. ":casts") or ""):gmatch("(%S+)=([%w_]+):(%d+):([%d%.]+):(%a+)") do
        if METAL[m] then casts[k] = { metal = m, mb = tonumber(mb), cool = tonumber(cool), kind = kind } end
    end
end

-- The structure around a controller: the inside (x0..x1, y0..y1, z0..z1), its drains and fuel
-- tanks; nil if it isn't a proper smeltery
local function scan(cx, cy, cz)
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local sx, sz = cx + d[1], cz + d[2]
        if is_interior(name_at(sx, cy, sz)) then
            local y0 = cy
            while cy - y0 < MAX_H and is_interior(name_at(sx, y0 - 1, sz)) do y0 = y0 - 1 end
            local x0, x1, z0, z1 = sx, sx, sz, sz
            while sx - x0 < MAX_W and is_interior(name_at(x0 - 1, y0, sz)) do x0 = x0 - 1 end
            while x1 - x0 + 1 < MAX_W and is_interior(name_at(x1 + 1, y0, sz)) do x1 = x1 + 1 end
            while sz - z0 < MAX_W and is_interior(name_at(sx, y0, z0 - 1)) do z0 = z0 - 1 end
            while z1 - z0 + 1 < MAX_W and is_interior(name_at(sx, y0, z1 + 1)) do z1 = z1 + 1 end
            local function level_ok(y, floor_check)
                for x = x0, x1 do for z = z0, z1 do
                    if not is_interior(name_at(x, y, z)) then return false end
                    if floor_check and name_at(x, y - 1, z) ~= S("seared_bricks") then return false end
                end end
                for x = x0, x1 do
                    if not is_wall(name_at(x, y, z0 - 1)) or not is_wall(name_at(x, y, z1 + 1)) then return false end
                end
                for z = z0, z1 do
                    if not is_wall(name_at(x0 - 1, y, z)) or not is_wall(name_at(x1 + 1, y, z)) then return false end
                end
                return true
            end
            if level_ok(y0, true) then
                local y1 = y0
                while y1 - y0 + 1 < MAX_H and level_ok(y1 + 1, false) do y1 = y1 + 1 end
                if cy >= y0 and cy <= y1 then
                    local st = { x0 = x0, x1 = x1, z0 = z0, z1 = z1, y0 = y0, y1 = y1, drains = {}, tanks = {} }
                    for y = y0, y1 do
                        local ring = {}
                        for x = x0, x1 do ring[#ring + 1] = { x, z0 - 1 }; ring[#ring + 1] = { x, z1 + 1 } end
                        for z = z0, z1 do ring[#ring + 1] = { x0 - 1, z }; ring[#ring + 1] = { x1 + 1, z } end
                        for _, p in ipairs(ring) do
                            local n = name_at(p[1], y, p[2])
                            if n == S("drain") then st.drains[key(p[1], y, p[2])] = true
                            elseif starts(n, "tanks_tank") then st.tanks[#st.tanks + 1] = { p[1], y, p[2] } end
                        end
                    end
                    return st
                end
            end
        end
    end
    return nil
end

local function totals(s)
    local total = 0
    for _, l in ipairs(s.layers) do total = total + l[2] end
    return total
end
local function capacity(s)
    if not s.st then return 0 end
    return (s.st.x1 - s.st.x0 + 1) * (s.st.z1 - s.st.z0 + 1) * (s.st.y1 - s.st.y0 + 1) * CELL_MB
end
local function slots(s)
    if not s.st then return 0 end
    return (s.st.x1 - s.st.x0 + 1) * (s.st.z1 - s.st.z0 + 1) * (s.st.y1 - s.st.y0 + 1)
end
local function add_metal(s, metal, mb)
    for _, l in ipairs(s.layers) do if l[1] == metal then l[2] = l[2] + mb return end end
    s.layers[#s.layers + 1] = { metal, mb }                 -- a new metal floats on top
end
local function take_bottom(s, mb)
    local l = s.layers[1]
    if not l then return nil, 0 end
    local got = math.min(mb, l[2])
    l[2] = l[2] - got
    local metal = l[1]
    if l[2] <= 0 then table.remove(s.layers, 1) end
    return metal, got
end

-- Fuel: lava in the controller itself, then in the wall's lava tanks
local function fuel_amount(s)
    local f = s.fuel
    if s.st and tanks and tanks.get then
        for _, t in ipairs(s.st.tanks) do
            local fl, mb = tanks.get(t[1], t[2], t[3])
            if fl == "lava" then f = f + mb end
        end
    end
    return f
end
local function burn(s, mb)
    if s.fuel >= mb then s.fuel = s.fuel - mb return true end
    if s.st and tanks and tanks.get then
        for _, t in ipairs(s.st.tanks) do
            local fl, have = tanks.get(t[1], t[2], t[3])
            if fl == "lava" and have > 0 then
                -- tanks hold 250 mB steps: use a step, keep the change in the controller
                tanks.set(t[1], t[2], t[3], "lava", have - 250)
                s.fuel = s.fuel + 250 - mb
                return true
            end
        end
    end
    return false
end

-- ---------------------------------------------------------------------------
-- Drawing the layers: the inside gets one "tank" block per cell, every column the same
-- ---------------------------------------------------------------------------
local function render(s, clear)
    local st = s.st or s.last_st
    if not st then return end
    local area = (st.x1 - st.x0 + 1) * (st.z1 - st.z0 + 1)
    -- each layer's height in 16ths of a block (at least 1, so even a little shows)
    local segments, h = {}, 0
    if not clear then
        for _, l in ipairs(s.layers) do
            local px = math.max(1, math.floor(l[2] * 16 / (area * CELL_MB) + 0.5))
            segments[#segments + 1] = { l[1], h, h + px }
            h = h + px
        end
    end
    for y = st.y0, st.y1 do
        local base = (y - st.y0) * 16
        local inside = {}
        for _, sg in ipairs(segments) do
            if sg[3] > base and sg[2] < base + 16 then inside[#inside + 1] = sg end
        end
        local want, meta = "air", 0
        if #inside == 1 or (#inside >= 1 and inside[1][2] > base) then
            -- one liquid in this cell (from its floor up)
            local sg = inside[1]
            if sg[2] <= base then want, meta = S("layer_" .. sg[1]), math.min(16, sg[3] - base) end
        elseif #inside >= 2 then
            -- two layers meet in this block: the bottom one, then the next one up. A block shows at
            -- most two, so if more meet here, the second is the one that fills most of the rest
            -- (a sliver of a third at the top waits for the block above)
            local low, high, best = inside[1], inside[2], -1
            for i = 2, #inside do
                local sg = inside[i]
                local cover = math.min(sg[3], base + 16) - math.max(sg[2], base)
                if cover > best then best, high = cover, sg end
            end
            local lowTop = math.max(1, math.min(15, low[3] - base))
            local highTop = math.max(lowTop + 1, math.min(16, inside[#inside][3] - base))
            want, meta = S("layer_" .. low[1] .. "__" .. high[1]), lowTop + 16 * (highTop - 1)
        end
        local tag = want .. "#" .. meta
        if s.shown[y] ~= tag or clear then
            s.shown[y] = tag
            for x = st.x0, st.x1 do for z = st.z0, st.z1 do
                local cur = name_at(x, y, z)
                if is_interior(cur) and (cur ~= want or engine.get_metadata(x, y, z) ~= meta) then
                    if want == "air" then engine.set_block(x, y, z, "air") else engine.set_block_meta(x, y, z, want, meta) end
                end
            end end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Every tick: check the structure, burn fuel, melt, alloy, cool casts, draw
-- ---------------------------------------------------------------------------
local function alloy(s)
    -- rose gold: 3 copper + 1 gold -> 4 rose gold (Tinkers' ratio), a little at a time
    for _ = 1, 4 do
        local cu, au
        for _, l in ipairs(s.layers) do
            if l[1] == "copper" then cu = l elseif l[1] == "gold" then au = l end
        end
        if not (cu and au and cu[2] >= 36 and au[2] >= 12) then return end
        cu[2], au[2] = cu[2] - 36, au[2] - 12
        add_metal(s, "rose_gold", 48)
        for i = #s.layers, 1, -1 do if s.layers[i][2] <= 0 then table.remove(s.layers, i) end end
        s.dirty = true
    end
end

local function forget(s)
    if s.last_st then render(s, true) end
    smelteries[s.key] = nil
    engine.set_storage(MOD .. ":s:" .. s.key, nil)
    save_list()
end

local function tick_smeltery(s, dt)
    local cname = name_at(s.x, s.y, s.z)
    if cname == "air" then return end                        -- (not loaded; broken ones: on_block_broken)
    if not is_controller(cname) then
        forget(s)          -- the controller was replaced: its smeltery is gone (the metal too)
        return
    end
    s.check = s.check - dt
    if s.check <= 0 then
        s.check = 2
        local st = scan(s.x, s.y, s.z)
        if not st and s.st then render(s, true) end          -- broken: empty the inside (kept in the controller)
        s.st = st
        if st then s.last_st = st; s.shown = s.shown or {} end
        if not st then s.shown = {} end
    end
    local working = false
    if s.st then
        local melting = false
        for _, it in ipairs(s.items) do if it.progress < 1 then melting = true break end end
        if melting and burn(s, 2) then
            working = true
            for _, it in ipairs(s.items) do
                if it.progress < 1 then it.progress = math.min(1, it.progress + dt / MELTS[it.item][3]) end
            end
            s.dirty = true
        end
        -- melted items become metal, if there's room
        local cap = capacity(s)
        for i = #s.items, 1, -1 do
            local it = s.items[i]
            if it.progress >= 1 then
                local m = MELTS[it.item]
                if totals(s) + m[2] <= cap then
                    add_metal(s, m[1], m[2])
                    table.remove(s.items, i)
                    s.dirty = true
                end
            end
        end
        alloy(s)
        render(s)
    end
    local want = working and S("controller_lit") or S("controller")
    if cname ~= want then engine.set_block(s.x, s.y, s.z, want) end
    if s.dirty then s.dirty = false; save_one(s) end
end

-- ---------------------------------------------------------------------------
-- Casting: faucets pour the bottom layer into tables (ingots) and basins (blocks)
-- ---------------------------------------------------------------------------
local function cast_kind(n)
    if n == S("table") or (starts(n, S("table_"))) then return "table" end
    if n == S("basin") or (starts(n, S("basin_"))) then return "basin" end
    return nil
end
local function cast_target(kind) return kind == "table" and INGOT_MB or BLOCK_MB end
local function show_cast(x, y, z, c, kind)
    if not c then engine.set_block(x, y, z, S(kind)) return end
    if c.mb >= cast_target(kind) and (c.cool or 0) <= 0 then
        engine.set_block(x, y, z, S(kind .. "_" .. c.metal .. "_cast"))
    else
        engine.set_block_meta(x, y, z, S(kind .. "_" .. c.metal), math.max(1, math.ceil(16 * c.mb / cast_target(kind))))
    end
end

local function smeltery_of_drain(x, y, z)
    local k = key(x, y, z)
    for _, s in pairs(smelteries) do if s.st and s.st.drains[k] then return s end end
    return nil
end

local function find_drain(fx, fy, fz)
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        if name_at(fx + d[1], fy, fz + d[2]) == S("drain") then
            local s = smeltery_of_drain(fx + d[1], fy, fz + d[2])
            if s then return s end
        end
    end
    return nil
end

local function stop_pour(fk, p)
    pours[fk] = nil
    if starts(name_at(p.x, p.y, p.z), S("faucet_")) then engine.set_block(p.x, p.y, p.z, S("faucet")) end
end

local function start_pour(fx, fy, fz)
    local s = find_drain(fx, fy, fz)
    if not s then say("Put the faucet against a Smeltery Drain of a working smeltery.") return end
    local kind = cast_kind(name_at(fx, fy - 1, fz))
    if not kind then say("Put a Casting Table or Casting Basin under the faucet.") return end
    local ck = key(fx, fy - 1, fz)
    local c = casts[ck]
    if c and (c.mb >= cast_target(kind)) then say("Take the cast out first (right-click it).") return end
    local bottom = s.layers[1]
    if not bottom then say("The smeltery is empty.") return end
    if c and c.metal ~= bottom[1] then say("The bottom layer isn't " .. METAL[c.metal].name .. " - click it to the bottom on the smeltery's screen.") return end
    pours[key(fx, fy, fz)] = { x = fx, y = fy, z = fz, s = s, ck = ck, kind = kind, t = 0 }
end

local function tick_pours(dt)
    for fk, p in pairs(pours) do
        p.t = p.t + dt
        local target = cast_target(p.kind)
        local rate = target / 16                              -- a cast fills in about 1.6 s
        while p.t >= 0.1 and pours[fk] do
            p.t = p.t - 0.1
            local c = casts[p.ck]
            local bottom = p.s.layers[1]
            if not bottom or (c and c.metal ~= bottom[1]) or cast_kind(name_at(p.x, p.y - 1, p.z)) ~= p.kind then
                stop_pour(fk, p) break
            end
            local need = target - (c and c.mb or 0)
            local metal, got = take_bottom(p.s, math.min(rate, need))
            c = c or { metal = metal, mb = 0, kind = p.kind }
            c.mb = c.mb + got
            casts[p.ck] = c
            p.s.dirty = true
            local want = S("faucet_" .. metal)
            if name_at(p.x, p.y, p.z) ~= want then engine.set_block(p.x, p.y, p.z, want) end
            if c.mb >= target then c.cool = p.kind == "table" and 2 or 4; stop_pour(fk, p) end
            show_cast(p.x, p.y - 1, p.z, c, p.kind)
            save_list()
        end
    end
    for ck, c in pairs(casts) do
        if c.cool and c.cool > 0 then
            c.cool = c.cool - dt
            if c.cool <= 0 then
                local x, y, z = ck:match("^(%-?%d+),(%-?%d+),(%-?%d+)$")
                x, y, z = tonumber(x), tonumber(y), tonumber(z)
                if cast_kind(name_at(x, y, z)) == c.kind then show_cast(x, y, z, c, c.kind) end
                save_list()
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- The controller's screen
-- ---------------------------------------------------------------------------
local open_key = nil
local screen = kit.form{
    id = MOD .. ":screen", width = 22, height = 13.5, title = "Smeltery",
    on_close = function() open_key = nil end,
    draw = function(f, st)
        local s = smelteries[open_key or ""]
        if not s then f:label(0, 0, "This smeltery is gone.") return end
        if not s.st then
            f:label(0, 0, "Not a complete smeltery:", { color = kit.theme.title })
            f:text(0, 0.9, 21, "It needs a floor of Seared Bricks and walls of Seared Bricks or Seared Glass around an empty space, " ..
                   "with this controller in a wall. Fuel: a lava tank in a wall, or a lava bucket on the controller.",
                   { color = kit.theme.muted })
            return
        end
        local sx, sz, sy = s.st.x1 - s.st.x0 + 1, s.st.z1 - s.st.z0 + 1, s.st.y1 - s.st.y0 + 1
        f:label(0, 0, string.format("%d x %d x %d inside  -  %s of %s", sx, sz, sy, mb_text(totals(s)), capacity(s) .. " mB"),
                { color = kit.theme.muted, size = 0.36 })

        -- left: what's melting
        f:label(0, 0.8, "Melting", { color = kit.theme.title })
        local n = slots(s)
        local cols = 4
        for i = 1, math.min(n, 20) do
            local it = s.items[i]
            local x, y = ((i - 1) % cols) * 1.6, 1.6 + math.floor((i - 1) / cols) * 1.9
            f:item(x, y, 1.4, it and it.item or nil, { id = "slot" .. i,
                tooltip = it and string.format("%s - %d%%", it.item:gsub("^smeltery_", ""):gsub("_", " "), math.floor(it.progress * 100)) or nil })
            if it then
                f:rect(x, y + 1.45, 1.4, 0.2, { 0.1, 0.1, 0.12, 1 })
                f:rect(x, y + 1.45, 1.4 * it.progress, 0.2, it.progress >= 1 and { 0.95, 0.4, 0.3, 1 } or { 0.95, 0.75, 0.3, 1 })
            end
        end
        if n > 20 then f:label(0, 11.3, "(+" .. (n - 20) .. " more slots)", { size = 0.32, color = kit.theme.muted }) end

        -- middle: the tank, layers stacked from the bottom (click one: it goes to the bottom)
        local tx, ty, tw, th = 7.2, 1.0, 8.6, 10.6
        f:label(tx, 0.8 - 0.0, "Liquids", { color = kit.theme.title })
        f:box(tx, ty, tw, th, { 0.08, 0.07, 0.07, 1 }, { 0.3, 0.28, 0.26, 1 })
        local cap = math.max(1, capacity(s))
        local yb = ty + th
        for i, l in ipairs(s.layers) do
            local h = math.max(0.12, th * l[2] / cap)
            yb = yb - h
            local c = METAL[l[1]].color
            local hover = f:hovered(tx, yb, tw, h)
            f:rect(tx, yb, tw, h, { c[1] * (hover and 1 or 0.85), c[2] * (hover and 1 or 0.85), c[3] * (hover and 1 or 0.85), 1 })
            if h >= 0.55 then f:label(tx + 0.25, yb + (h - 0.4) / 2, "Molten " .. METAL[l[1]].name .. "  " .. mb_text(l[2]),
                                      { size = 0.34, color = { 0.1, 0.06, 0.04 } }) end
            f:tooltip(tx, yb, tw, h, "Molten " .. METAL[l[1]].name .. ": " .. mb_text(l[2]) ..
                      (i > 1 and " - click: move to the bottom (the drain pours it first)" or " - the drain pours this first"))
            if i > 1 and f:take_click(tx, yb, tw, h) then
                table.insert(s.layers, 1, table.remove(s.layers, i))
                s.dirty = true
            end
        end
        if #s.layers == 0 then f:label(tx + 0.4, ty + th / 2 - 0.2, "Empty", { color = kit.theme.muted }) end

        -- right: fuel
        local fx = 16.6
        f:label(fx, 0.8, "Fuel", { color = kit.theme.title })
        local fuel = fuel_amount(s)
        local fcap = 4000 + #s.st.tanks * 4000
        f:box(fx, 1.6, 1.2, 10.0, { 0.08, 0.07, 0.07, 1 }, { 0.3, 0.28, 0.26, 1 })
        local fh = 10.0 * math.min(1, fuel / fcap)
        f:rect(fx, 1.6 + 10.0 - fh, 1.2, fh, { 1, 0.45, 0.12, 1 })
        f:tooltip(fx, 1.6, 1.2, 10, "Lava: " .. fuel .. " mB")
        f:text(fx + 1.5, 1.6, 4, fuel > 0 and (fuel .. " mB lava") or "No fuel: a lava tank in a wall, or a lava bucket on the controller",
               { size = 0.32, color = fuel > 0 and kit.theme.text or { 1, 0.5, 0.4 } })
        f:text(fx + 1.5, 5.0, 4, "Ore gives 2 ingots. Copper + gold make rose gold.", { size = 0.3, color = kit.theme.muted })
        f:label(0, 12.6, "Right-click the controller holding ore, ingots or metal blocks to put them in (sneak: the whole stack).",
                { size = 0.3, color = kit.theme.muted })
    end,
}

-- ---------------------------------------------------------------------------
-- Clicks
-- ---------------------------------------------------------------------------
local function state_for(x, y, z)
    local k = key(x, y, z)
    local s = smelteries[k]
    if not s then
        s = new_state(x, y, z)
        smelteries[k] = s
        save_list()
    end
    if not s.st then s.st = scan(x, y, z); if s.st then s.last_st = s.st end end
    return s
end

local previous_click = on_right_click_block
function on_right_click_block(x, y, z, block_name, held, sneaking, repeat_click, ...)
    local mine = is_controller(block_name) or block_name == S("faucet") or starts(block_name, S("faucet_"))
                 or cast_kind(block_name) ~= nil
    if mine and not authority() then
        if not repeat_click then say("Smelteries work in your own world (singleplayer, or when you host).") end
        return true
    end
    if is_controller(block_name) then
        if repeat_click then return true end
        local s = state_for(x, y, z)
        if held and held ~= "" and MELTS[held] then
            if not s.st then say("Not a complete smeltery yet (open it with an empty hand to see why).") return true end
            local slot = hotbar.get_active_slot()
            local name, count = hotbar.get_slot(slot)
            local free = slots(s) - #s.items
            local n = math.min(sneaking and count or 1, free)
            if n <= 0 then say("The smeltery is full of things melting.") return true end
            for _ = 1, n do s.items[#s.items + 1] = { item = held, progress = 0 } end
            if not (engine.is_creative and engine.is_creative()) then
                if count - n > 0 then hotbar.set_slot(slot, name, count - n) else hotbar.set_slot(slot, "", 0) end
            end
            s.dirty = true
            return true
        end
        if held == nil or held == "" then
            open_key = s.key
            screen:open({})
            return true
        end
        say("That doesn't melt. Ores, ingots and metal blocks do.")
        return true
    end
    if block_name == S("faucet") or starts(block_name, S("faucet_")) then
        if repeat_click then return true end
        local fk = key(x, y, z)
        if pours[fk] then stop_pour(fk, pours[fk]) else start_pour(x, y, z) end
        return true
    end
    local kind = cast_kind(block_name)
    if kind and (held == nil or held == "") and block_name ~= S(kind) then
        if repeat_click then return true end
        local ck = key(x, y, z)
        local c = casts[ck]
        if c and c.mb >= cast_target(kind) and (c.cool or 0) <= 0 then
            local item = S(c.metal .. (kind == "table" and "_ingot" or "_block"))
            if engine.give_item(item, 1) then
                casts[ck] = nil
                engine.set_block(x, y, z, S(kind))
                save_list()
            else say("No room for it.") end
        elseif c and c.mb < cast_target(kind) and sneaking then
            casts[ck] = nil                                   -- sneak: tip out a part-filled cast
            engine.set_block(x, y, z, S(kind))
            save_list()
        elseif c and c.mb < cast_target(kind) then
            say(string.format("%d / %d mB of %s - pour more, or sneak + right-click to tip it out.", c.mb,
                              cast_target(kind), METAL[c.metal].name))
        else
            say("Still cooling...")
        end
        return true
    end
    if previous_click then return previous_click(x, y, z, block_name, held, sneaking, repeat_click, ...) end
    return false
end

-- Breaking the controller: the smeltery's gone, and so is the metal in it (as in Tinkers');
-- breaking a table or basin loses what was cast in it
local previous_broken = on_block_broken
function on_block_broken(x, y, z, block_name, ...)
    if is_controller(block_name or "") then
        local s = smelteries[key(x, y, z)]
        if s then forget(s) end
    elseif cast_kind(block_name or "") then
        casts[key(x, y, z)] = nil
        save_list()
    end
    if previous_broken then return previous_broken(x, y, z, block_name, ...) end
end

-- Lava buckets fuel the controller (the buckets mod hands us the click)
if buckets and buckets.register_target then
    buckets.register_target(function(pointed, item, fluid)
        local u = pointed.under
        if not is_controller(u.name) then return false end
        if fluid ~= "lava" then say("The smeltery burns lava.") return true end
        local s = state_for(u.x, u.y, u.z)
        if s.fuel + 1000 > 4000 then say("The controller holds 4000 mB of lava; it's full.") return true end
        if buckets.swap_held(pointed, buckets.EMPTY) then s.fuel = s.fuel + 1000; s.dirty = true end
        return true
    end)
end

local timer = 0
local previous_update = on_update
function on_update(dt, ...)
    if authority() and engine.is_in_game and engine.is_in_game() then
        dt = tonumber(dt) or 0
        timer = timer + dt
        if timer >= TICK then
            local step = timer
            timer = 0
            local list = {}
            for _, s in pairs(smelteries) do list[#list + 1] = s end
            for _, s in ipairs(list) do tick_smeltery(s, step) end
        end
        tick_pours(dt)
    end
    if previous_update then return previous_update(dt, ...) end
end

-- ---------------------------------------------------------------------------
-- For other mods (and the showcase)
-- ---------------------------------------------------------------------------
-- smeltery.add_melting(item, metal, mB, seconds): something else that melts
function smeltery.add_melting(item, metal, mb, seconds) if METAL[metal] then MELTS[item] = { metal, mb, seconds or 4 } end end
-- smeltery.fill(controller x, y, z, { {metal, mB}, ... } [, fuel_mB]): sets what's inside (bottom first)
function smeltery.fill(x, y, z, layers, fuel)
    local s = state_for(x, y, z)
    s.layers = {}
    for _, l in ipairs(layers or {}) do if METAL[l[1]] then s.layers[#s.layers + 1] = { l[1], l[2] } end end
    if fuel then s.fuel = fuel end
    s.dirty = true
    s.check = 0
    return s.st ~= nil
end
-- smeltery.insert(x, y, z, item [, count]): puts items in to melt
function smeltery.insert(x, y, z, item, count)
    local s = state_for(x, y, z)
    if not MELTS[item] then return false end
    for _ = 1, count or 1 do if #s.items < slots(s) then s.items[#s.items + 1] = { item = item, progress = 0 } end end
    s.dirty = true
    return true
end
function smeltery.start_pour(x, y, z) start_pour(x, y, z) end
-- smeltery.open_screen(x, y, z): the controller's screen
function smeltery.open_screen(x, y, z)
    local s = state_for(x, y, z)
    open_key = s.key
    screen:open({})
end
function smeltery.get(x, y, z) local s = smelteries[key(x, y, z)] return s and s.layers, s and s.items end

load_all()

if creative and creative.register_tab then
    local items = { S("seared_bricks"), S("seared_glass"), S("controller"), S("drain"), S("faucet"), S("table"), S("basin") }
    for _, m in ipairs(METALS) do
        if m.ore then items[#items + 1] = S(m.id .. "_ore") end
        items[#items + 1] = S(m.id .. "_ingot")
        items[#items + 1] = S(m.id .. "_block")
    end
    creative.register_tab{ name = "Smeltery", icon = S("controller"), items = items }
end

print("[Smeltery] Ready: build a smeltery of seared bricks (see mods/smeltery/init.lua)")
