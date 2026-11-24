// Client mods: see ClientMods.h
#include "ClientMods.h"
#include "UdpInput.h"
#include "Diagnostics.h"
#include "ModOrder.h"
#include "SafeReplace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A Lua value from one state, rebuilt in another (nil, booleans, numbers, strings and tables;
// functions and userdata become nil). Used to let client mods call the game's ui.* functions.
sol::object copyValue(lua_State* to, const sol::object& v, int depth = 0) {
    sol::state_view dst(to);
    switch (v.get_type()) {
        case sol::type::boolean:
            return sol::make_object(dst, v.as<bool>());
        case sol::type::number: {
            const double n = v.as<double>();
            if (n == std::floor(n) && std::fabs(n) < 9.0e15) return sol::make_object(dst, static_cast<long long>(n));
            return sol::make_object(dst, n);
        }
        case sol::type::string:
            return sol::make_object(dst, v.as<std::string>());
        case sol::type::table: {
            if (depth > 8) return sol::make_object(dst, sol::lua_nil);
            sol::table out = dst.create_table();
            for (const auto& kv : v.as<sol::table>()) {
                sol::object k = copyValue(to, kv.first, depth + 1);
                if (k.get_type() == sol::type::lua_nil) continue;
                out.raw_set(k, copyValue(to, kv.second, depth + 1));
            }
            return sol::make_object(dst, out);
        }
        default:
            return sol::make_object(dst, sol::lua_nil);
    }
}

std::map<std::string, std::string> readKeyValues(const fs::path& file) {
    std::map<std::string, std::string> kv;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t\r") + 1);
            return s;
        };
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

// Storage files: one value per line, key=value, with \n and \\ escaped
std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '=') o += "\\e";
        else o += c;
    }
    return o;
}
std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 'e' ? '=' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

// The part of the client API written in Lua: callback lists, dispatch, chat commands, storage
// objects and dofile. Runs once in the client state, before any mod.
const char* kPrelude = R"LUA(
-- Sandbox: no loading files or binary chunks behind the engine's back, a harmless os
dofile, loadfile = nil, nil
do
    local _os = os
    os = { time = _os.time, clock = _os.clock, date = _os.date, difftime = _os.difftime }
    local _load = load
    load = function(chunk, name, mode, env) return _load(chunk, name, "t", env) end
end

local callbacks = { update = {}, hud = {}, key = {}, chat_message = {}, right_click_block = {},
                    dig_block = {}, join = {}, leave = {} }
local commands = {}

