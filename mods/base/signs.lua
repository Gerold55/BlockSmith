-- signs.lua: signs any game can have - placed on the ground (turned toward you, in 16 steps) or
-- on a wall, with text on the front and the back, edited in a little screen. Part of the base mod.
--
--   signs.register("mymod:oak_sign", {
--       texture = "oak_planks.png",          -- the board and post
--       inventory_image = "oak_sign.png",    -- the item (default: the texture)
--       description = "Oak Sign",
--       -- optional: max_chars = 20 (per line), lines = 4, edit_on_place = true,
--       --           glow_light = 0 (light the block gives while its text glows)
--   })
--
--   signs.register_style_item("mymod:red_dye", { color = "#b02e26" })   -- colours the text
--   signs.register_style_item("mymod:glow_ink", { glowing = true })     -- makes it glow
--   signs.register_style_item("mymod:honeycomb", { waxed = true })      -- nobody can edit it now
--   (used on a sign by right-clicking it with the item; one is used up outside creative)
--
-- Using it: place the item on the top of a block (a standing sign, facing you) or on a side (on
-- the wall); the editor opens. Right-click a sign to edit the side you're looking at. Sneak to
-- place another block against it instead. A sign whose block underneath (or wall behind) is
-- broken drops as an item.
--
-- Functions: signs.register(name, def), signs.register_style_item(item, style),
--   signs.open_editor(x, y, z [, side]) (this game's player), signs.get_text(x, y, z) / set_text
--   (engine.get_block_text / set_block_text), signs.is_sign(block).
-- Text, checks and syncing are the engine's (engine.get_block_text, edit_block_text,
-- can_edit_text); this file adds the blocks, placing and the screen.

signs = signs or {}
local S = signs
S.defs = S.defs or {}            -- sign item name -> def
S.by_block = S.by_block or {}    -- standing / wall block name -> def
S.style_items = S.style_items or {}

local floor = math.floor

local function is_authority() return not engine.is_authority or engine.is_authority() end

function S.is_sign(block) return S.by_block[block] ~= nil end
function S.get_text(x, y, z) return engine.get_block_text(x, y, z) end
function S.set_text(x, y, z, t) return engine.set_block_text(x, y, z, t) end

function S.register(name, def)
    assert(type(name) == "string" and name ~= "", "signs.register: a name")
    def = def or {}
    def.name = name
    def.wall_name = def.wall_name or (name .. "_wall")
    def.max_chars = tonumber(def.max_chars) or 20
    def.lines = math.max(1, math.min(8, tonumber(def.lines) or 4))
    if def.edit_on_place == nil then def.edit_on_place = true end
    local tex = def.texture or "oak_planks.png"
    local item = def.inventory_image or tex
    local common = { transparent = true, walkable = false, text = true, hardness = def.hardness or 1,
                     tool = def.tool or "axe", description = def.description, light = def.light }
    local standing = {}
    for k, v in pairs(common) do standing[k] = v end
    standing.pattern = "sign"
    standing.inventory_image = item
    local wall = {}
    for k, v in pairs(common) do wall[k] = v end
    wall.pattern = "wall_sign"
    wall.drops = name
    wall.in_creative = false
    engine.register_block(name, 1, 1, 1, tex, nil, standing)
    engine.register_block(def.wall_name, 1, 1, 1, tex, nil, wall)
    S.defs[name] = def
    S.by_block[name] = def
    S.by_block[def.wall_name] = def

    -- Placing: on a block's top (standing, facing you) or side (on the wall)
    engine.register_item_use(name, function(pointed)
        local under, above = pointed.under, pointed.above
        if not under or not above then return false end
        if engine.is_replaceable and not engine.is_replaceable(above.x, above.y, above.z) then return true end
        local dx, dy, dz = above.x - under.x, above.y - under.y, above.z - under.z
        if dy < 0 then return true end                       -- (not under a ceiling)
        local block, meta
        if dy > 0 then
            -- standing: the front toward the player, in 16 steps
            -- (yaw 0 looks along +x, 90 along +z; the front faces back at the player)
            local yaw = engine.get_player_look and engine.get_player_look() or 0
            local back = math.atan(-math.cos(math.rad(yaw)), -math.sin(math.rad(yaw)))
            local rot = floor(math.deg(back) / 22.5 + 0.5) % 16
            block, meta = name, rot
        else
            -- on the wall: the front faces away from the block it's on
            local facing = (dz < 0 and 0) or (dx > 0 and 1) or (dz > 0 and 2) or 3
            block, meta = def.wall_name, facing
        end
        engine.set_block_meta(above.x, above.y, above.z, block, meta)
        if not (engine.is_creative and engine.is_creative()) and pointed.slot and hotbar and hotbar.set_slot then
            if (pointed.count or 1) > 1 then hotbar.set_slot(pointed.slot, name, pointed.count - 1)
            else hotbar.set_slot(pointed.slot, "", 0) end
        end
        if def.edit_on_place then S.open_editor(above.x, above.y, above.z, "front", true) end
        return true
    end)
    return def
end

-- Items that style a sign's text: { color = "#rrggbb" } / { glowing = true|false } / { waxed = true }
function S.register_style_item(item, style)
    assert(type(item) == "string" and type(style) == "table", "signs.register_style_item(item, style)")
    S.style_items[item] = style
end

