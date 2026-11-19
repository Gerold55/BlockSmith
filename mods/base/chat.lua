-- chat.lua: chat commands for the base mod.
--
--   /time                    what time and day it is
--   /time set day|noon|night|midnight|sunrise|sunset|<hh:mm>|<0..1>   (also: /time day, /time night)
--   /time add <hours>
--   /time speed <x>          1 normal, 2 twice as fast, 0 stops time
--   /rain                    rain for a bit
--   /gamerule [rule] [true|false|default], /give <item> [count], /clear, /kill, /tp <x y z>
--                            in your own world (singleplayer, or hosting); on a server, the
--                            server's commands of the same names answer instead
--
-- The time of day (sky.get_time): 0.25 midnight, 0.5 sunrise, 0.75 midday, 0 sunset.
-- On someone else's server, the server's clock counts: /time goes to the server (operators).

local NAMED = {
    day = 7 / 24 + 0.25, morning = 7 / 24 + 0.25,      -- 07:00
    noon = 0.75, midday = 0.75,
    sunset = 0.0, evening = 0.0,
    night = (19 / 24 + 0.25) % 1,                         -- 19:00
    midnight = 0.25,
    sunrise = 0.5, dawn = 0.5,
}

local function time_of(text)
    text = (text or ""):lower()
    if NAMED[text] then return NAMED[text] end
    local h, m = text:match("^(%d%d?):(%d%d)$")
    if h then
        h, m = tonumber(h), tonumber(m)
        if h < 24 and m < 60 then return ((h * 60 + m) / 1440 + 0.25) % 1 end
    end
    local v = tonumber(text)
    if v and v >= 0 and v <= 1 then return v end
    return nil
end

local function report(reply)
    local speed = sky.get_time_speed and sky.get_time_speed() or 1
    local line = string.format("Day %d, %s", sky.get_day and sky.get_day() or 0,
                               sky.get_clock and sky.get_clock() or string.format("%.3f", sky.get_time()))
    if speed == 0 then line = line .. " - time is stopped (/time speed 1 starts it)"
    elseif speed ~= 1 then line = line .. string.format(" - running at %gx", speed) end
    reply(line)
end

-- Game rules in your own world: saved with it, and shared ("gamerule.<name>") so mods - the
-- survival mod - follow them, the same as on a server
local RULES = { "doDaylightCycle", "keepInventory", "naturalRegeneration", "fallDamage", "drowningDamage", "fireDamage", "doMobSpawning",
                "randomTickSpeed" }
local NUMERIC = { randomTickSpeed = "3" }      -- rules that are numbers (their default)
local function find_rule(n)
    for _, r in ipairs(RULES) do if r:lower() == (n or ""):lower() then return r end end
end
local function load_rules()
    local out = {}
    for k, v in ((engine.get_storage("base:gamerules") or "") .. ";"):gmatch("(%w+)=(%w+);") do out[k] = v end
    return out
end
local function apply_rules(rules)
    for _, r in ipairs(RULES) do engine.set_shared("gamerule." .. r, rules[r] or "") end
    if sky.set_time_speed and rules.doDaylightCycle then sky.set_time_speed(rules.doDaylightCycle == "false" and 0 or 1) end
end
local rules_applied = false
local previous_update = on_update
function on_update(dt, ...)
    local own = engine.is_in_game() and (not engine.is_authority or engine.is_authority())
    if own and not rules_applied then rules_applied = true; apply_rules(load_rules()) end
    if not engine.is_in_game() then rules_applied = false end
    if previous_update then return previous_update(dt, ...) end
end

-- A coordinate: a number, or ~ / ~5 (from where you are)
local function coord(t, base)
    if not t then return nil end
    if t:sub(1, 1) == "~" then return base + (tonumber(t:sub(2)) or 0) end
    return tonumber(t)
end