for kind, list in pairs(callbacks) do
    client["register_on_" .. kind] = function(fn)
        if type(fn) ~= "function" then error("client.register_on_" .. kind .. " needs a function", 2) end
        list[#list + 1] = { fn = fn, mod = client.get_current_modname() or "?" }
    end
end

function client.register_chat_command(name, def)
    if type(name) ~= "string" or name == "" or name:find("%s") then
        error("client.register_chat_command needs a name without spaces", 2)
    end
    if type(def) == "function" then def = { func = def } end
    if type(def) ~= "table" or type(def.func) ~= "function" then
        error("client.register_chat_command needs { func = function(param) ... end }", 2)
    end
    commands[name] = { def = def, mod = client.get_current_modname() or "?" }
end

function client.get_mod_storage()
    local mod = client.get_current_modname()
    if not mod then error("call client.get_mod_storage() while your mod loads (at the top of the file)", 2) end
    local s = {}
    function s:get(key) return __storage_get(mod, tostring(key)) end
    function s:get_string(key) return __storage_get(mod, tostring(key)) or "" end
    function s:get_int(key) return math.floor(tonumber(__storage_get(mod, tostring(key))) or 0) end
    function s:get_float(key) return tonumber(__storage_get(mod, tostring(key))) or 0 end
    function s:set(key, value)
        if value == nil then __storage_set(mod, tostring(key)) else __storage_set(mod, tostring(key), tostring(value)) end
    end
    s.set_string, s.set_int, s.set_float = s.set, s.set, s.set
    function s:get_keys() return __storage_keys(mod) end
    return s
end

function client.dofile(file)
    local src, name = __read_mod_file(tostring(file))
    if not src then error(name, 2) end
    local f, err = load(src, "@" .. name)
    if not f then error(err, 2) end
    return f()
end

function __client_dispatch(kind, ...)
    local handled = false
    for _, c in ipairs(callbacks[kind]) do
        if not c.dead then
            local ok, r = pcall(c.fn, ...)
            if not ok then
                c.dead = true                       -- one error: that callback stops
                __client_error(c.mod, "register_on_" .. kind, tostring(r))
            elseif r == true then
                handled = true
            end
        end
    end
    return handled
end

function __client_command(text)
    local name, param = text:match("^(%S+)%s*(.-)%s*$")
    if not name then return false end
    if name == "help" and not commands.help then
        local names = {}
        for n in pairs(commands) do names[#names + 1] = n end
        table.sort(names)
        if #names == 0 then client.display_chat_message("No client mod commands.") return true end
        client.display_chat_message("Client mod commands:")
        for _, n in ipairs(names) do
            local d = commands[n].def
            client.display_chat_message("  ." .. n .. (d.params and (" " .. d.params) or "")
                .. (d.description and ("  " .. d.description) or ""))
        end
        return true
    end
    local c = commands[name]
    if not c then
        client.display_chat_message("Unknown client command ." .. name .. " (.help lists them)", 1, 0.6, 0.6)
        return true
    end
    local ok, a, b = pcall(c.def.func, param)
    if not ok then
        __client_error(c.mod, "." .. name, tostring(a))
    elseif type(b) == "string" then
        client.display_chat_message(b)
    elseif type(a) == "string" then
        client.display_chat_message(a)
    end
    return true
end
)LUA";

template <typename... Args>
bool callDispatch(sol::state& L, bool& inCallback, const char* kind, Args&&... args) {
    sol::protected_function f = L["__client_dispatch"];
    if (!f.valid()) return false;
    inCallback = true;
    sol::protected_function_result r = f(kind, std::forward<Args>(args)...);
    inCallback = false;
    if (!r.valid()) {
        sol::error err = r;
        std::cerr << "[Client mods] " << kind << ": " << err.what() << std::endl;
        return false;
    }
    return r.get_type() == sol::type::boolean && r.get<bool>();
}

} // namespace

ClientMods::~ClientMods() {
    m_lua.reset();   // before the game's state goes: our copies of its functions refer to it
}

sol::table ClientMods::api() { return (*m_lua)["client"]; }

void ClientMods::bridge(sol::table from, sol::table to, const std::string& fromName, const std::string& toName) {
    sol::object f = from[fromName];
    if (f.get_type() != sol::type::function) return;
    sol::protected_function fn = f.as<sol::protected_function>();
    const bool isImage = (toName == "draw_image");
    to.set_function(toName, [this, fn, isImage, toName](sol::variadic_args va, sol::this_state ts) {
        sol::variadic_results out;
        std::vector<sol::object> args;
        for (auto a : va) args.push_back(copyValue(m_game, a.get<sol::object>()));
        // ui.draw_image("minimap_frame.png", ...): this client mod's own pictures first
        if (isImage && !args.empty() && args[0].get_type() == sol::type::string) {
            const std::string p = texturePath(args[0].as<std::string>());
            if (!p.empty()) args[0] = sol::make_object(sol::state_view(m_game), p);
        }
        sol::protected_function_result r = fn(sol::as_args(args));
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Client mods] " << toName << ": " << err.what() << std::endl;
            return out;
        }
        for (int i = 0; i < r.return_count(); ++i) out.push_back(copyValue(ts, r.get<sol::object>(i)));
        return out;
    });
}