-- ===========================================================================
-- Which side of a sign you're looking at (its front faces its normal)
-- ===========================================================================
local function front_normal(block, meta)
    local def = S.by_block[block]
    if not def then return 0, 1 end
    if block == def.wall_name then
        local f = meta % 4
        if f == 0 then return 0, -1 elseif f == 1 then return 1, 0 elseif f == 2 then return 0, 1 else return -1, 0 end
    end
    local a = (meta % 16) * math.pi / 8
    return math.sin(a), math.cos(a)
end
local function facing_side(x, y, z)
    local px, _, pz = engine.get_player_position()
    if not px then return "front" end
    local nx, nz = front_normal(engine.get_block_name(x, y, z), engine.get_metadata and engine.get_metadata(x, y, z) or 0)
    return ((px - (x + 0.5)) * nx + (pz - (z + 0.5)) * nz) >= 0 and "front" or "back"
end

-- ===========================================================================
-- The editor (this game's player)
-- ===========================================================================
local editor
local function make_editor()
    local ok, kit = pcall(require, "ui_kit")
    if not ok or not kit then return nil end
    return kit.form{
        width = 12, height = 7.5, title = "Edit sign",
        draw = function(f, st)
            f:label(0, 0, st.side == "back" and "The back of the sign" or "The front of the sign")
            for i = 1, st.count do
                local v = f:field("l" .. i, 0, 0.5 + i * 1.1, 12, 1, st.lines[i] or "", { placeholder = "" })
                if type(v) == "string" then
                    if #v > st.max then v = v:sub(1, st.max) end
                    st.lines[i] = v
                end
            end
            if f:button("done", 8, st.count * 1.1 + 1.9, 4, 1, "Done") then
                local lines = {}
                for i = 1, st.count do lines[i] = st.lines[i] or "" end
                engine.edit_block_text(st.x, st.y, st.z, st.side, lines)
                f:close()
            end
            if f:button("cancel", 0, st.count * 1.1 + 1.9, 4, 1, "Cancel") then f:close() end
        end,
    }
end

-- signs.open_editor(x, y, z [, side]): the screen for writing on that side of a sign
function S.open_editor(x, y, z, side, fresh)
    local def = S.by_block[engine.get_block_name(x, y, z)]
    if not def and not fresh then return false end
    def = def or {}
    side = side or facing_side(x, y, z)
    local t = engine.get_block_text(x, y, z)
    if t and t.waxed then return false end
    local current = t and t[side] and t[side].lines or {}
    local lines = {}
    for i = 1, def.lines or 4 do lines[i] = current[i] or "" end
    editor = editor or make_editor()
    if not editor then return false end
    editor:open({ x = x, y = y, z = z, side = side, lines = lines, count = def.lines or 4, max = def.max_chars or 20 })
    return true
end

-- Right-clicking a sign: a style item styles it; otherwise the side you're facing is edited
local prev_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_, ...)
    if S.by_block[block] and not sneaking and not repeat_ then
        local style = held and S.style_items[held]
        if style then
            if engine.send_to_server then
                engine.send_to_server("signs:style", string.format("%d %d %d %s %s", x, y, z, facing_side(x, y, z), held))
            end
            return true
        end
        S.open_editor(x, y, z)
        return true
    end
    if prev_click then return prev_click(x, y, z, block, held, sneaking, repeat_, ...) end
end

-- Styling, where the world runs: checked (next to them, a sign, not waxed), then one item used up
local prev_msg = on_mod_message
function on_mod_message(name, channel, data, ...)
    if channel == "signs:style" then
        local x, y, z, side, item = tostring(data):match("^(%-?%d+) (%-?%d+) (%-?%d+) (%a+) (%S+)$")
        x, y, z = tonumber(x), tonumber(y), tonumber(z)
        local style = item and S.style_items[item]
        local info = engine.get_player_info and engine.get_player_info(name)
        if not (x and style and info and (side == "front" or side == "back")) then return end
        if (x + 0.5 - info.x) ^ 2 + (y + 0.5 - info.y) ^ 2 + (z + 0.5 - info.z) ^ 2 > 8 * 8 then return end
        if not S.by_block[engine.get_block_name(x, y, z)] then return end
        local t = engine.get_block_text(x, y, z) or { front = { lines = {} }, back = { lines = {} } }
        if t.waxed then return end
        if style.color then t[side].color = style.color end
        if style.glowing ~= nil then t[side].glowing = style.glowing end
        if style.waxed then t.waxed = true end
        engine.set_block_text(x, y, z, t)
        return
    end
    if prev_msg then return prev_msg(name, channel, data, ...) end
end

-- A sign whose support goes drops as its item
local prev_broken = on_block_broken
function on_block_broken(x, y, z, block, ...)
    if is_authority() and next(S.by_block) then
        -- standing sign on top, wall signs on the sides facing away
        local checks = { { 0, 1, 0, false }, { 0, 0, -1, 0 }, { 1, 0, 0, 1 }, { 0, 0, 1, 2 }, { -1, 0, 0, 3 } }
        for _, c in ipairs(checks) do
            local sx, sy, sz = x + c[1], y + c[2], z + c[3]
            local n = engine.get_block_name(sx, sy, sz)
            local def = S.by_block[n]
            if def then
                local attached
                if c[4] == false then attached = (n == def.name)
                else attached = (n == def.wall_name) and ((engine.get_metadata(sx, sy, sz) or 0) % 4 == c[4]) end
                if attached then
                    engine.set_block(sx, sy, sz, "air")
                    if engine.drop_item then engine.drop_item(def.name, 1, sx + 0.5, sy + 0.3, sz + 0.5) end
                end
            end
        end
    end
    if prev_broken then return prev_broken(x, y, z, block, ...) end
end