-- Tab completion of these commands' arguments (the names come from the registrations below).
-- on_chat_complete(words, word): words = the words before the one being typed, without the "/".
-- Hooks chain: each mod adds to what the one before it returned.
local function item_names()
    local out = {}
    if ui and ui.get_all_blocks then
        for _, b in ipairs(ui.get_all_blocks() or {}) do
            local n = type(b) == "table" and (b.name or b.id) or b
            if type(n) == "string" and n ~= "" and n ~= "air" then out[#out + 1] = n end
        end
    end
    table.sort(out)
    return out
end
local TIMES = { "day", "noon", "night", "midnight", "sunrise", "sunset" }
local previous_complete = on_chat_complete
function on_chat_complete(words, word, ...)
    local out = previous_complete and previous_complete(words, word, ...) or {}
    local function add(list) for _, v in ipairs(list) do out[#out + 1] = v end end
    local cmd, n = (words[1] or ""):lower(), #words
    if cmd == "time" then
        if n == 1 then add({ "set", "add", "speed", "query" }); add(TIMES)
        elseif n == 2 and words[2]:lower() == "set" then add(TIMES)
        elseif n == 2 and words[2]:lower() == "speed" then add({ "0", "1", "2" }) end
    elseif cmd == "gamerule" then
        if n == 1 then add(RULES)
        elseif n == 2 and find_rule(words[2]) then
            add(NUMERIC[find_rule(words[2])] and { "0", "3", "10", "default" } or { "true", "false", "default" })
        end
    elseif cmd == "give" then
        if n == 1 or (n == 2 and words[2] == "@s") then add(item_names()) end
    elseif cmd == "tp" or cmd == "teleport" then
        if n <= 3 then add({ "~" }) end
    end
    return out
end

if chat and chat.register_command then
    chat.register_command("time", "/time [set|add|speed|query] [value]", "Show or change the time of day")
    chat.register_command("gamerule", "/gamerule [rule] [true|false|default]", "Show or change the game rules")
    chat.register_command("give", "/give <item> [count]", "Give yourself an item")
    chat.register_command("clear", "/clear", "Empty your inventory")
    chat.register_command("kill", "/kill", "Die and respawn")
    chat.register_command("tp", "/tp <x y z>", "Teleport (~ = where you are)")
    chat.register_command("rain", "/rain", "Make it rain for a bit")
end

function on_chat_command(cmd, player, is_operator, reply)
    reply = reply or function(t) chat.send(t) end
    local name, args = cmd:match("^(%S+)%s*(.*)$")
    if not name then return false end

    if name == "time" then
        -- Joined to a server: its clock is the one everyone sees, so the server answers
        if engine.is_authority and not engine.is_authority() then return false end
        local sub, rest = args:match("^(%S*)%s*(.-)%s*$")
        sub = (sub or ""):lower()
        if sub == "" or sub == "query" then
            report(reply)
            return true
        end
        if sub ~= "set" and sub ~= "add" and sub ~= "speed" then rest, sub = sub, "set" end   -- "/time day"
        if sub == "set" then
            local t = time_of(rest)
            if not t then
                reply("Usage: /time set day|noon|night|midnight|sunrise|sunset|<hh:mm>")
                return true
            end
            sky.set_time(t, true)     -- forward to the next time it's that time, like Minecraft
            report(reply)
        elseif sub == "add" then
            local hours = tonumber(rest)
            if not hours then reply("Usage: /time add <hours>") return true end
            sky.add_time(hours)
            report(reply)
        elseif sub == "speed" then
            local v = tonumber(rest)
            if not v or v < 0 then reply("Usage: /time speed <x>   (1 normal, 0 stops time)") return true end
            sky.set_time_speed(v)
            report(reply)
        end
        return true
    end

    local own_world = not engine.is_authority or engine.is_authority()
    if name == "gamerule" or name == "give" or name == "clear" or name == "kill" or name == "tp" or name == "teleport" then
        if not own_world then return false end      -- on a server: its own commands answer
        local a = {}
        for w in args:gmatch("%S+") do a[#a + 1] = w end
        if name == "gamerule" then
            local rules = load_rules()
            if #a == 0 then
                for _, r in ipairs(RULES) do reply(r .. " = " .. (rules[r] or "default")) end
                return true
            end
            local rule = find_rule(a[1])
            if not rule then reply("No game rule called '" .. a[1] .. "'. Type /gamerule for the list.") return true end
            if not a[2] then reply(rule .. " = " .. (rules[rule] or "default")) return true end
            local v = a[2]:lower()
            if NUMERIC[rule] then
                if v ~= "default" and not v:match("^%d%d?%d?%d?$") then reply(rule .. " is a number (0 to 9999) or default") return true end
            elseif v ~= "true" and v ~= "false" and v ~= "default" then reply("A game rule is true, false or default") return true end
            rules[rule] = (v ~= "default") and v or nil
            local parts = {}
            for k, val in pairs(rules) do parts[#parts + 1] = k .. "=" .. val end
            engine.set_storage("base:gamerules", table.concat(parts, ";"))
            apply_rules(rules)
            reply("Game rule " .. rule .. " is now " .. v)
        elseif name == "give" then
            local item, count = a[1], tonumber(a[2] or "1")
            if a[1] == "@s" then item, count = a[2], tonumber(a[3] or "1") end
            if not item or not count or count < 1 then reply("Usage: /give <item> [count]") return true end
            if engine.get_block_id(item) == 0 then reply("There's no item called '" .. item .. "'") return true end
            local left = math.min(count, 4096)
            while left > 0 do
                local n = math.min(left, 64)
                if not engine.give_item(item, n) then break end
                left = left - n
            end
            reply("Gave " .. (math.min(count, 4096) - left) .. " " .. item)
        elseif name == "clear" then
            for i = 1, hotbar.get_slot_count() do hotbar.set_slot(i, "", 0) end
            if survival and survival.inventory then survival.inventory.clear() end
            reply("Cleared your inventory")
        elseif name == "kill" then
            if survival and survival.kill and survival.kill("gave up") then return true end
            reply("Nothing to kill here (creative mode, or no survival mod)")
        else -- tp <x y z>
            local x0, y0, z0 = engine.get_player_position()
            local i = (a[1] == "@s") and 2 or 1
            local x, y, z = coord(a[i], x0), coord(a[i + 1], y0), coord(a[i + 2], z0)
            if not (x and y and z) then reply("Usage: /tp <x y z>   (~ = where you are, ~5 = 5 more)") return true end
            if a[i]:sub(1, 1) ~= "~" and not a[i]:find("%.") then x = x + 0.5 end
            if a[i + 2]:sub(1, 1) ~= "~" and not a[i + 2]:find("%.") then z = z + 0.5 end
            engine.set_player_position(x, y, z)
            reply(string.format("Teleported to %.1f, %.1f, %.1f", x, y, z))
        end
        return true
    end

    if name == "rain" then
        clouds.set_weather("rain", 10)
        chat.send("Rain starting")
        return true
    end

    return false   -- not ours; the engine shows "Unknown command"
end