void ClientMods::init(sol::state& gameLua, const std::string& folder) {
    m_game = gameLua.lua_state();
    m_folder = folder;
    m_lua = std::make_unique<sol::state>();
    sol::state& L = *m_lua;
    L.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math,
                     sol::lib::utf8, sol::lib::os);
    diag::installLuaTraceback(L.lua_state());   // client mods' errors: with their Lua stack

    L["print"] = [](sol::variadic_args va, sol::this_state ts) {
        sol::state_view lv(ts);
        sol::protected_function tostr = lv["tostring"];
        std::string line = "[Client mod]";
        for (auto a : va) {
            sol::protected_function_result r = tostr(a.get<sol::object>());
            line += " " + (r.valid() ? r.get<std::string>() : std::string("?"));
        }
        std::cout << line << std::endl;
    };

    sol::table client = L.create_named_table("client");
    client["get_current_modname"] = [this](sol::this_state ts) -> sol::object {
        if (m_loading.empty()) return sol::make_object(ts, sol::lua_nil);
        return sol::make_object(ts, m_loading);
    };
    client["get_modpath"] = [this](sol::optional<std::string> name, sol::this_state ts) -> sol::object {
        const std::string n = name.value_or(m_loading);
        const auto it = m_modPaths.find(n);
        if (it == m_modPaths.end()) return sol::make_object(ts, sol::lua_nil);
        return sol::make_object(ts, it->second);
    };
    client["get_mod_names"] = [this](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        for (size_t i = 0; i < m_modNames.size(); ++i) t[i + 1] = m_modNames[i];
        return t;
    };
    client["get_rules"] = [this](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        t["allowed"] = m_rules.allowed;
        t["chat"] = m_rules.chat;
        t["players"] = m_rules.players;
        t["block_range"] = m_rules.blockRange;
        t["messages"] = m_rules.messages;
        return t;
    };
    // client.send_to_server(channel, data) -> true if sent: a message to the server's mods (they
    // get it in on_mod_message, from this player, and check it like anything a player's game
    // sends). Only if the server allows it (client_mods_messages).
    client["send_to_server"] = [this](const std::string& channel, sol::object data) -> std::tuple<bool, sol::object> {
        sol::state_view lv(*m_lua);
        auto no = [&](const char* why) { return std::make_tuple(false, sol::make_object(lv, std::string(why))); };
        if (!m_rules.messages) return no("this server doesn't let client mods send messages");
        std::string text;
        if (data.get_type() == sol::type::string) text = data.as<std::string>();
        else if (data.get_type() == sol::type::number) text = std::to_string(data.as<double>());
        else return no("data must be text");
        if (!sendToServer || !sendToServer(channel, text)) return no("not sent (a bad channel name, too long, or not in a world)");
        return std::make_tuple(true, sol::make_object(lv, sol::lua_nil));
    };

    // Network input (UdpInput.h): only if the player turned it on in Settings, only on the ports
    // they allow. Packets are data (strings of bytes); nothing in them is ever run.
    //   client.udp_status() -> { mode = "off"|"local"|"network", ports = { allowed }, listening = { ports }, received }
    //   client.udp_listen(port) -> true, or false + why
    //   client.udp_receive(port [, max]) -> { { data, from, port, time }, ... } (since the last call)
    //   client.udp_reply(port, address, to_port, data) -> true, or false + why (only to who just sent here)
    client["udp_status"] = [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        const auto m = g_UdpInput.mode();
        t["mode"] = std::string(m == UdpInput::Mode::Off ? "off" : m == UdpInput::Mode::Local ? "local" : "network");
        sol::table ports = lv.create_table();
        int i = 1;
        for (int p : g_UdpInput.allowedPorts()) ports[i++] = p;
        t["ports"] = ports;
        sol::table lis = lv.create_table();
        i = 1;
        for (int p : g_UdpInput.listeningPorts()) lis[i++] = p;
        t["listening"] = lis;
        t["received"] = static_cast<double>(g_UdpInput.received());
        if (!g_UdpInput.lastError().empty()) t["error"] = g_UdpInput.lastError();
        return t;
    };
    client["udp_listen"] = [](int port, sol::this_state ts) -> std::tuple<bool, sol::object> {
        std::string why;
        if (g_UdpInput.listen(port, why)) return std::make_tuple(true, sol::make_object(ts, sol::lua_nil));
        return std::make_tuple(false, sol::make_object(ts, why));
    };
    client["udp_receive"] = [](int port, sol::optional<int> max, sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        int i = 1;
        for (auto& p : g_UdpInput.take(port, static_cast<size_t>(std::clamp(max.value_or(256), 1, 256)))) {
            sol::table e = lv.create_table();
            e["data"] = p.data;
            e["from"] = p.from;
            e["port"] = p.fromPort;
            e["time"] = p.time;
            t[i++] = e;
        }
        return t;
    };
    client["udp_reply"] = [](int port, const std::string& to, int toPort, const std::string& data,
                              sol::this_state ts) -> std::tuple<bool, sol::object> {
        std::string why;
        if (g_UdpInput.reply(port, to, toPort, data, why)) return std::make_tuple(true, sol::make_object(ts, sol::lua_nil));
        return std::make_tuple(false, sol::make_object(ts, why));
    };
    client["display_chat_message"] = [this](const std::string& text, sol::optional<float> r,
                                             sol::optional<float> g, sol::optional<float> b) {
        if (showChat) showChat(text, r.value_or(0.85f), g.value_or(0.85f), b.value_or(1.0f));
    };

    // Storage (client.get_mod_storage() in the prelude wraps these)
    L["__storage_get"] = [this](const std::string& mod, const std::string& key, sol::this_state ts) -> sol::object {
        const auto m = m_storage.find(mod);
        if (m == m_storage.end()) return sol::make_object(ts, sol::lua_nil);
        const auto it = m->second.find(key);
        if (it == m->second.end()) return sol::make_object(ts, sol::lua_nil);
        return sol::make_object(ts, it->second);
    };
    L["__storage_set"] = [this](const std::string& mod, const std::string& key, sol::optional<std::string> value) {
        if (!m_modPaths.count(mod)) return;
        if (value) m_storage[mod][key] = *value;
        else m_storage[mod].erase(key);
        saveStorage(mod);
    };
    L["__storage_keys"] = [this](const std::string& mod, sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        int i = 1;
        for (const auto& kv : m_storage[mod]) t[i++] = kv.first;
        return t;
    };
    // client.dofile(file): a file in the mod's own folder, only while the mod loads
    L["__read_mod_file"] = [this](const std::string& file) -> std::tuple<sol::object, std::string> {
        sol::state_view lv(*m_lua);
        auto fail = [&](const std::string& why) { return std::make_tuple(sol::make_object(lv, sol::lua_nil), why); };
        if (m_loading.empty()) return fail("client.dofile only works while your mod loads");
        const fs::path rel(file);
        if (rel.is_absolute() || file.find("..") != std::string::npos) return fail("client.dofile: '" + file + "' must be inside your mod's folder");
        const fs::path p = fs::path(m_modPaths[m_loading]) / rel;
        std::ifstream in(p, std::ios::binary);
        if (!in) return fail("client.dofile: no file " + p.generic_string());
        std::stringstream ss;
        ss << in.rdbuf();
        return std::make_tuple(sol::make_object(lv, ss.str()), m_loading + "/" + rel.generic_string());
    };
    L["__client_error"] = [this](const std::string& mod, const std::string& where, const std::string& msg) {
        std::cerr << "[Client mod " << mod << "] error in " << where << ": " << msg << std::endl;
        if (showChat) showChat("[" + mod + "] error in " + where + " (switched off): " + msg, 1.0f, 0.45f, 0.45f);
    };

    // Copies of the game's drawing and sound functions (they run in the game's state)
    sol::table game = sol::state_view(m_game).globals();
    sol::table ui = L.create_named_table("ui");
    if (game["ui"].get_type() == sol::type::table) {
        sol::table gameUi = game["ui"];
        for (const auto& kv : gameUi) {
            if (kv.first.get_type() != sol::type::string) continue;
            const std::string name = kv.first.as<std::string>();
            if (kv.second.get_type() == sol::type::function) bridge(gameUi, ui, name, name);
            else ui[name] = copyValue(L.lua_state(), kv.second);   // ui.keys, ui.actions
        }
    }
    sol::table sound = L.create_named_table("sound");
    if (game["sound"].get_type() == sol::type::table) {
        sol::table gameSound = game["sound"];
        for (const char* n : { "stop", "fade", "set_gain", "set_pitch", "set_position", "is_playing",
                               "is_available", "get_volume" }) {
            bridge(gameSound, sound, n, n);
        }
        // Looking sounds up by name: only the sandboxed versions, which can't reach files outside
        // the sound folders (no raw paths, no "..", no symlinks out). Without them: no play/exists.
        bridge(gameSound, sound, "__play_sandboxed", "play");
        bridge(gameSound, sound, "__exists_sandboxed", "exists");
    }
    // Read-only facts about the player (always allowed: it's their own game)
    struct Alias { const char* table; const char* from; const char* to; };
    for (const Alias& a : std::initializer_list<Alias>{
             { "engine", "get_player_state", "get_player_state" },
             { "engine", "get_player_position", "get_player_position" },
             { "engine", "get_player_name", "get_player_name" },
             { "engine", "is_creative", "is_creative" },
             { "engine", "is_left_handed", "is_left_handed" },
             { "engine", "key_name", "key_name" },
             { "engine", "get_block_def", "get_block_def" },
             { "engine", "list_blocks", "list_blocks" },
             { "engine", "get_dig_progress", "get_dig_progress" },
             { "sky", "get_time", "get_time" },
             { "hotbar", "get_slot", "get_hotbar_slot" },
             { "hotbar", "get_active_slot", "get_active_slot" },
             { "hotbar", "get_slot_count", "get_hotbar_size" },
         }) {
        if (game[a.table].get_type() != sol::type::table) continue;
        sol::table from = game[a.table];
        bridge(from, client, a.from, a.to);
    }
}

