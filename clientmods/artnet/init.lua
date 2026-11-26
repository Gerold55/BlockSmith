-- Art-Net: DMX from your lighting software or console (QLC+, grandMA onPC, MagicQ, Lightkey, a
-- desk with an Art-Net node...) into the game, passed on to the server's mods.
--
--   1. Settings > Network > Network input: "This computer" (software on this PC) or "Network" (a
--      desk or another PC). The port is Art-Net's, 6454 (settings.txt: udp_ports).
--   2. Switch this client mod on (Mods screen).
--   3. Point your software's Art-Net output at this PC (127.0.0.1 on the same PC).
--
-- What it does: reads ArtDmx packets into universes (512 channels each), answers ArtPoll on
-- "Network" (consoles then list the game as a node), and sends the universes that changed to the
-- server's mods on the channel "artnet:dmx", about 15 times a second (everything again once a
-- second). A server mod that wants DMX listens on that channel and decides what to do with it
-- (and whether to believe this player). In singleplayer it goes to your own world's mods.
--
-- Message format (for server mods): "u=<universe>:<1024 hex digits>" parts joined by "|"; a
-- universe is the Art-Net port address (net * 256 + sub-net * 16 + universe), 0 for most software.
--
-- .artnet            how it's going
-- .artnet pause      stop sending to the server (.artnet resume)
-- .artnet port <n>   listen on another port (one the player allows in settings.txt: udp_ports)

