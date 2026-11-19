-- chat.lua: chat commands for the base mod.
--
--   /time                    what time and day it is
--   /time set day|noon|night|midnight|sunrise|sunset|<hh:mm>|<0..1>   (also: /time day, /time night)
--   /time add <hours>
--   /time speed <x>          1 normal, 2 twice as fast, 0 stops time
--   /rain                    rain for a bit
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

    if name == "rain" then
        clouds.set_weather("rain", 10)
        chat.send("Rain starting")
        return true
    end

    return false   -- not ours; the engine shows "Unknown command"
end