void ClientMods::load() {
    g_UdpInput.closeAll();          // (client mods loading again: they ask for their ports again)
    if (!m_lua) return;
    sol::state& L = *m_lua;
    {
        auto r = L.safe_script(kPrelude, sol::script_pass_on_error, "=client_prelude");
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Client mods] prelude: " << err.what() << std::endl;
            return;
        }
    }

    std::error_code ec;
    if (!fs::is_directory(m_folder, ec)) {
        fs::create_directories(m_folder, ec);
        std::ofstream readme(fs::path(m_folder) / "README.txt");
        readme << "Client mods: your own scripts, running only in your game (a minimap, waypoints, HUDs...).\n"
                  "Each mod is a folder here with an init.lua. They're off until you switch them on, on the\n"
                  "Mods screen (saved in mods.conf here as load_mod_<name> = true). Servers decide what\n"
                  "client mods may do while you play there. See docs/MODDING.md, 'Client mods'.\n";
    }
    const auto conf = readKeyValues(fs::path(m_folder) / "mods.conf");

    // Like Luanti, a client mod only runs once the player has switched it on
    std::vector<fs::path> inits;
    for (const auto& entry : fs::directory_iterator(m_folder, ec)) {
        if (!entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '_' || name[0] == '.') continue;     // _storage etc.
        if (!fs::exists(entry.path() / "init.lua", ec)) continue;
        const auto it = conf.find("load_mod_" + name);
        const bool on = it != conf.end() && (lower(it->second) == "true" || it->second == "1");
        m_onAtStartup[name] = on;
        if (!on) {
            std::cout << "[Client mods] " << name << " is off (switch it on on the Mods screen)" << std::endl;
            continue;
        }
        inits.push_back(entry.path() / "init.lua");
    }
    modorder::sortRoot(inits, 0);
    modorder::applyDepends(inits);
    std::cout << "[Client mods] " << m_onAtStartup.size() << " found, " << inits.size()
              << " switched on" << (m_onAtStartup.empty() ? " (each is a folder with an init.lua)" : "") << std::endl;

    for (const auto& init : inits) {
        const fs::path dir = init.parent_path();
        const std::string name = dir.filename().string();
        m_modPaths[name] = dir.string();
        m_modFolders.push_back(dir.string());
        for (const auto& f : fs::recursive_directory_iterator(dir, ec)) {
            if (f.is_regular_file() && lower(f.path().extension().string()) == ".png") {
                m_textures.emplace(lower(f.path().filename().string()), fs::absolute(f.path(), ec).string());
            }
        }
        loadStorage(name);
    }
    if (onModFoldersReady) onModFoldersReady();   // e.g. their sounds: findable from init.lua on
    for (const auto& init : inits) {
        const std::string name = init.parent_path().filename().string();
        m_loading = name;
        std::cout << "[Client mods] Loading " << name << std::endl;
        auto r = L.safe_script_file(init.string(), sol::script_pass_on_error);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Client mod " << name << "] " << err.what() << std::endl;
            m_loadErrors[name] = err.what();
            if (showChat) showChat("[" + name + "] failed to load: " + std::string(err.what()), 1.0f, 0.45f, 0.45f);
        } else {
            m_modNames.push_back(name);
        }
    }
    m_loading.clear();
}