local PORT_DEFAULT = 6454
local SEND_RATE = 15                 -- messages a second, at most
local UNIVERSES_PER_MESSAGE = 3      -- (a message from a player's game: 4 KB at most)

local storage = client.get_mod_storage()
local port = tonumber(storage:get("port") or "") or PORT_DEFAULT
local paused = storage:get("paused") == "1"

local universes = {}                 -- [universe] = { data = "512 bytes", frames, last, hex, sent_hex }
local listening, why_not = false, nil
local t, send_t, full_t = 0, 0, 0
local told = false
local packets, polls, sent = 0, 0, 0

local ID = "Art-Net\0"
local OP_POLL, OP_POLL_REPLY, OP_DMX = 0x2000, 0x2100, 0x5000

local function try_listen()
    local ok, err = client.udp_listen(port)
    listening, why_not = ok, (not ok) and err or nil
    return ok
end

-- an ArtPollReply: "a node called BlockSmith, taking universes 0-3"
local function poll_reply()
    local function pad(s, n) return s .. string.rep("\0", n - #s) end
    local parts = {
        ID, string.char(OP_POLL_REPLY & 0xFF, OP_POLL_REPLY >> 8),
        "\0\0\0\0",                      -- IP address (consoles use where it came from)
        string.char(0x36, 0x19),         -- port 6454
        "\0\1", "\0\0", "\0\0",          -- version, net / sub-net, OEM
        "\0", string.char(0xD0), "\0\0", -- UBEA, status, ESTA
        pad("BlockSmith", 18), pad("BlockSmith Art-Net client mod", 64), pad("#0001 [0000] Ready", 64),
        "\0\4",                          -- 4 ports
        string.rep(string.char(0x80), 4),-- port types: take DMX from the network
        "\0\0\0\0",                      -- good input
        string.rep(string.char(0x80), 4),-- good output: data being taken
        "\0\0\0\0",                      -- SwIn
        string.char(0, 1, 2, 3),         -- SwOut: universes 0-3
        "\0\0\0\0\0\0\0",                -- (video, macro, remote, spare)
        "\0",                            -- style: a node
    }
    local r = table.concat(parts)
    return r .. string.rep("\0", 239 - #r)
end

local function handle(p)
    local d = p.data
    if #d < 12 or d:sub(1, 8) ~= ID then return end
    local op = d:byte(9) | (d:byte(10) << 8)
    if op == OP_POLL then
        polls = polls + 1
        if client.udp_status().mode == "network" then client.udp_reply(port, p.from, PORT_DEFAULT, poll_reply()) end
        return
    end
    if op ~= OP_DMX or #d < 18 then return end
    local universe = ((d:byte(16) & 0x7F) << 8) | d:byte(15)
    local length = (d:byte(17) << 8) | d:byte(18)
    if length < 1 or length > 512 or #d < 18 + length then return end
    local data = d:sub(19, 18 + length)
    if length < 512 then data = data .. string.rep("\0", 512 - length) end
    local u = universes[universe]
    if not u then
        local count = 0
        for _ in pairs(universes) do count = count + 1 end
        if count >= 16 then return end                 -- (16 universes is plenty for a game)
        u = { frames = 0 }
        universes[universe] = u
    end
    u.data, u.frames, u.last, u.hex = data, u.frames + 1, t, nil
    packets = packets + 1
end

local function hex_of(u)
    if not u.hex then u.hex = (u.data:gsub(".", function(c) return string.format("%02x", c:byte()) end)) end
    return u.hex
end

-- the universes that changed (all of them once a second), to the server's mods
local function forward()
    send_t = send_t - 1
    full_t = full_t - 1
    local full = full_t <= 0
    if full then full_t = SEND_RATE end
    local parts = {}
    local function flush()
        if #parts > 0 then
            if client.send_to_server("artnet:dmx", table.concat(parts, "|")) then sent = sent + 1 end
            parts = {}
        end
    end
    for universe, u in pairs(universes) do
        if u.data and t - (u.last or -99) < 3 then
            local h = hex_of(u)
            if full or h ~= u.sent_hex then
                u.sent_hex = h
                parts[#parts + 1] = "u=" .. universe .. ":" .. h
                if #parts >= UNIVERSES_PER_MESSAGE then flush() end
            end
        end
    end
    flush()
end

client.register_on_join(function()
    try_listen()
    told = false
end)

client.register_on_update(function(dt)
    t = t + dt
    if not listening then
        -- the player may turn network input on while playing: try again now and then
        if math.floor(t) ~= math.floor(t - dt) and math.floor(t) % 3 == 0 then try_listen() end
        if not listening and not told and t > 2 then
            told = true
            client.display_chat_message("[Art-Net] Not receiving: " .. tostring(why_not) .. ". (.artnet for help)", 1, 0.8, 0.4)
        end
        return
    end
    for _, p in ipairs(client.udp_receive(port, 256)) do handle(p) end
    if paused then return end
    send_t = send_t + dt * SEND_RATE
    if send_t >= 1 then forward() end
    if send_t > 2 then send_t = 0 end                   -- (a long frame: don't send a burst)
end)

client.register_chat_command("artnet", {
    params = "[pause | resume | port <n>]",
    description = "Art-Net input: status, pause or resume sending to the server, change the port",
    func = function(param)
        local cmd, arg = (param or ""):match("^(%S*)%s*(.-)$")
        if cmd == "pause" then paused = true; storage:set("paused", "1"); return true, "Not sending DMX to the server"
        elseif cmd == "resume" then paused = false; storage:set("paused", nil); return true, "Sending DMX to the server"
        elseif cmd == "port" and tonumber(arg) then
            port = math.floor(tonumber(arg)); storage:set("port", tostring(port))
            return try_listen(), listening and ("Listening on port " .. port) or ("Can't listen on " .. port .. ": " .. tostring(why_not))
        end
        local st = client.udp_status()
        if not listening then
            return false, "Not receiving: " .. tostring(why_not) .. ". Turn on Settings > Network > Network input (This computer, or Network for a desk or another PC)."
        end
        local list = {}
        for universe, u in pairs(universes) do
            list[#list + 1] = string.format("%d (%s)", universe, (t - (u.last or -99) < 2) and "live" or "quiet")
        end
        table.sort(list)
        return true, string.format("Listening on port %d (%s). Universes: %s. %d packets, %d sent to the server%s.",
            port, st.mode, #list > 0 and table.concat(list, ", ") or "none yet", packets, sent, paused and " (paused)" or "")
    end,
})
