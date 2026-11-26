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
-- Clickable signs (servers: rules, teleports, warps...): an operator types a /sign command, then
-- right-clicks a sign to give it that action. Anyone who right-clicks it afterwards runs it
-- (checked where the world runs; the sign's text stays whatever was written on it):
--   /sign message <text>          tells them the text in chat ("|" starts a new line)
--   /sign show <title> | <lines>  opens a window with the text, lines split by "|" (rules, help)
--   /sign teleport <x> <y> <z> [dimension]
--   /sign warp <name>             to a warp made with /sign setwarp <name> (where you stand)
--   /sign <action> <args>         any action a mod registered (signs.register_action)
--   /sign clear  /sign info  /sign edit   remove the action, say what it is, write on the sign
--   /sign setwarp <name>, /sign delwarp <name>, /sign warps, /sign cancel
-- Mods add actions (where the world runs; args is the text after the action's name):
--   signs.register_action("heal", { description = "Heal fully",
--       run = function(player, args, pos) ... end })   -- return a message for them, or nothing
--   signs.set_action(x, y, z, action, args [, dimension]) / signs.get_action(x, y, z [, dimension])
-- Actions are kept with the world (engine.set_storage) and dropped when the sign is broken.
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
        -- Where the world runs decides: an action sign runs its action, a sign an operator is
        -- giving an action gets it, any other sign opens the editor (it answers "signs:edit")
        if engine.send_to_server then
            engine.send_to_server("signs:click", string.format("%d %d %d %s", x, y, z, facing_side(x, y, z)))
        else
            S.open_editor(x, y, z)
        end
        return true
    end
    if prev_click then return prev_click(x, y, z, block, held, sneaking, repeat_, ...) end
    return false
end

-- Styling, where the world runs: checked (next to them, a sign, not waxed), then one item used up
local prev_msg = on_mod_message
function on_mod_message(name, channel, data, ...)
    if channel == "signs:click" then
        S._click(name, tostring(data))
        return
    end
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

-- ===========================================================================
-- Clickable signs: actions
-- ===========================================================================
S.actions = S.actions or {}           -- action name -> { description, usage, run(player, args, pos) }
local placed = {}                      -- "dim|x|y|z" -> { action = name, args = text }
local warps = {}                       -- name -> { x, y, z, dim }
local pending = {}                     -- operator -> { op = "set" | "clear" | "info" | "edit", action, args }
local last_click = {}                  -- player -> time of their last click (no spamming)
local loaded = false
local clock = 0

local function dim_of_player(name)
    local d = engine.get_player_dimension and engine.get_player_dimension(name)
    return d or "overworld"
end
local function dim_here()
    local d = dimensions and dimensions.current and dimensions.current()
    return (type(d) == "string" and d ~= "") and d or "overworld"
end
local function key(dim, x, y, z) return string.format("%s|%d|%d|%d", dim, x, y, z) end

-- Saved as text: one sign or warp per line, fields split by tabs (tabs and new lines in the
-- text are turned into spaces before saving)
local function clean(t) return (tostring(t or ""):gsub("[\t\r\n]", " ")) end
local function save()
    if not engine.set_storage then return end
    local lines = {}
    for k, a in pairs(placed) do lines[#lines + 1] = k:gsub("|", "\t") .. "\t" .. clean(a.action) .. "\t" .. clean(a.args) end
    table.sort(lines)
    engine.set_storage("signs:actions", table.concat(lines, "\n"))
    local w = {}
    for n, p in pairs(warps) do w[#w + 1] = string.format("%s\t%s\t%.2f\t%.2f\t%.2f", clean(n), p.dim, p.x, p.y, p.z) end
    table.sort(w)
    engine.set_storage("signs:warps", table.concat(w, "\n"))
end
local function load()
    if loaded or not engine.get_storage then return end
    loaded = true
    for line in ((engine.get_storage("signs:actions") or "") .. "\n"):gmatch("(.-)\n") do
        local dim, x, y, z, action, args = line:match("^([^\t]+)\t(%-?%d+)\t(%-?%d+)\t(%-?%d+)\t([^\t]*)\t?(.*)$")
        if dim and action ~= "" then placed[key(dim, tonumber(x), tonumber(y), tonumber(z))] = { action = action, args = args or "" } end
    end
    for line in ((engine.get_storage("signs:warps") or "") .. "\n"):gmatch("(.-)\n") do
        local n, dim, x, y, z = line:match("^([^\t]+)\t([^\t]+)\t(%S+)\t(%S+)\t(%S+)$")
        if n then warps[n] = { dim = dim, x = tonumber(x), y = tonumber(y), z = tonumber(z) } end
    end
end

local function tell(name, text, r, g, b)
    if engine.message_player then engine.message_player(name, text, r or 0.75, g or 0.9, b or 1.0) end
end

function S.register_action(name, def)
    assert(type(name) == "string" and name:match("^[%w_:%-]+$"), "signs.register_action: a name (letters, digits, _ : -)")
    assert(type(def) == "table" and type(def.run) == "function", "signs.register_action(name, { run = function(player, args, pos) })")
    S.actions[name] = def
end
function S.set_action(x, y, z, action, args, dim)
    load()
    local k = key(dim or dim_here(), x, y, z)
    if action == nil or action == "" then placed[k] = nil
    else placed[k] = { action = action, args = tostring(args or "") } end
    save()
    return true
end
function S.get_action(x, y, z, dim)
    load()
    local a = placed[key(dim or dim_here(), x, y, z)]
    if a then return a.action, a.args end
    return nil
end
function S.get_warp(name) load(); return warps[name] end

local function teleport(player, x, y, z, dim)
    if dim and dim ~= dim_of_player(player) and engine.set_player_dimension then
        return engine.set_player_dimension(player, dim, x, y, z)
    end
    return engine.teleport_player and engine.teleport_player(player, x, y, z)
end

-- ---- The actions every game has
S.register_action("message", { description = "Tell them something in chat", usage = "<text> (| = new line)",
    run = function(player, args)
        for line in (args .. "|"):gmatch("(.-)|") do
            line = line:match("^%s*(.-)%s*$")
            if line ~= "" then tell(player, line) end
        end
    end })
S.register_action("show", { description = "Open a window with text (rules, help)", usage = "<title> | <line> | <line>...",
    run = function(player, args)
        if engine.send_to_player then engine.send_to_player(player, "signs:show", args) end
    end })
S.register_action("teleport", { description = "Teleport them", usage = "<x> <y> <z> [dimension]",
    run = function(player, args)
        local x, y, z, dim = args:match("^%s*(%-?[%d%.]+)%s+(%-?[%d%.]+)%s+(%-?[%d%.]+)%s*(%S*)")
        if not x then return "This sign's teleport is broken (it needs x y z)" end
        if not teleport(player, tonumber(x), tonumber(y), tonumber(z), dim ~= "" and dim or nil) then
            return "Couldn't teleport you there"
        end
    end })
S.register_action("warp", { description = "Send them to a warp (/sign setwarp)", usage = "<warp name>",
    run = function(player, args)
        local w = warps[args:match("^%s*(.-)%s*$")]
        if not w then return "That warp doesn't exist any more" end
        if not teleport(player, w.x, w.y, w.z, w.dim) then return "Couldn't take you there" end
        tell(player, "Warped to " .. args:match("^%s*(.-)%s*$"))
    end })

-- ---- A click, where the world runs (checked: near it, really a sign, not too often)
function S._click(name, data)
    load()
    local x, y, z, side = data:match("^(%-?%d+) (%-?%d+) (%-?%d+) (%a+)$")
    x, y, z = tonumber(x), tonumber(y), tonumber(z)
    if not x or (side ~= "front" and side ~= "back") then return end
    local info = engine.get_player_info and engine.get_player_info(name)
    if info and (x + 0.5 - info.x) ^ 2 + (y + 0.5 - info.y) ^ 2 + (z + 0.5 - info.z) ^ 2 > 8 * 8 then return end
    if not S.by_block[engine.get_block_name(x, y, z)] then return end
    if last_click[name] and clock - last_click[name] < 0.4 then return end
    last_click[name] = clock
    local dim = dim_of_player(name)
    local k = key(dim, x, y, z)
    local reply = function(t) engine.send_to_player(name, "signs:edit", string.format("%d %d %d %s", x, y, z, side)) end

    -- an operator giving this sign an action (/sign ...)
    local p = pending[name]
    if p then
        pending[name] = nil
        if p.op == "set" then
            placed[k] = { action = p.action, args = p.args }; save()
            tell(name, "This sign now runs: " .. p.action .. (p.args ~= "" and (" " .. p.args) or ""), 0.6, 1, 0.6)
        elseif p.op == "clear" then
            placed[k] = nil; save()
            tell(name, "This sign has no action now", 0.6, 1, 0.6)
        elseif p.op == "info" then
            local a = placed[k]
            tell(name, a and ("This sign runs: " .. a.action .. (a.args ~= "" and (" " .. a.args) or "")) or "This sign has no action")
        elseif p.op == "edit" then
            if engine.send_to_player then reply() end
        end
        return
    end

    local a = placed[k]
    if not a then
        if engine.send_to_player then reply() end       -- an ordinary sign: write on it
        return
    end
    local def = S.actions[a.action]
    if not def then tell(name, "This sign's action (" .. a.action .. ") isn't available on this server", 1, 0.6, 0.5) return end
    local ok, msg = pcall(def.run, name, a.args, { x = x, y = y, z = z, side = side, dimension = dim })
    if not ok then
        print("[signs] action " .. a.action .. " failed: " .. tostring(msg))
        tell(name, "This sign's action failed", 1, 0.6, 0.5)
    elseif type(msg) == "string" and msg ~= "" then
        tell(name, msg)
    end
end

-- ---- This game's player: the answers
local prev_server_msg = on_server_message
function on_server_message(channel, data, ...)
    if channel == "signs:edit" then
        local x, y, z, side = tostring(data):match("^(%-?%d+) (%-?%d+) (%-?%d+) (%a+)$")
        if x then S.open_editor(tonumber(x), tonumber(y), tonumber(z), side) end
        return
    elseif channel == "signs:show" then
        S.show_text(tostring(data))
        return
    end
    if prev_server_msg then return prev_server_msg(channel, data, ...) end
end

-- A window with a title and lines of text (the "show" action)
local viewer
function S.show_text(text)
    local parts = {}
    for piece in (text .. "|"):gmatch("(.-)|") do parts[#parts + 1] = piece:match("^%s*(.-)%s*$") end
    local title = table.remove(parts, 1) or ""
    while #parts > 0 and parts[#parts] == "" do parts[#parts] = nil end
    local ok, kit = pcall(require, "ui_kit")
    if not ok or not kit then
        if chat and chat.send then chat.send(title) for _, l in ipairs(parts) do chat.send(l) end end
        return
    end
    while #parts > 14 do parts[#parts] = nil end      -- (a sign's action is one line of text: short)
    local h = math.max(2, #parts) * 0.6 + 1.8          -- as tall as the text
    if viewer and viewer.close then viewer:close() end
    viewer = kit.form{
        width = 14, height = h, title = title ~= "" and title or "Sign",
        draw = function(f, st)
            for i, line in ipairs(st.lines) do f:label(0, (i - 1) * 0.6, line) end
            if f:button("close", 10, h - 1.4, 4, 1, "Close") then f:close() end
        end,
    }
    viewer:open({ lines = parts })
end

-- ---- /sign (operators, where the world runs)
if chat and chat.register_command then
    chat.register_command("sign", "/sign <action> [args] | clear | info | edit | setwarp <name> | warps",
        "Give the next sign you right-click an action (operators)")
end
local prev_cmd = on_chat_command
function on_chat_command(text, player, is_operator, reply, ...)
    local cmd, rest = tostring(text):match("^(%S+)%s*(.-)%s*$")
    if cmd ~= "sign" then
        if prev_cmd then return prev_cmd(text, player, is_operator, reply, ...) end
        return false
    end
    if not is_authority() then return false end       -- joined to a server: it handles /sign
    load()
    local say = function(t) if reply then reply(t) elseif player then tell(player, t) end end
    if not is_operator then say("Only operators can give signs actions") return true end
    local action, args = rest:match("^(%S*)%s*(.-)$")
    if action == "" or action == "help" then
        say("Right-click a sign after one of these:")
        local names = {}
        for n in pairs(S.actions) do names[#names + 1] = n end
        table.sort(names)
        for _, n in ipairs(names) do
            local d = S.actions[n]
            say("  /sign " .. n .. " " .. (d.usage or "") .. (d.description and ("  - " .. d.description) or ""))
        end
        say("  /sign clear | info | edit | cancel;  /sign setwarp <name> | delwarp <name> | warps")
        return true
    end
    if action == "cancel" then pending[player] = nil; say("Cancelled") return true end
    if action == "clear" or action == "info" or action == "edit" then
        pending[player] = { op = action }
        say("Right-click the sign")
        return true
    end
    if action == "setwarp" or action == "delwarp" then
        if args == "" then say("Usage: /sign " .. action .. " <name>") return true end
        if action == "delwarp" then warps[args] = nil; save(); say("Warp " .. args .. " removed") return true end
        local info = engine.get_player_info and engine.get_player_info(player)
        if not info then say("Couldn't find where you are") return true end
        warps[args] = { x = info.x, y = info.y, z = info.z, dim = dim_of_player(player) }
        save()
        say(string.format("Warp %s set here (%.0f, %.0f, %.0f). Use it with /sign warp %s", args, info.x, info.y, info.z, args))
        return true
    end
    if action == "warps" then
        local names = {}
        for n in pairs(warps) do names[#names + 1] = n end
        table.sort(names)
        say(#names > 0 and ("Warps: " .. table.concat(names, ", ")) or "No warps yet (/sign setwarp <name>)")
        return true
    end
    if not S.actions[action] then say("No sign action called " .. action .. " (/sign help lists them)") return true end
    pending[player] = { op = "set", action = action, args = args }
    say("Now right-click the sign to give it: " .. action .. (args ~= "" and (" " .. args) or ""))
    return true
end

-- An action sign that's broken loses its action
local prev_broken2 = on_block_broken
function on_block_broken(x, y, z, block, ...)
    if is_authority() and S.by_block[block] then
        load()
        local k = key(dim_here(), x, y, z)
        if placed[k] then placed[k] = nil; save() end
    end
    if prev_broken2 then return prev_broken2(x, y, z, block, ...) end
end

local prev_update = on_update
function on_update(dt, ...)
    clock = clock + (dt or 0)
    if prev_update then return prev_update(dt, ...) end
end