std::vector<ClientMods::Info> ClientMods::list() const {
    std::vector<Info> out;
    std::error_code ec;
    if (m_folder.empty() || !fs::is_directory(m_folder, ec)) return out;
    const auto conf = readKeyValues(fs::path(m_folder) / "mods.conf");
    for (const auto& entry : fs::directory_iterator(m_folder, ec)) {
        if (!entry.is_directory()) continue;
        Info m;
        m.name = entry.path().filename().string();
        if (m.name.empty() || m.name[0] == '_' || m.name[0] == '.') continue;
        if (!fs::exists(entry.path() / "init.lua", ec)) continue;
        m.path = entry.path().string();
        const auto kv = readKeyValues(entry.path() / "mod.conf");
        auto get = [&](const char* k) { const auto it = kv.find(k); return it == kv.end() ? std::string() : it->second; };
        m.title = get("title").empty() ? m.name : get("title");
        m.author = get("author");
        m.description = get("description");
        m.version = get("version");
        m.depends = get("depends");
        for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file() && f.path().extension() == ".lua") ++m.luaFiles;
        }
        const auto it = conf.find("load_mod_" + m.name);
        m.enabled = it != conf.end() && (lower(it->second) == "true" || it->second == "1");
        const auto at = m_onAtStartup.find(m.name);
        const bool wasOn = at != m_onAtStartup.end() && at->second;
        m.restartNeeded = m.enabled != wasOn;
        const auto err = m_loadErrors.find(m.name);
        if (err != m_loadErrors.end()) { m.status = "error"; m.error = err->second; }
        else if (std::find(m_modNames.begin(), m_modNames.end(), m.name) != m_modNames.end()) m.status = "loaded";
        else if (wasOn || m.enabled) m.status = "not_loaded";
        else m.status = "disabled";
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return lower(a.title) < lower(b.title); });
    return out;
}

bool ClientMods::setEnabled(const std::string& name, bool on, std::string& error) {
    std::error_code ec;
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos || !fs::exists(fs::path(m_folder) / name / "init.lua", ec)) {
        error = "Client mod not found.";
        return false;
    }
    // Rewrite mods.conf, keeping its other lines and comments
    const fs::path file = fs::path(m_folder) / "mods.conf";
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    const std::string key = "load_mod_" + name;
    const std::string value = key + " = " + (on ? "true" : "false");
    bool found = false;
    for (auto& line : lines) {
        std::string k = line.substr(0, line.find('='));
        k.erase(k.find_last_not_of(" \t") + 1);
        if (k == key) { line = value; found = true; }
    }
    if (!found) {
        if (lines.empty()) lines.push_back("# Client mods that run (switch them on and off on the Mods screen)");
        lines.push_back(value);
    }
    std::ofstream out(file, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
    if (!out) { error = "Could not save " + file.generic_string(); return false; }
    std::cout << "[Client mods] " << name << " will be " << (on ? "on" : "off") << " after a restart" << std::endl;
    return true;
}

std::string ClientMods::texturePath(const std::string& name) const {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return "";
    const auto it = m_textures.find(lower(name));
    return it == m_textures.end() ? std::string() : it->second;
}

void ClientMods::loadStorage(const std::string& mod) {
    auto& s = m_storage[mod];
    std::ifstream in(fs::path(m_folder) / "_storage" / (mod + ".txt"));
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        s[unescape(line.substr(0, eq))] = unescape(line.substr(eq + 1));
    }
}

void ClientMods::saveStorage(const std::string& mod) {
    std::error_code ec;
    const fs::path dir = fs::path(m_folder) / "_storage";
    fs::create_directories(dir, ec);
    const fs::path file = dir / (mod + ".txt"), tmp = dir / (mod + ".txt.tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        for (const auto& kv : m_storage[mod]) out << escape(kv.first) << "=" << escape(kv.second) << "\n";
        if (!out) return;
    }
    if (!replaceFile(tmp, file, ec)) {   // the old storage file stays as it was
        std::cerr << "[Client mods] Could not save " << file.string() << ": " << ec.message() << std::endl;
    }
}

void ClientMods::update(float dt, const std::string& where, const std::string& address) {
    if (!m_lua || m_modNames.empty()) return;
    const bool on = m_rules.allowed && !where.empty();
    const std::string was = m_where.empty() ? "" : m_where + "|" + m_address;
    const std::string now = on ? where + "|" + address : "";
    if (now != was) {
        if (!was.empty()) callDispatch(*m_lua, m_inCallback, "leave");
        if (!where.empty() && !m_rules.allowed && showChat) {
            showChat("Client mods are switched off on this server.", 1.0f, 0.75f, 0.4f);
        }
        m_where = on ? where : "";
        m_address = on ? address : "";
        if (on) {
            sol::table info = m_lua->create_table();
            info["mode"] = where;
            if (where == "server") info["address"] = address;
            else info["world"] = address;     // singleplayer / hosting: the world's name
            callDispatch(*m_lua, m_inCallback, "join", info);
        }
    }
    if (on) callDispatch(*m_lua, m_inCallback, "update", dt);
}

void ClientMods::key(int key, int action, int mods) {
    if (active() && !m_where.empty()) callDispatch(*m_lua, m_inCallback, "key", key, action, mods);
}

void ClientMods::rightClickBlock(int x, int y, int z, const std::string& block) {
    if (active() && !m_where.empty()) callDispatch(*m_lua, m_inCallback, "right_click_block", x, y, z, block);
}

void ClientMods::blockDug(int x, int y, int z, const std::string& block) {
    if (active() && !m_where.empty()) callDispatch(*m_lua, m_inCallback, "dig_block", x, y, z, block);
}

void ClientMods::hud(float W, float H) {
    if (active() && !m_where.empty()) callDispatch(*m_lua, m_inCallback, "hud", W, H);
}

void ClientMods::chatMessage(const std::string& text) {
    if (m_inCallback || !active() || m_where.empty()) return;   // not our own messages
    callDispatch(*m_lua, m_inCallback, "chat_message", text);
}

bool ClientMods::command(const std::string& text) {
    if (!m_lua || m_modNames.empty()) return false;          // no client mods: "." is just text
    if (!m_rules.allowed) {
        if (showChat) showChat("Client mods are switched off on this server.", 1.0f, 0.75f, 0.4f);
        return true;
    }
    sol::protected_function f = (*m_lua)["__client_command"];
    if (!f.valid()) return false;
    m_inCallback = true;
    sol::protected_function_result r = f(text);
    m_inCallback = false;
    if (!r.valid()) {
        sol::error err = r;
        std::cerr << "[Client mods] command: " << err.what() << std::endl;
        return true;
    }
    return r.get_type() == sol::type::boolean && r.get<bool>();
}
