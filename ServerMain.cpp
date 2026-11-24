// BlockSmithServer - the BlockSmith engine's dedicated (headless) multiplayer server, for any game made with it.
//
// No window, no graphics card: it loads a game's mods, opens a world, and runs it at a
// fixed tick rate, sleeping in between, so it uses very little CPU while nobody is moving.
// Players join it from the Multiplayer screen like any other server.
//
// The server's owner decides everything in server.conf (created with defaults the first
// time) and from the console (type "help"):
//   - name, description (motd) and icon shown in everyone's server list
//   - which game to run and which world, the seed for a new world, port, max players
//   - welcome message, spawn point, view distance limit, whitelist and bans
//
// Several servers (even for different games) can run from one install:
//   BlockSmithServer --game skyblock --world sky --port 30001
//   BlockSmithServer --config survival.conf
//   BlockSmithServer --list-games

#include "World.h"
#include "Version.h"
#include "UpdateCheck.h"
#include "Diagnostics.h"
#include "ChunkData.h"
#include "CaveGenerator.h"
#include "CaveLua.h"
#include "Block.h"
#include "BlockDefs.h"
#include "DimensionsLua.h"
#include "BlockTextLua.h"
#include "BlockStateLua.h"
#include "DimensionWorlds.h"
#include "Net.h"
#include "Sha256.h"
#include "ChunkGenerator.h"
#include "Fluids.h"
#include "ModOrder.h"
#include "SafeReplace.h"
#include "ClientModRules.h"
#include "Auth.h"
#include "LuaSandbox.h"
#include "ServerLuaApi.h"
#include "EntityLua.h"
#include "BlockModifiers.h"
#include "Light.h"
#include "RemoteLua.h"
#include "ConsoleInput.h"

#include <sol/sol.hpp>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <chrono>
#include <csignal>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <ctime>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

// ===========================================================================
// Small helpers
// ===========================================================================
std::string trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    const size_t e = s.find_last_not_of(" \t\r\n");
    s.erase(e == std::string::npos ? 0 : e + 1);
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int toInt(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

bool toBool(const std::string& s, bool fallback) {
    const std::string v = lower(trim(s));
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return fallback;
}

std::map<std::string, std::string> readConfFile(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return out;
}

// Changes one "key = value" line in a conf file, keeping everything else (comments too).
void updateConfValue(const fs::path& file, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    bool found = false;
    for (auto& line : lines) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq != std::string::npos && trim(t.substr(0, eq)) == key) {
            line = key + " = " + value;
            found = true;
            break;
        }
    }
    if (!found) lines.push_back(key + " = " + value);
    std::ofstream out(file, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
}

fs::path findFolder(const std::string& rel) {
    for (const std::string& p : {rel, "../" + rel, "../../" + rel}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) return fs::path(p);
    }
    return fs::path(rel);
}

std::string safeName(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    if (out.empty()) out = "world";
    if (out.size() > 32) out.resize(32);
    return out;
}

// ===========================================================================
// Games (any game made with the engine: games/<folder>/game.conf)
// ===========================================================================
struct GameInfo {
    std::string folder;
    std::string title;
    std::string description;
};

GameInfo readGameInfo(const fs::path& dir) {
    GameInfo g;
    g.folder = dir.filename().string();
    g.title = g.folder;
    fs::path conf = dir / "game.conf";
    if (!fs::exists(conf)) conf = dir / "mod.conf";
    auto kv = readConfFile(conf);
    if (!kv["title"].empty()) g.title = kv["title"];
    else if (!kv["name"].empty()) g.title = kv["name"];
    g.description = kv["description"];
    return g;
}

std::vector<GameInfo> listGames() {
    std::vector<GameInfo> games;
    const fs::path root = findFolder("games");
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return games;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory()) games.push_back(readGameInfo(entry.path()));
    }
    std::sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) { return a.folder < b.folder; });
    return games;
}

void printGames() {
    const auto games = listGames();
    if (games.empty()) {
        std::cout << "No games found (looked for a games/ folder)." << std::endl;
        return;
    }
    std::cout << "Installed games (use the folder name for 'game' / --game):" << std::endl;
    for (const auto& g : games) {
        std::cout << "  " << g.folder << "  -  " << g.title
                  << (g.description.empty() ? "" : "  (" + g.description + ")") << std::endl;
    }
}

// ===========================================================================
// Settings (server.conf)
// ===========================================================================
struct ServerConfig {
    std::string name = "BlockSmith Server";
    std::string motd = "{gold}{b}A BlockSmith server{/b}{n}{gray}Running on a dedicated server";
    std::string icon = "server_icon.png";
    std::string game = "craft_and_ruin";
    std::string world = "world";
    std::string seed;
    int port = 30000;
    int maxPlayers = 50;
    int viewDistance = 8;
    int spawnX = 8, spawnZ = 8;
    std::string welcome = "{gold}Welcome to the server, {player}!";
    bool whitelist = false;
    bool requirePassword = false;
    std::string gameMode = "survival";   // mode for players who haven't been given another
    int generatorThreads = 0;   // 0 = automatic (CPU cores - 1)
    std::set<std::string> disabledMods;
    int tickRate = 30;
    int autosaveSeconds = 60;
    double dayLengthMinutes = 20.0;   // one whole day (DayTime.h); 'time' in the console
    int spawnProtection = 16;         // blocks around spawn only operators may change (0 = off)
    int opLevel = 4;                  // the level 'op <name>' gives when no level is said (1-4)
    double luaTimeLimit = 5.0;   // seconds one mod event may run before it's stopped (0 = no limit)
    bool remoteContent = true;   // mods may download data (JSON, text, pictures, sounds) - never code
    std::vector<std::string> remoteDomains;   // approved besides GitHub
    bool updateCheck = true;                  // look for a newer BlockSmith now and then (data only)
    std::string updateUrl = update::kDefaultUrl;
    ClientModRules clientMods;   // what players' own client mods may do here
};

void writeDefaultConfig(const fs::path& file, const ServerConfig& c) {
    std::ofstream out(file);
    out << "# BlockSmith dedicated server settings.\n"
        << "# Most of these can be changed while the server runs: edit this file, then type\n"
        << "# 'reload' in the server window. (game, world, seed and port need a restart.)\n"
        << "\n"
        << "# --- How the server looks in everyone's server list ---\n"
        << "# Shown on the LAN list\n"
        << "name = " << c.name << "\n"
        << "# Description under the server's name. Colours: {gold} {red} {aqua} {#ff8800} ...\n"
        << "#   {b}bold{/b}   {n} = second line   (also: type 'motd <text>' in the console)\n"
        << "motd = " << c.motd << "\n"
        << "# Icon file next to this one: a PNG, 64x64 recommended, up to 64 KB\n"
        << "icon = " << c.icon << "\n"
        << "\n"
        << "# --- What runs ---\n"
        << "# The game (a folder in games/). Players must have the same game installed and selected.\n"
        << "# Type 'games' in the console (or start with --list-games) to see what's installed.\n"
        << "game = " << c.game << "\n"
        << "# World folder in saves/ (created if it doesn't exist)\n"
        << "world = " << c.world << "\n"
        << "# Seed for a new world (number or any text; empty = random)\n"
        << "seed = " << c.seed << "\n"
        << "port = " << c.port << "\n"
        << "\n"
        << "# --- Players ---\n"
        << "max_players = " << c.maxPlayers << "\n"
        << "# Furthest players may see, in chunks (2-" << net::kMaxViewDistance << "). Lower = less work for the server.\n"
        << "view_distance = " << c.viewDistance << "\n"
        << "# Where new players appear (x z); the ground height is found automatically\n"
        << "spawn = " << c.spawnX << " " << c.spawnZ << "\n"
        << "# Sent to each player when they join. {player} = their name. Empty = none.\n"
        << "welcome = " << c.welcome << "\n"
        << "# Only players listed in whitelist.txt may join ('whitelist add <name>' in the console)\n"
        << "whitelist = " << (c.whitelist ? "true" : "false") << "\n"
        << "# Players must set a password (Settings > Player in the game) to join. Either way, a\n"
        << "# name that joined once with a password can only be used with that password again.\n"
        << "# Recommended for public servers.\n"
        << "require_password = " << (c.requirePassword ? "true" : "false") << "\n"
        << "# Game mode for players: survival or creative. Only operators can change a player's\n"
        << "# mode, with /gamemode <mode> [name] (saved in gamemodes.txt). Players can't switch themselves.\n"
        << "gamemode = " << c.gameMode << "\n"
        << "\n"
        << "# --- Mods ---\n"
        << "# The server loads the game's mods and everything in mods/, except these\n"
        << "# (comma separated mod folder names). Players need the same mods.\n"
        << "disabled_mods = \n"
        << "\n"
        << "# --- Players' client mods ---\n"
        << "# Players can run their own scripts in their game (clientmods/ folder: minimaps, HUDs,\n"
        << "# waypoints...). Decide what they may do here. Honest games keep to this; it can't stop\n"
        << "# a modified game.\n"
        << "client_mods = " << (c.clientMods.allowed ? "true" : "false") << "\n"
        << "# May client mods send chat messages and commands for the player?\n"
        << "client_mods_chat = " << (c.clientMods.chat ? "true" : "false") << "\n"
        << "# May they see other players' names and positions?\n"
        << "client_mods_players = " << (c.clientMods.players ? "true" : "false") << "\n"
        << "# How far from the player they can read blocks (0 = any distance)\n"
        << "client_mods_block_range = " << c.clientMods.blockRange << "\n"
        << "# May they send messages to this server's mods (client.send_to_server)? The mods check\n"
        << "# everything they get, as they do anything from a player's game.\n"
        << "client_mods_messages = " << (c.clientMods.messages ? "true" : "false") << "\n"
        << "\n"
        << "# --- Performance ---\n"
        << "# Updates per second (20-60). Higher = smoother, more CPU.\n"
        << "tick_rate = " << c.tickRate << "\n"
        << "# Threads that generate new terrain (0 = automatic: CPU cores - 1)\n"
        << "generator_threads = 0\n"
        << "autosave_seconds = " << c.autosaveSeconds << "\n"
        << "# How long a whole day (day + night) lasts, in minutes. 'time' in the console sets the\n"
        << "# time of day, or stops it ('time speed 0')\n"
        << "day_length = " << c.dayLengthMinutes << "\n"
        << "# Blocks around the spawn point that only operators can change (0 = off). As in\n"
        << "# Minecraft, it's off while there are no operators.\n"
        << "spawn_protection = " << c.spawnProtection << "\n"
        << "# The level 'op <name>' gives: 1 build at spawn, 2 also game commands (gamemode, tp,\n"
        << "# give, time...), 3 also player commands (kick, ban, op...), 4 everything (stop, reload...)\n"
        << "op_level = " << c.opLevel << "\n"
        << "# A mod's event (on_update, on_player_chat...) that runs longer than this many seconds is\n"
        << "# stopped with an error instead of freezing the server (0 = no limit)\n"
        << "lua_time_limit = " << c.luaTimeLimit << "\n"
        << "\n"
        << "# --- Online mod content ---\n"
        << "# Mods may download DATA (JSON, text, PNG/JPEG pictures, OGG/WAV sounds) from GitHub\n"
        << "# (raw files, GitHub Pages) - never code. Each mod lists its sources in its mod.conf.\n"
        << "remote_content = " << (c.remoteContent ? "true" : "false") << "\n"
        << "# Other sites mods may download from (comma separated, e.g. example.com, *.mysite.org)\n"
        << "remote_domains = \n"
        << "\n"
        << "# --- Updates ---\n"
        << "# Check now and then whether a newer BlockSmith is out (only a small version file is read;\n"
        << "# nothing is downloaded or installed - the console says where to get it)\n"
        << "update_check = " << (c.updateCheck ? "true" : "false") << "\n"
        << "update_url = " << c.updateUrl << "\n";
}

ServerConfig parseConfig(const std::map<std::string, std::string>& kvIn) {
    auto kv = kvIn;
    ServerConfig c;
    if (kv.count("name") && !kv["name"].empty()) c.name = kv["name"];
    if (kv.count("motd")) c.motd = kv["motd"];
    if (kv.count("icon")) c.icon = kv["icon"];
    if (kv.count("game") && !kv["game"].empty()) c.game = kv["game"];
    if (kv.count("world") && !kv["world"].empty()) c.world = kv["world"];
    if (kv.count("seed")) c.seed = kv["seed"];
    c.port = std::clamp(toInt(kv["port"], c.port), 1, 65535);
    c.maxPlayers = std::clamp(toInt(kv["max_players"], c.maxPlayers), 1, 4000);
    c.viewDistance = std::clamp(toInt(kv["view_distance"], c.viewDistance), 2, net::kMaxViewDistance);
    if (kv.count("spawn")) {
        std::istringstream s(kv["spawn"]);
        int x = c.spawnX, z = c.spawnZ;
        if (s >> x >> z) { c.spawnX = x; c.spawnZ = z; }
    }
    if (kv.count("welcome")) c.welcome = kv["welcome"];
    c.whitelist = toBool(kv["whitelist"], c.whitelist);
    c.requirePassword = toBool(kv["require_password"], c.requirePassword);
    if (kv.count("gamemode")) {
        const std::string m = lower(trim(kv["gamemode"]));
        if (m == "creative" || m == "1" || m == "c") c.gameMode = "creative";
        else if (m == "survival" || m == "0" || m == "s") c.gameMode = "survival";
    }
    c.generatorThreads = std::clamp(toInt(kv["generator_threads"], 0), 0, 32);
    {
        std::istringstream mods(kv["disabled_mods"]);
        std::string id;
        while (std::getline(mods, id, ',')) {
            id = trim(id);
            if (!id.empty()) c.disabledMods.insert(id);
        }
    }
    c.tickRate = std::clamp(toInt(kv["tick_rate"], c.tickRate), 10, 120);
    c.autosaveSeconds = std::clamp(toInt(kv["autosave_seconds"], c.autosaveSeconds), 10, 3600);
    c.spawnProtection = std::clamp(toInt(kv["spawn_protection"], c.spawnProtection), 0, 10000);
    c.opLevel = std::clamp(toInt(kv["op_level"], c.opLevel), 1, 4);
    // A number from server.conf, only if the whole value is one: "nan", "inf", "5abc" or
    // "1.5 minutes" are refused (the default stays), so they can never reach the clock or the
    // Lua time limit. (readConfFile has already trimmed the spaces around it.)
    auto finiteNumber = [&kv](const char* key, double& out) {
        if (!kv.count(key)) return false;
        const std::string& text = kv[key];
        try {
            size_t used = 0;
            const double v = std::stod(text, &used);
            if (used == text.size() && std::isfinite(v)) { out = v; return true; }
        } catch (...) {}
        std::cerr << "[Server] server.conf: " << key << " = \"" << kv[key] << "\" isn't a number - using the default" << std::endl;
        return false;
    };
    if (double v = 0.0; finiteNumber("day_length", v)) c.dayLengthMinutes = std::clamp(v, 0.5, 100000.0);
    if (kv.count("remote_content")) c.remoteContent = toBool(kv["remote_content"], c.remoteContent);
    if (kv.count("update_check")) c.updateCheck = toBool(kv["update_check"], c.updateCheck);
    if (kv.count("update_url") && !kv["update_url"].empty()) c.updateUrl = kv["update_url"];
    {
        std::istringstream doms(kv["remote_domains"]);
        std::string d;
        while (std::getline(doms, d, ',')) {
            d = lower(trim(d));
            if (d.rfind("https://", 0) == 0) d = d.substr(8);
            if (d.rfind("*.", 0) == 0) d = d.substr(2);
            while (!d.empty() && d.back() == '/') d.pop_back();
            if (!d.empty()) c.remoteDomains.push_back(d);
        }
    }
    if (double v = 0.0; finiteNumber("lua_time_limit", v)) c.luaTimeLimit = std::clamp(v, 0.0, 600.0);
    c.clientMods = ClientModRules::fromConfig(kv, "client_mods", c.clientMods);
    return c;
}

// ===========================================================================
// Server icon: checked so the owner hears about problems instead of a blank icon
// ===========================================================================
std::vector<uint8_t> loadIcon(const fs::path& file) {
    std::vector<uint8_t> bytes;
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) {
        std::cout << "[Server] No icon (put a 64x64 PNG at " << file.string() << " for one)" << std::endl;
        return bytes;
    }
    if (fs::file_size(file, ec) > net::kMaxIconBytes) {
        std::cout << "[Server] Icon " << file.string() << " is too big (max " << net::kMaxIconBytes / 1024
                  << " KB) - not using it" << std::endl;
        return bytes;
    }
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    static const uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 24 || !std::equal(pngSig, pngSig + 8, bytes.begin())) {
        std::cout << "[Server] Icon " << file.string() << " isn't a PNG image - not using it" << std::endl;
        bytes.clear();
        return bytes;
    }
    auto be32 = [&bytes](size_t p) {
        return (uint32_t(bytes[p]) << 24) | (uint32_t(bytes[p + 1]) << 16) | (uint32_t(bytes[p + 2]) << 8) | bytes[p + 3];
    };
    const uint32_t w = be32(16), h = be32(20);   // from the PNG's IHDR chunk
    std::cout << "[Server] Icon: " << file.string() << " (" << w << "x" << h << ")" << std::endl;
    if (w != h) std::cout << "[Server]   It isn't square, so it will look stretched." << std::endl;
    else if (w != 64) std::cout << "[Server]   Tip: 64x64 looks sharpest." << std::endl;
    return bytes;
}

// ===========================================================================
// Name lists next to server.conf: whitelist.txt (one name per line) and
// banned.txt ("name | reason")
// ===========================================================================
struct NameList {
    fs::path file;
    std::map<std::string, std::string> entries;   // lower-case name -> "Name|reason"

    void load() {
        entries.clear();
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const size_t bar = line.find('|');
            const std::string name = trim(line.substr(0, bar));
            const std::string reason = bar == std::string::npos ? "" : trim(line.substr(bar + 1));
            if (!name.empty()) entries[lower(name)] = name + "|" + reason;
        }
    }
    void save() const {
        std::ofstream out(file, std::ios::trunc);
        for (const auto& [key, v] : entries) {
            const size_t bar = v.find('|');
            const std::string reason = v.substr(bar + 1);
            out << v.substr(0, bar) << (reason.empty() ? "" : " | " + reason) << "\n";
        }
    }
    bool has(const std::string& name) const { return entries.count(lower(name)) > 0; }
    std::string reason(const std::string& name) const {
        auto it = entries.find(lower(name));
        return it == entries.end() ? "" : it->second.substr(it->second.find('|') + 1);
    }
    void add(const std::string& name, const std::string& why = "") { entries[lower(name)] = name + "|" + why; save(); }
    bool remove(const std::string& name) { const bool had = entries.erase(lower(name)) > 0; if (had) save(); return had; }
    std::string names() const {
        std::string out;
        for (const auto& [k, v] : entries) out += (out.empty() ? "" : ", ") + v.substr(0, v.find('|'));
        return out.empty() ? "(nobody)" : out;
    }
};

// ===========================================================================
// Accounts (accounts.txt next to server.conf): a name belongs to whoever first joined with a
// password. Each line is "name | v2 | key": the key is this server's own key for that player
// (Auth.h) - derived from their password and THIS server's identity, so it is no use on any
// other server, and it is never sent again after the account is made: logins answer a fresh
// challenge with it instead.
// Lines from before protocol 8 ("name | salt | hash") can't check the new logins: those players
// are asked to have an operator reset their password (password reset <name>).
// ===========================================================================
struct Accounts {
    fs::path file;
    struct Entry { std::string name, format, key; };   // format "v2", or a legacy salt
    std::map<std::string, Entry> entries;   // lower-case name -> entry

    void load() {
        entries.clear();
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            Entry e;
            std::string part;
            std::vector<std::string> parts;
            while (std::getline(ss, part, '|')) parts.push_back(trim(part));
            if (parts.size() != 3) continue;
            e.name = parts[0]; e.format = parts[1]; e.key = parts[2];
            entries[lower(e.name)] = e;
        }
    }
    void save() const {
        std::ofstream out(file, std::ios::trunc);
        out << "# Player accounts: name | v2 | this server's key for them (not their password, and no use\n"
            << "# on other servers). Delete a line, or type 'password reset <name>', to reset a password.\n";
        for (const auto& [k, e] : entries) out << e.name << " | " << e.format << " | " << e.key << "\n";
    }
    bool has(const std::string& name) const { return entries.count(lower(name)) > 0; }
    // Saved before protocol 8: can't be checked any more (the player needs a reset)
    bool legacy(const std::string& name) const {
        auto it = entries.find(lower(name));
        return it != entries.end() && it->second.format != "v2";
    }
    // Does this login's answer prove the password? (the challenge is inside `proof`)
    bool check(const std::string& name, const net::AuthProof& proof) const {
        auto it = entries.find(lower(name));
        if (it == entries.end() || it->second.format != "v2" || !proof.hasPassword()) return false;
        return proof.matches(it->second.key, name);
    }
    // A new account from a first login: the key must be well-formed and must itself answer this
    // connection's challenge (so it really is the key the player's game will use from now on)
    bool create(const std::string& name, const net::AuthProof& proof) {
        const std::string key = auth::toLower(proof.registerKey);
        if (key.size() != 64 || auth::fromHex(key).size() != 32 || !proof.matches(key, name)) return false;
        entries[lower(name)] = Entry{name, "v2", key};
        save();
        return true;
    }
    bool remove(const std::string& name) {
        const bool had = entries.erase(lower(name)) > 0;
        if (had) save();
        return had;
    }
};

// This server's identity (server_id.txt next to server.conf), made once: every password answer
// is bound to it, so answers and keys from here are worthless on any other server.
std::string loadOrCreateServerId(const fs::path& file) {
    std::ifstream in(file);
    std::string id;
    std::getline(in, id);
    id = trim(id);
    if (id.size() >= 16 && id.size() <= 64) return id;
    id = auth::randomHex(16);
    std::ofstream out(file, std::ios::trunc);
    out << id << "\n";
    std::cout << "[Server] New server identity " << id << " (" << file.string() << "). Keep this file: "
              << "players' passwords here are tied to it." << std::endl;
    return id;
}

// ===========================================================================
// Mods available to a game (its own mods + the global mods/ folder)
// ===========================================================================
bool modIsRequired(const fs::path& modFolder, const std::string& id);

struct ModEntry { std::string id, title, source; bool required = false; };

std::vector<ModEntry> listServerMods(const std::string& game) {
    std::vector<ModEntry> out;
    std::set<std::string> seen;
    auto scan = [&](const fs::path& root, const std::string& source) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) return;
        std::vector<fs::path> paths;
        for (const auto& e : fs::directory_iterator(root, ec)) paths.push_back(e.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& p : paths) {
            ModEntry m;
            if (fs::is_directory(p, ec)) m.id = p.filename().string();
            else if (p.extension() == ".lua") m.id = p.stem().string();
            else continue;
            if (!seen.insert(m.id).second) continue;
            m.source = source;
            m.title = m.id;
            auto conf = readConfFile(p / "mod.conf");
            if (!conf["title"].empty()) m.title = conf["title"];
            else if (!conf["name"].empty()) m.title = conf["name"];
            m.required = modIsRequired(p, m.id);
            out.push_back(m);
        }
    };
    scan(findFolder("games/" + game) / "mods", "game");
    scan(findFolder("mods"), "global");
    return out;
}

std::string joinIds(const std::set<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) out += (out.empty() ? "" : ", ") + id;
    return out;
}

// ===========================================================================
// World files (same layout as the game, so worlds can be copied either way)
// ===========================================================================
uint32_t seedFromText(std::string t) {
    t = trim(t);
    if (t.empty()) {
        std::random_device rd;
        return rd() ^ (rd() << 16);
    }
    const bool numeric = t.size() <= 10 &&
        std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        const unsigned long long v = std::stoull(t);
        if (v <= 0xFFFFFFFFull) return static_cast<uint32_t>(v);
    }
    uint32_t h = 2166136261u;
    for (unsigned char c : t) { h ^= c; h *= 16777619u; }
    return h;
}

// world.dat: keeps lines it doesn't use (a singleplayer world's player/hotbar lines)
struct WorldMeta {
    bool exists = false;
    uint32_t seed = 0;
    std::string game;
    std::string timeLine;                    // "time=<days> <speed>" (DayTime.h)
    std::vector<std::string> otherLines;
};

WorldMeta readWorldMeta(const fs::path& file) {
    WorldMeta m;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("seed=", 0) == 0) {
            try { m.seed = static_cast<uint32_t>(std::stoul(line.substr(5))); m.exists = true; } catch (...) {}
        } else if (line.rfind("game=", 0) == 0) {
            m.game = line.substr(5);
        } else if (line.rfind("time=", 0) == 0) {
            m.timeLine = line;
        } else if (line.rfind("version=", 0) != 0 && !line.empty()) {
            m.otherLines.push_back(line);
        }
    }
    return m;
}

bool writeWorldMeta(const fs::path& file, const WorldMeta& m) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "version=1\n";
        if (!m.game.empty()) out << "game=" << m.game << "\n";
        out << "seed=" << m.seed << "\n";
        if (!m.timeLine.empty()) out << m.timeLine << "\n";
        for (const auto& l : m.otherLines) out << l << "\n";
        if (!out) return false;
    }
    if (!replaceFile(tmp, file, ec)) {   // the old file stays as it was
        std::cerr << "[Server] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

fs::path playerFile(const fs::path& worldDir, const std::string& name) {
    return worldDir / "players" / (name + ".txt");
}

bool loadPlayer(const fs::path& worldDir, const std::string& name, glm::vec3& pos, std::string* dimension = nullptr) {
    std::ifstream in(playerFile(worldDir, name));
    std::string key;
    if (!((in >> key >> pos.x >> pos.y >> pos.z) && key == "pos")) return false;
    // "dim <name>": the dimension they were in (none: the overworld)
    if (dimension) {
        dimension->clear();
        std::string k, d;
        if ((in >> k >> d) && k == "dim" && dims::validName(d) && d != dims::kOverworld) *dimension = d;
    }
    return true;
}

void savePlayer(const fs::path& worldDir, const std::string& name, const glm::vec3& pos, const std::string& dimension = "") {
    std::error_code ec;
    fs::create_directories(worldDir / "players", ec);
    std::ofstream out(playerFile(worldDir, name), std::ios::trunc);
    out << std::fixed << std::setprecision(3) << "pos " << pos.x << " " << pos.y << " " << pos.z << "\n";
    if (!dimension.empty()) out << "dim " << dimension << "\n";
}

// ===========================================================================
// Mods (same rules as the game's loader in main.cpp)
// ===========================================================================
bool modIsRequired(const fs::path& modFolder, const std::string& id) {
    if (id == "base") return true;
    std::error_code ec;
    const fs::path conf = modFolder / "mod.conf";
    if (!fs::exists(conf, ec)) return false;
    return readConfFile(conf)["required"] == "true";
}

// Which mods load is up to the server's owner: everything in the game's mods and the
// global mods/ folder, except the ones listed in disabled_mods (required mods always load).
std::vector<fs::path> findModFiles(const std::string& game, const std::set<std::string>& disabled) {
    std::vector<fs::path> modFiles;
    std::set<std::string> initFolders, seenRoots;
    const std::vector<std::string> searchPaths = {"mods", "../mods", "../../mods", "games/" + game + "/mods"};

    for (const auto& searchPath : searchPaths) {
        std::string resolved = searchPath;
        if (!fs::exists(resolved) && fs::exists("../../" + searchPath)) resolved = "../../" + searchPath;
        if (!fs::exists(resolved)) continue;
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(resolved, ec);
        if (!seenRoots.insert((ec ? fs::path(resolved) : canon).string()).second) continue;
        std::cout << "[Mod] Mod folder: " << resolved << std::endl;

        std::set<std::string> reported;
        const size_t rootStart = modFiles.size();

        for (const auto& entry : fs::recursive_directory_iterator(resolved)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
            std::error_code relEc;
            const fs::path rel = fs::relative(entry.path(), resolved, relEc);
            std::string id = (relEc || rel.empty()) ? "" : rel.begin()->string();
            if (id == "clientmods") continue;   // players' client mods, if someone put them in mods/
            if (!id.empty() && std::next(rel.begin()) == rel.end()) id = fs::path(id).stem().string();   // single-file mod
            if (!id.empty() && disabled.count(id)) {
                const bool required = modIsRequired(fs::path(resolved) / id, id);
                if (!required) {
                    if (reported.insert(id).second) std::cout << "[Mod] Not loading (disabled_mods): " << id << std::endl;
                    continue;
                }
                if (reported.insert(id).second) std::cout << "[Mod] " << id << " is required - loading it anyway" << std::endl;
            }
            if (entry.path().filename() == "init.lua") initFolders.insert(entry.path().parent_path().string());
            modFiles.push_back(entry.path());
        }
        modorder::sortRoot(modFiles, rootStart);   // alphabetical, the same as the game
    }
    modorder::applyDepends(modFiles);              // mod.conf depends = ... loads after those mods

    auto insideInitFolder = [&initFolders](const fs::path& file) {
        for (fs::path p = file.parent_path(); ; p = p.parent_path()) {
            if (initFolders.count(p.string())) return true;
            if (p.empty() || p == p.parent_path()) return false;
        }
    };
    std::vector<fs::path> order;
    for (const auto& f : modFiles) if (f.filename() == "init.lua") order.push_back(f);
    for (const auto& f : modFiles) {
        if (f.filename() == "init.lua" || insideInitFolder(f)) continue;
        order.push_back(f);
    }
    return order;
}

// ===========================================================================
// Console input on its own thread, so the server keeps ticking while you type
// ===========================================================================
std::atomic<bool> g_Running{true};
std::mutex g_ConsoleMutex;
std::deque<std::string> g_ConsoleLines;

void onSignal(int) { g_Running = false; }   // Ctrl+C: save and stop

// The prompt (ConsoleInput.h): line editing, history, and Tab completion of commands and their
// arguments. Completion reads this snapshot, which the main loop refreshes (the console has its
// own thread, and mustn't touch the server's lists while the main thread changes them).
console::LineEditor g_Console;
struct CompletionData {
    std::vector<std::string> modCommands;   // chat.register_command names
    std::vector<std::string> players;       // online
    std::vector<std::string> banned, whitelisted, ops, accounts;
    std::vector<std::string> modsOn, modsOff;   // "mods disable" / "mods enable" can change these
    std::vector<std::string> items;             // every block and item (give)
    std::vector<std::string> bannedIps;
};
std::mutex g_CompletionMutex;
CompletionData g_Completion;

// The console's commands, in the order 'help' shows them (respawn is the game's own)
const std::vector<std::string> kConsoleCommands = {
    "stop", "save", "save-all", "save-on", "save-off", "reload", "tps", "seed", "version", "diag", "report", "reports", "games", "mods", "motd", "name",
    "list", "say", "me", "msg", "tell", "kick", "ban", "unban", "pardon", "ban-ip", "pardon-ip", "banlist",
    "whitelist", "op", "deop", "ops", "password", "gamemode", "gm", "time", "gamerule", "tp", "teleport",
    "give", "clear", "kill", "setworldspawn", "spawnpoint", "help"};

// Game rules (/gamerule): saved with the world (gamerules.txt) and shared with every player as
// "gamerule.<name>" (engine.get_shared), so mods - like the survival mod - follow them.
// doDaylightCycle is the server's own (the clock stops). "" default = the game decides.
struct GameRuleDef { const char* name; const char* defaultValue; const char* about; };
const GameRuleDef kGameRules[] = {
    { "doDaylightCycle",     "true", "the time of day moves on" },
    { "keepInventory",       "",     "players keep their items when they die (not set: the game decides)" },
    { "naturalRegeneration", "true", "health comes back when you're well fed" },
    { "fallDamage",          "true", "falling hurts" },
    { "drowningDamage",      "true", "running out of air underwater hurts" },
    { "fireDamage",          "true", "lava (and fire) hurts" },
    { "doMobSpawning",       "true", "mobs appear by themselves (entities.register_spawn)" },
    { "randomTickSpeed",     "3",    "random blocks per 16x16x16 section per tick: how fast grass spreads, crops grow... (0 = never)" },
};
const GameRuleDef* findGameRule(const std::string& name) {
    for (const auto& g : kGameRules) {
        std::string a = g.name, b = name;
        for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (a == b) return &g;
    }
    return nullptr;
}

// logs/latest.log next to server.conf: everything the console shows, each line with the time.
// The previous log is kept as logs/<date>-<n>.log. (Diagnostics.h: shared with the game.)
void startLogFile(const fs::path& dir) { diag::startLogFile(dir); }

// What Tab offers: `before` are the words before the one being typed
std::vector<std::string> completeConsole(const std::vector<std::string>& before, const std::string&) {
    std::lock_guard<std::mutex> lock(g_CompletionMutex);
    const CompletionData& d = g_Completion;
    if (before.empty()) {                                  // the command
        std::vector<std::string> out = kConsoleCommands;
        out.insert(out.end(), d.modCommands.begin(), d.modCommands.end());
        return out;
    }
    std::string cmd = before[0];
    for (char& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t arg = before.size();                      // 1 = the first argument
    auto lowerWord = [&](size_t i) {
        std::string w = i < before.size() ? before[i] : "";
        for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return w;
    };
    auto join = [](std::vector<std::string> a, const std::vector<std::string>& b) {
        for (const auto& x : b) if (std::find(a.begin(), a.end(), x) == a.end()) a.push_back(x);
        return a;
    };
    std::vector<std::string> targets = d.players;   // player arguments: names or @a @p @r @s
    for (const char* sel : { "@a", "@p", "@r", "@s" }) targets.push_back(sel);
    std::vector<std::string> rules;
    for (const auto& g : kGameRules) rules.push_back(g.name);
    if (arg == 1) {
        if (cmd == "tp" || cmd == "teleport" || cmd == "give" || cmd == "clear" || cmd == "kill" || cmd == "spawnpoint")
            return targets;
        if (cmd == "gamerule") return rules;
        if (cmd == "ban-ip") return d.players;
        if (cmd == "pardon-ip") return d.bannedIps;
        if (cmd == "banlist") return {"players", "ips"};
        if (cmd == "kick" || cmd == "msg" || cmd == "tell" || cmd == "report") return d.players;
        if (cmd == "reports") return {"all", "show", "close", "reopen", "delete"};
        if (cmd == "ban") return join(d.players, d.accounts);
        if (cmd == "op") return join(join(d.players, d.accounts), d.whitelisted);
        if (cmd == "deop") return d.ops;
        if (cmd == "unban" || cmd == "pardon") return d.banned;
        if (cmd == "whitelist") return {"on", "off", "add", "remove", "list"};
        if (cmd == "gamemode" || cmd == "gm") return {"survival", "creative"};
        if (cmd == "password") return {"reset"};
        if (cmd == "mods") return {"list", "enable", "disable"};
        if (cmd == "time") return {"set", "add", "speed", "query"};
        if (cmd == "help") return {};
    }
    if (arg == 2) {
        const std::string sub = lowerWord(1);
        if (cmd == "whitelist" && sub == "add") return join(d.players, d.accounts);
        if (cmd == "whitelist" && sub == "remove") return d.whitelisted;
        if (cmd == "gamemode" || cmd == "gm") return join(targets, d.accounts);
        if (cmd == "give") return d.items;
        if (cmd == "tp" || cmd == "teleport") return targets;
        if (cmd == "gamerule") {
            const GameRuleDef* g = findGameRule(before[1]);
            if (g && std::isdigit(static_cast<unsigned char>(g->defaultValue[0]))) return {"0", "3", "10", "default"};
            if (g) return {"true", "false"};
        }
        if (cmd == "password" && sub == "reset") return d.accounts;
        if (cmd == "mods" && sub == "enable") return d.modsOff;
        if (cmd == "time" && sub == "set") return {"day", "noon", "night", "midnight", "sunrise", "sunset"};
        if (cmd == "mods" && sub == "disable") return d.modsOn;
    }
    return {};
}

std::string stripTags(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            const size_t close = s.find('}', i);
            if (close != std::string::npos && close - i <= 9) { i = close; continue; }
        }
        out += s[i];
    }
    return out;
}

// ===========================================================================
// Lua for mods, without the parts that only make sense in a game window
// ===========================================================================
const char* kNoopLua = R"(
local function noop_table()
    local t = {}
    return setmetatable(t, {
        __index = function(_, _) return noop_table() end,
        __call = function() return nil end,
    })
end
_server_noop = noop_table
for _, name in ipairs({ "ui", "hotbar", "sky", "clouds", "fog", "underwater", "menu", "sound", "remote" }) do
    _G[name] = noop_table()
end
local function fallback(t)
    return setmetatable(t, { __index = function(_, _) return function() return nil end end })
end
fallback(engine)
fallback(world)
fallback(chat)
)";

struct ServerContext {
    World* world = nullptr;                   // the overworld
    DimensionWorlds* dims = nullptr;          // the other dimensions' worlds
    // The world of the dimension code is running in now (Dimensions.h: dims::current())
    World* here() const {
        const std::string& d = dims::current();
        if (d.empty() || !dims) return world;
        World* w = dims->get(d);
        return w ? w : world;
    }
    GameServer* server = nullptr;
    ServerConfig* config = nullptr;
    DayClock* clock = nullptr;                // the time of day (sky.get_time / set_time)
    GameInfo game;
    std::vector<std::string> sharedChanged;   // for on_shared_changed
    struct ModCommand { std::string name, usage, description; };
    std::vector<ModCommand> modCommands;      // chat.register_command (listed by help)
    // engine.get_storage / set_storage: the world's mod_storage.txt (same file as in the game)
    std::map<std::string, std::string> storage;
    bool storageDirty = false;
    ServerLuaApi api;
};

// mod_storage.txt: "key<TAB>value" lines, \\ \\n \\t escaped (the same as the game's)
std::string escapeStorage(const std::string& v) {
    std::string o;
    for (char ch : v) {
        if (ch == '\\') o += "\\\\";
        else if (ch == '\n') o += "\\n";
        else if (ch == '\t') o += "\\t";
        else o += ch;
    }
    return o;
}
std::string unescapeStorage(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[++i];
            o += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else {
            o += v[i];
        }
    }
    return o;
}
void loadStorage(const fs::path& file, std::map<std::string, std::string>& out) {
    out.clear();
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) out[unescapeStorage(line.substr(0, tab))] = unescapeStorage(line.substr(tab + 1));
    }
}
bool saveStorage(const fs::path& file, const std::map<std::string, std::string>& kv) {
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        for (const auto& [k, v] : kv) out << escapeStorage(k) << '\t' << escapeStorage(v) << '\n';
        if (!out) return false;
    }
    std::error_code ec;
    return replaceFile(tmp, file, ec);
}

// ---------------------------------------------------------------------------
// Player reports (/report <player> <reason>): kept in reports.txt next to server.conf, so the
// owner can read them whenever - in the console ('reports'), in the game as an operator, or in
// the file itself - whether or not they were online when it happened. Each report keeps who
// reported whom and why, when, where both players were, and the chat around it.
// reports.txt: one report per line, tab-separated, \\ \\n \\t escaped:
//   id, status (open/closed), unix time, reporter, reported, reported's position,
//   reporter's position, reason, closed by, note, chat (lines joined with newlines)
// ---------------------------------------------------------------------------
struct Report {
    int id = 0;
    std::string status = "open";
    long long time = 0;
    std::string reporter, target, targetPos, reporterPos, reason, handledBy, note;
    std::vector<std::string> chat;
};

struct ReportBook {
    fs::path file;
    std::vector<Report> list;
    int nextId = 1;

    void load() {
        list.clear();
        nextId = 1;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            size_t start = 0;
            for (size_t i = 0; i <= line.size(); ++i) {
                if (i == line.size() || line[i] == '\t') { f.push_back(unescapeStorage(line.substr(start, i - start))); start = i + 1; }
            }
            if (f.size() < 8) continue;
            Report r;
            try { r.id = std::stoi(f[0]); r.time = std::stoll(f[2]); } catch (...) { continue; }
            r.status = f[1] == "closed" ? "closed" : "open";
            r.reporter = f[3]; r.target = f[4]; r.targetPos = f[5]; r.reporterPos = f[6]; r.reason = f[7];
            if (f.size() > 8) r.handledBy = f[8];
            if (f.size() > 9) r.note = f[9];
            if (f.size() > 10 && !f[10].empty()) {
                std::istringstream c(f[10]);
                std::string l;
                while (std::getline(c, l)) r.chat.push_back(l);
            }
            nextId = std::max(nextId, r.id + 1);
            list.push_back(r);
        }
    }
    bool save() const {
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << "# Player reports (/report). Read them with 'reports' in the server console.\n";
            for (const auto& r : list) {
                std::string chat;
                for (size_t i = 0; i < r.chat.size(); ++i) chat += (i ? "\n" : "") + r.chat[i];
                const std::string fields[] = { std::to_string(r.id), r.status, std::to_string(r.time), r.reporter, r.target,
                                               r.targetPos, r.reporterPos, r.reason, r.handledBy, r.note, chat };
                for (size_t i = 0; i < 11; ++i) out << (i ? "\t" : "") << escapeStorage(fields[i]);
                out << "\n";
            }
            if (!out) return false;
        }
        std::error_code ec;
        return replaceFile(tmp, file, ec);
    }
    Report* find(int id) {
        for (auto& r : list) if (r.id == id) return &r;
        return nullptr;
    }
    int openCount() const {
        int n = 0;
        for (const auto& r : list) if (r.status == "open") ++n;
        return n;
    }
    int openBy(const std::string& reporter, const std::string& target) const {
        int n = 0;
        for (const auto& r : list) {
            if (r.status == "open" && lower(r.reporter) == lower(reporter) && lower(r.target) == lower(target)) ++n;
        }
        return n;
    }
};

// "3 min ago", "2 h ago", "5 days ago"
std::string agoText(long long when) {
    const long long s = std::max<long long>(0, static_cast<long long>(std::time(nullptr)) - when);
    if (s < 60) return std::to_string(s) + " s ago";
    if (s < 3600) return std::to_string(s / 60) + " min ago";
    if (s < 86400) return std::to_string(s / 3600) + " h ago";
    return std::to_string(s / 86400) + " days ago";
}
std::string dateText(long long when) {
    const std::time_t t = static_cast<std::time_t>(when);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

// The folders a mod's Lua may read (LuaSandbox): the game's folder, the global mods folder(s)
// and the shared module folders (lualib/ next to mods/, and games/<game>/lualib/)
std::vector<fs::path> serverModRoots(const std::string& game) {
    std::vector<fs::path> modRoots;
    for (const std::string& p : {std::string("mods"), std::string("../mods"), std::string("../../mods"),
                                 "games/" + game + "/mods", "../../games/" + game + "/mods"}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) modRoots.push_back(p);
    }
    return modRoots;
}

void installSandbox(sol::state& lua, const std::string& game) {
    std::vector<fs::path> modRoots = serverModRoots(game), libs;
    for (const std::string& p : {std::string("lualib"), std::string("../lualib"), std::string("../../lualib"),
                                 "games/" + game + "/lualib", "../../games/" + game + "/lualib"}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) libs.push_back(p);
    }
    luasandbox::install(lua, { findFolder("games/" + game) }, modRoots, libs);
}

// worker = true: a Lua state for a world generator thread. Same mods, but nothing in it
// may touch the world or the block list (the main thread owns those), and it stays quiet.
void setupLua(sol::state& lua, ServerContext& ctx, bool worker = false) {
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table,
                       sol::lib::coroutine, sol::lib::utf8);
    // every Lua error the server catches: with its Lua stack (world-generator threads: once their
    // mods have loaded - see the generator below)
    if (!worker) diag::installLuaTraceback(lua.lua_state());
    installSandbox(lua, ctx.config->game);        // require / dofile only inside mods and lualib
    luasandbox::installTimeLimitHook(lua);
    if (worker) lua["print"] = [](sol::variadic_args) {};
    else {
        // print() through std::cout, so it shows above the console prompt instead of through it
        lua["print"] = [](sol::variadic_args va, sol::this_state ts) {
            sol::state_view lv(ts);
            sol::protected_function tostr = lv["tostring"];
            std::string line;
            for (auto v : va) {
                if (!line.empty()) line += "\t";
                auto r = tostr(v.get<sol::object>());
                line += r.valid() ? r.get<std::string>() : std::string("?");
            }
            std::cout << line << std::endl;
        };
    }

    lua.new_usertype<ChunkData>("Chunk",
        "set_block", &ChunkData::setBlock, "setBlock", &ChunkData::setBlock,
        "get_block", &ChunkData::getLocalBlock, "getBlock", &ChunkData::getLocalBlock,
        "set_block_with_meta", &ChunkData::setBlockWithMeta,
        "set_metadata", &ChunkData::setMetadata, "get_metadata", &ChunkData::getLocalMetadata,
            // chunk:set_tint(x, z, r, g, b [, "grass" | "foliage"]) - biome colour (0..255 or 0..1)
            "set_tint", [](ChunkData& c, int x, int z, float r, float g, float b, sol::optional<std::string> channel) {
                const bool bytes = r > 1.0f || g > 1.0f || b > 1.0f;
                auto to8 = [bytes](float v) { return static_cast<uint32_t>(std::clamp(bytes ? v : v * 255.0f, 0.0f, 255.0f)); };
                c.setColumnTint(x, z, (to8(r) << 16) | (to8(g) << 8) | to8(b), channel && *channel == "foliage" ? 1 : 0);
            }
    );

    auto engine = lua.create_named_table("engine");
    if (worker) {
        engine["register_block"] = [](sol::variadic_args) {};   // already registered by the main thread
    } else {
        engine["register_block"] = [](const std::string& name, float r, float g, float b, sol::object tex,
                                      sol::object overlay, sol::optional<sol::table> props) {
            registerBlockDefinition(name, r, g, b, tex, overlay, props, [](const std::string&) { return -1; });
        };
    }
    engine["get_block_id"] = [](const std::string& name) { return BlockRegistry::getInstance().getBlockId(name); };
    if (worker) {
        engine["set_block"] = [](sol::variadic_args) {};
        engine["get_block_name"] = [](sol::variadic_args) { return std::string("air"); };
        engine["get_metadata"] = [](sol::variadic_args) { return 0; };
        engine["set_block_meta"] = [](sol::variadic_args) {};
    } else {
        engine["get_metadata"] = [&ctx](int x, int y, int z) -> int {
            return ctx.here()->getMetadataAt(glm::ivec3(x, y, z));
        };
        engine["set_block_meta"] = [&ctx](int x, int y, int z, const std::string& name, int meta) {
            ctx.here()->setBlockWithMetaAt(glm::ivec3(x, y, z), BlockRegistry::getInstance().getBlockId(name),
                                          static_cast<uint8_t>(meta & 0xFF));
        };
        engine["is_opaque"] = [&ctx](int x, int y, int z) {      // (see the game's engine.is_opaque)
            const uint8_t id = ctx.here()->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0 || FluidRegistry::get().forBlock(id)) return false;
            const auto& b = BlockRegistry::getInstance().getBlock(id);
            return b.isWalkable && !b.isTransparent && !b.isCutout;
        };
        engine["is_replaceable"] = [&ctx](int x, int y, int z) {
            const uint8_t id = ctx.here()->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0) return true;
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            return FluidRegistry::get().forBlock(id) != nullptr || prop.pattern == "plantlike";
        };
        engine["set_block"] = [&ctx](int x, int y, int z, const std::string& name) {
            ctx.here()->setBlockAt(glm::ivec3(x, y, z), BlockRegistry::getInstance().getBlockId(name));
        };
        engine["get_block_name"] = [&ctx](int x, int y, int z) -> std::string {
            const uint8_t id = ctx.here()->getBlockAt(glm::ivec3(x, y, z));
            return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name;
        };
    }
    engine["is_server"] = []() { return true; };
    if (!worker) {
        // Mod storage, saved with the world (like engine.get_storage in singleplayer)
        engine["get_storage"] = [&ctx](const std::string& key, sol::this_state ts) -> sol::object {
            const auto it = ctx.storage.find(key);
            if (it == ctx.storage.end()) return sol::make_object(ts, sol::lua_nil);
            return sol::make_object(ts, it->second);
        };
        engine["set_storage"] = [&ctx](const std::string& key, sol::optional<std::string> value) {
            if (value) ctx.storage[key] = *value; else ctx.storage.erase(key);
            ctx.storageDirty = true;
        };
        ctx.api.addFunctions(engine);             // players: get_online_players, kick_player...
        // block text (signs): get / set where the world runs; the server has no player of its own
        blocktext::install(engine, [&ctx]() { return ctx.here(); }, []() { return true; }, nullptr);
        blockstate::install(engine, [&ctx]() { return ctx.here(); });   // BlockStateLua.h
    }
    engine["get_player_name"] = [&ctx]() { return ctx.config->name; };
    engine["send_chat"] = [&ctx](const std::string& text) {
        ctx.server->broadcastMessage("[Server] " + text, glm::vec3(1.0f));
        return true;
    };
    engine["get_game_info"] = [&ctx, &lua]() {
        sol::table t = lua.create_table();
        t["selected"] = ctx.game.folder;
        t["loaded"] = ctx.game.folder;
        t["title"] = ctx.game.title;
        t["can_play"] = true;
        return t;
    };

    auto world = lua.create_named_table("world");
    world["get_falling_blocks"] = [&ctx](sol::this_state ts) {
        // world.get_falling_blocks() -> { { name, x, y, z }, ... }: sand, gravel... in the air now.
        // x, y, z: the block's bottom corner (y has a fraction while it falls)
        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        if (ctx.world) {
            int i = 1;
            for (const auto& f : ctx.here()->fallingBlocks()) {
                sol::table t = lv.create_table();
                t["name"] = BlockRegistry::getInstance().getBlock(f.id).name;
                t["x"] = f.pos.x; t["y"] = f.pos.y; t["z"] = f.pos.z;
                out[i++] = t;
            }
        }
        return out;
    };
    world["set_render_distance"] = [](int) {};
    world["get_render_distance"] = [&ctx]() { return ctx.config->viewDistance; };
    world["set_load_budget"] = [&ctx](float ms) { ctx.world->setLoadBudgetMs(ms); };
    world["set_mesh_budget"] = [](float) {};
    world["get_loaded_chunks"] = [&ctx]() { return static_cast<int>(ctx.world->loadedChunkCount()); };
    world["get_pending_chunks"] = [&ctx]() { return static_cast<int>(ctx.world->pendingLoadCount()); };
    world["get_caves_enabled"] = []() { return CaveGenerator::isEnabled(); };
    world["set_caves_enabled"] = [](bool on) { CaveGenerator::setEnabled(on); };
    // Mods also run on the world generator threads: only the main copy changes the settings
    if (worker) world["set_cave_settings"] = [](sol::variadic_args) {};
    else world["set_cave_settings"] = [](sol::table t) { applyCaveSettings(t); };
    world["get_cave_settings"] = [](sol::this_state ts) { return caveSettingsTable(ts); };

    auto chat = lua.create_named_table("chat");
    chat["register_command"] = [&ctx](const std::string& name, sol::optional<std::string> usage,
                                      sol::optional<std::string> description) {
        for (const auto& c : ctx.modCommands) if (c.name == name) return;
        ctx.modCommands.push_back({name, usage.value_or("/" + name), description.value_or("")});
    };
    chat["send"] = [&ctx](const std::string& text, sol::optional<float> r, sol::optional<float> g,
                          sol::optional<float> b) {
        ctx.server->broadcastMessage(text, glm::vec3(r.value_or(1.0f), g.value_or(1.0f), b.value_or(0.6f)));
    };

    // Fluids: engine.register_fluid etc. Not on generator threads: the fluid registry is
    // shared, and the main state already registered everything.
    if (!worker) addFluidLuaApi(lua, engine, [&ctx]() { return ctx.here(); });

    // Shared values: the server decides (e.g. the weather) and every player gets them
    engine["is_authority"] = []() { return true; };
    engine["set_shared"] = [&ctx](const std::string& key, const std::string& value) {
        if (key.empty() || !ctx.server) return false;
        const auto& now = ctx.server->shared();
        const auto it = now.find(key);
        if (it != now.end() && it->second == value) return true;
        ctx.server->setShared(key, value);
        ctx.sharedChanged.push_back(key);
        return true;
    };
    engine["get_shared"] = [&ctx](const std::string& key) -> std::string {
        if (!ctx.server) return "";
        const auto it = ctx.server->shared().find(key);
        return it == ctx.server->shared().end() ? std::string() : it->second;
    };
    engine["is_outdoors"] = [&ctx](int x, int y, int z) {
        return ctx.world && ctx.here()->hasSkyAbove(glm::ivec3(x, y, z));
    };

    if (worker) {
        // Generator threads must not touch the network either (only the main thread may)
        engine["send_chat"] = [](sol::variadic_args) { return false; };
        chat["send"] = [](sol::variadic_args) {};
        world["get_falling_blocks"] = [](sol::this_state ts) { return sol::state_view(ts).create_table(); };
    }
    dims::installLua(lua);   // dimensions.register / get / list (before the noop fallbacks)
    lua.script(kNoopLua);
    if (!worker) {
        // The time of day is the server's: sky.get_time / set_time and friends work here (the
        // rest of sky.* is the game's drawing, and does nothing on a server)
        sol::table sky = lua["sky"];
        sky["get_time"] = [&ctx]() { return ctx.clock ? ctx.clock->time() : 0.75; };
        sky["set_time"] = [&ctx](double t, sol::optional<bool> forward) {
            if (ctx.clock && std::isfinite(t)) ctx.clock->setTimeOfDay(t, forward.value_or(false));
        };
        sky["add_time"] = [&ctx](double hours) {
            if (ctx.clock && std::isfinite(hours)) ctx.clock->total = std::max(0.0, ctx.clock->total + hours / 24.0);
        };
        sky["get_day"] = [&ctx]() { return ctx.clock ? static_cast<double>(ctx.clock->day()) : 0.0; };
        sky["get_clock"] = [&ctx]() { return ctx.clock ? ctx.clock->clock() : std::string("12:00"); };
        sky["set_time_speed"] = [&ctx](double v) { if (ctx.clock && std::isfinite(v)) ctx.clock->speed = std::clamp(v, 0.0, 1000.0); };
        sky["get_time_speed"] = [&ctx]() { return ctx.clock ? ctx.clock->speed : 1.0; };
        sky["set_day_length"] = [&ctx](double sec) { if (ctx.clock && std::isfinite(sec)) ctx.clock->secondsPerDay = std::clamp(sec, 10.0, 1.0e7); };
        sky["get_day_length"] = [&ctx]() { return ctx.clock ? ctx.clock->secondsPerDay : 1200.0; };
    }
}

void printUsage() {
    std::cout << "Usage: BlockSmithServer [options]\n"
              << "  --version         show the version and commit, and exit\n"
              << "  --setup           answer a few questions to set up (or change) the server\n"
              << "  --config <file>   settings file (default server.conf, created if missing)\n"
              << "  --game <folder>   game to run (overrides 'game' in the settings)\n"
              << "  --world <name>    world folder in saves/ (overrides 'world')\n"
              << "  --port <number>   port (overrides 'port')\n"
              << "  --list-games      show installed games and exit\n";
}

// ===========================================================================
// --setup: set up the server by answering questions in the console (for machines
// without the game - the console version of the game's My Servers screen)
// ===========================================================================
std::string ask(const std::string& question, const std::string& current) {
    std::cout << question << (current.empty() ? "" : " [" + current + "]") << ": " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return current;
    answer = trim(answer);
    return answer.empty() ? current : answer;
}

void runSetup(const fs::path& configFile) {
    std::error_code ec;
    if (!fs::exists(configFile, ec)) writeDefaultConfig(configFile, ServerConfig{});
    ServerConfig c = parseConfig(readConfFile(configFile));

    std::cout << "\n=== Server setup === (press Enter to keep the value in [brackets])\n\n";
    c.name = ask("Server name", c.name);
    std::cout << "Description colours: {gold} {red} {aqua} {#ff8800}, {b}bold{/b}, {n} = second line\n";
    c.motd = ask("Description", c.motd);

    const auto games = listGames();
    if (!games.empty()) {
        std::cout << "\nGames installed:\n";
        for (size_t i = 0; i < games.size(); ++i) {
            std::cout << "  " << (i + 1) << ") " << games[i].title << " (" << games[i].folder << ")"
                      << (games[i].folder == c.game ? "  <- current" : "") << "\n";
        }
        const std::string pick = ask("Game (number or folder name)", c.game);
        const int n = toInt(pick, 0);
        if (n >= 1 && n <= static_cast<int>(games.size())) c.game = games[n - 1].folder;
        else c.game = pick;
    }
    c.world = ask("World name", c.world);
    c.seed = ask("Seed for a new world (empty = random)", c.seed);
    c.port = std::clamp(toInt(ask("Port", std::to_string(c.port)), c.port), 1024, 65535);
    c.maxPlayers = std::clamp(toInt(ask("Max players", std::to_string(c.maxPlayers)), c.maxPlayers), 1, 4000);
    c.requirePassword = toBool(ask("Require players to have a password? (yes/no) - recommended for public servers",
                                   c.requirePassword ? "yes" : "no"), c.requirePassword);
    c.whitelist = toBool(ask("Whitelist only? (yes/no)", c.whitelist ? "yes" : "no"), c.whitelist);

    // Mods: show them all, let the owner switch some off by number
    const auto mods = listServerMods(c.game);
    if (!mods.empty()) {
        std::cout << "\nMods for this game (x = on):\n";
        for (size_t i = 0; i < mods.size(); ++i) {
            const bool on = mods[i].required || !c.disabledMods.count(mods[i].id);
            std::cout << "  " << (i + 1) << ") [" << (on ? "x" : " ") << "] " << mods[i].title << " (" << mods[i].id
                      << ", " << mods[i].source << (mods[i].required ? ", required" : "") << ")\n";
        }
        const std::string toggle = ask("Numbers to switch on/off, comma separated (Enter = keep)", "");
        std::istringstream ss(toggle);
        std::string part;
        while (std::getline(ss, part, ',')) {
            const int n = toInt(trim(part), 0);
            if (n < 1 || n > static_cast<int>(mods.size())) continue;
            const auto& m = mods[n - 1];
            if (m.required) { std::cout << "  " << m.id << " is required - it stays on\n"; continue; }
            if (c.disabledMods.count(m.id)) c.disabledMods.erase(m.id); else c.disabledMods.insert(m.id);
        }
    }

    updateConfValue(configFile, "name", c.name);
    updateConfValue(configFile, "motd", c.motd);
    updateConfValue(configFile, "game", c.game);
    updateConfValue(configFile, "world", c.world);
    updateConfValue(configFile, "seed", c.seed);
    updateConfValue(configFile, "port", std::to_string(c.port));
    updateConfValue(configFile, "max_players", std::to_string(c.maxPlayers));
    updateConfValue(configFile, "require_password", c.requirePassword ? "true" : "false");
    updateConfValue(configFile, "whitelist", c.whitelist ? "true" : "false");
    updateConfValue(configFile, "disabled_mods", joinIds(c.disabledMods));
    std::cout << "\nSaved to " << configFile.string() << ". Put a 64x64 server_icon.png next to it for an icon.\n"
              << "Tip: after joining, type 'op <your name>' here to manage the server from the game.\n\n";
}

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv) {
    diag::init("server");   // crash reports, and (below) warning traces (Diagnostics.h)
    std::cout << "BlockSmith dedicated server " << BLOCKSMITH_VERSION << " (commit " << BLOCKSMITH_COMMIT
              << ", protocol " << net::kVersionName << ")" << std::endl;

    // ---- Command line ----
    fs::path configFile = "server.conf";
    std::map<std::string, std::string> overrides;
    bool setup = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (a == "--help" || a == "-h") { printUsage(); return 0; }
        else if (a == "--version" || a == "-v") return 0;   // (the first line printed says it)
        else if (a == "--list-games") { printGames(); return 0; }
        else if (a == "--setup") setup = true;
        else if (a == "--config") configFile = next();
        else if (a == "--game") overrides["game"] = next();
        else if (a == "--world") overrides["world"] = next();
        else if (a == "--port") overrides["port"] = next();
        else if (i == 1 && a.rfind("--", 0) != 0) configFile = a;   // old style: BlockSmithServer my.conf
        else { std::cerr << "Unknown option '" << a << "'\n"; printUsage(); return 1; }
    }

    std::error_code ec;
    if (setup) {
        runSetup(configFile);
    } else if (!fs::exists(configFile, ec)) {
        writeDefaultConfig(configFile, ServerConfig{});
        std::cout << "[Server] Created " << configFile.string() << " with default settings. Edit it, or run "
                  << "BlockSmithServer --setup to answer a few questions instead." << std::endl;
    }
    const fs::path confDir = configFile.has_parent_path() ? configFile.parent_path() : fs::path(".");
    startLogFile(confDir);   // logs/latest.log: everything from here on, with the time
    // (first in the log: which build wrote it)
    std::cout << "[Server] " << BLOCKSMITH_VERSION_FULL << ", protocol " << net::kVersionName << std::endl;
    // the system report (build, OS, CPU, memory...), then every warning with its trace
    diag::addReportLine(std::string("Protocol: ") + net::kVersionName);
    diag::addReportLine("Config: " + fs::absolute(configFile).string());
    diag::printSystemReport();
    diag::enableWarningTraces();
    auto readSettings = [&]() {
        auto kv = readConfFile(configFile);
        for (const auto& [k, v] : overrides) kv[k] = v;
        return parseConfig(kv);
    };
    ServerConfig config = readSettings();

    // ---- Game ----
    const fs::path gameDir = findFolder("games/" + config.game);
    if (!fs::is_directory(gameDir, ec)) {
        std::cerr << "[Server] There's no game called '" << config.game << "'." << std::endl;
        printGames();
        return 1;
    }
    ServerContext ctx;
    ctx.config = &config;
    ctx.game = readGameInfo(gameDir);
    std::cout << "[Server] Game: " << ctx.game.title << " (" << ctx.game.folder << ")" << std::endl;

    // ---- World ----
    World world;
    GameServer server;
    ctx.world = &world;
    // The world's other dimensions (Dimensions.h): loaded while someone's in them, each saved in
    // dimensions/<name>/ inside the world's folder
    DimensionWorlds dimWorlds;
    dimWorlds.primary = &world;
    dimWorlds.renderDistance = config.viewDistance;
    ctx.dims = &dimWorlds;
    ctx.server = &server;

    const fs::path worldDir = findFolder("saves") / safeName(config.world);
    dimWorlds.worldDir = worldDir.string();   // (dimensions/<name>/ inside it)
    fs::create_directories(worldDir / "chunks", ec);
    WorldMeta meta = readWorldMeta(worldDir / "world.dat");
    if (meta.exists && !meta.game.empty() && meta.game != config.game) {
        std::cerr << "[Server] World '" << config.world << "' was made with the game '" << meta.game
                  << "', not '" << config.game << "'. Pick another world (--world) or game." << std::endl;
        return 1;
    }
    if (!meta.exists) meta.seed = seedFromText(config.seed);
    meta.game = config.game;
    // The time of day: carries on where the world left it (a new world: morning)
    DayClock dayClock;
    if (!meta.timeLine.empty()) dayClock.loadLine(meta.timeLine);
    dayClock.secondsPerDay = config.dayLengthMinutes * 60.0;
    ctx.clock = &dayClock;
    meta.timeLine = dayClock.saveLine();

    // Game rules (gamerules.txt in the world: "name = value") and players' own spawn points
    // (spawnpoints.txt: "Name | x y z", set by /spawnpoint)
    std::map<std::string, std::string> gameRules;
    {
        for (const auto& [k, v] : readConfFile(worldDir / "gamerules.txt")) {
            if (const GameRuleDef* g = findGameRule(k)) gameRules[g->name] = lower(trim(v));
        }
    }
    auto ruleValue = [&](const std::string& name) -> std::string {
        const auto it = gameRules.find(name);
        if (it != gameRules.end()) return it->second;
        const GameRuleDef* g = findGameRule(name);
        return g ? g->defaultValue : "";
    };
    auto saveGameRules = [&]() {
        std::ofstream out(worldDir / "gamerules.txt", std::ios::trunc);
        out << "# Game rules ('gamerule' in the console). Removed or empty: the default.\n";
        for (const auto& [k, v] : gameRules) out << k << " = " << v << "\n";
    };
    NameList spawnPoints{worldDir / "spawnpoints.txt", {}};
    spawnPoints.load();
    writeWorldMeta(worldDir / "world.dat", meta);
    world.setSaveDirectory(worldDir.string());
    world.setSeed(meta.seed);
    std::cout << "[Server] " << (meta.exists ? "Opened" : "Created") << " world '" << config.world
              << "' (seed " << meta.seed << ")" << std::endl;

    // ---- Mods ----
    loadStorage(worldDir / "mod_storage.txt", ctx.storage);
    sol::state lua;
    ctx.api.lua = &lua;
    ctx.api.server = &server;
    ctx.api.dedicated = true;
    ctx.api.timeLimit = config.luaTimeLimit;
    setupLua(lua, ctx);
    // Entities (mobs, dropped items): run here; players' games get them as data (Entities.h)
    // The server's light query. It keeps no light data (no lighting), so: under open sky, the
    // sunlight for the time of day; under anything solid that light can't get through, dark.
    // (With light data it's the real light.) Used by engine.get_light_level and mob spawning,
    // so both agree - and `sky` says whether the sky is overhead (spawn rules with sky = true).
    struct ServerLight { int level; bool sky; };
    auto serverLight = [&ctx, &dayClock](int x, int y, int z) -> ServerLight {
        World& world = *ctx.here();   // (the dimension being worked on: Dimensions.h)
        const DimensionType& dt = world.dimensionType();
        float day = std::clamp(dayClock.sunDirection().y * 2.0f + 0.5f, 0.0f, 1.0f);
        if (dt.fixedTime >= 0.0) day = (dt.fixedTime >= 0.25 && dt.fixedTime < 0.75) ? 1.0f : 0.0f;
        const int dimming = static_cast<int>(std::lround((1.0f - day) * 11.0f));
        if (!dt.hasSkylight && !world.lighting()) return { 0, false };
        if (world.lighting()) {
            const uint16_t l = world.getLightAt(glm::ivec3(x, y, z));
            const int skyNow = std::max(0, light::sky(l) - dimming);
            return { std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l)))), light::sky(l) >= 15 };
        }
        for (int yy = y + 1; yy < CHUNK_HEIGHT; ++yy) {
            const uint8_t id = world.getBlockAt(glm::ivec3(x, yy, z));
            const auto& bp = BlockRegistry::getInstance().getBlock(id);
            if (id != 0 && !bp.isTransparent && !bp.isCutout && bp.isWalkable) return { 0, false };   // (leaves, glass, plants let light through)
        }
        return { 15 - dimming, true };
    };
    EntityManager entityManager;
    EntityLua entityLua;
    {
        EntityLuaHost host;
        host.manager = &entityManager;
        host.world = &world;
        host.isAuthority = [] { return true; };
        host.players = [&server]() { return server.playerPositions(); };
        host.playerPlaces = [&server]() {
            std::vector<std::tuple<std::string, glm::vec3, std::string>> out;
            for (const auto& p : server.playerPlaces()) out.emplace_back(p.name, p.pos, p.dimension);
            return out;
        };
        host.worldFor = [&dimWorlds](const std::string& d) -> const World* { return dimWorlds.find(d); };
        host.damagePlayer = [&server](const std::string& name, float amount, const std::string& reason) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", amount);
            server.sendAction(name, "damage", buf, reason);
        };
        host.giveItem = [&server](const std::string& name, const std::string& item, int count) {
            return server.sendAction(name, "give", item, std::to_string(count));
        };
        host.lightLevel = [serverLight](int x, int y, int z) { return serverLight(x, y, z).level; };
        host.skyVisible = [serverLight](int x, int y, int z) { return serverLight(x, y, z).sky; };
        host.isDay = [&dayClock] { const double t = dayClock.time(); return t >= 0.5 && t < 0.98; };
        host.spawningEnabled = [&ruleValue] { return ruleValue("doMobSpawning") != "false"; };
        host.timeLimit = config.luaTimeLimit;
        entityLua.bind(lua, host);
    }
    server.setEntities(&entityManager);
    // ABMs and LBMs (engine.register_abm / register_lbm)
    BlockModifiers blockMods;
    blockMods.timeLimit = config.luaTimeLimit;
    blockMods.randomTickSpeed = [&ruleValue] {
        try { return std::stoi(ruleValue("randomTickSpeed")); } catch (...) { return 3; }
    };
    blockMods.bind(lua, lua["engine"]);
    blockMods.loadWorld(worldDir.string());
    // engine.get_light_level(x, y, z): the server keeps no light, so: open sky = sunlight for the
    // time of day, under cover = dark (enough for grass, crops and mob spawning rules)
    lua["engine"]["get_light_level"] = [serverLight](int x, int y, int z) -> int { return serverLight(x, y, z).level; };
    server.onEntityAct = [&](const std::string& name, uint32_t id, int kind, const std::string& item, const glm::vec3& at) {
        if (kind == 0) entityLua.punch(id, name, at, item);
        else entityLua.rightClick(id, name, item);
    };
    server.canEditText = [&ctx](const std::string& name, const glm::ivec3& pos, int side, std::vector<std::string>& lines,
                                std::string& refusal) {
        return ctx.api.canEditText(name, pos, side, lines, refusal);
    };
    server.onModMessage = [&ctx](const std::string& name, const std::string& channel, const std::string& data) {
        ctx.api.modMessage(name, channel, data);
    };
    server.onHarvest = [&ctx](const std::string& name, const std::string& item, const glm::ivec3& pos) {
        ctx.api.harvest(name, item, pos);
    };
    server.onDropItem = [&](const std::string&, const std::string& item, int count, const glm::vec3& at, const GameServer::DropInfo& how) {
        if (BlockRegistry::getInstance().getBlockId(item) == 0) return;
        const uint32_t id = how.hasVelocity ? entityManager.dropItem(item, count, at, how.velocity) : entityLua.drop(item, count, at);
        Entity* e = entityManager.get(id);
        if (!e) return;
        if (how.returned) { e->vel = glm::vec3(0.0f); e->pickupDelay = 3.0f; }
        else if (how.pickupDelay >= 0.0f) e->pickupDelay = how.pickupDelay;
    };
    // Online content for mods (remote.*): data only, from GitHub and the approved sites
    remote::Policy remotePolicy;
    remotePolicy.enabled = config.remoteContent;
    remotePolicy.extraDomains = config.remoteDomains;
    RemoteLua remoteContent(lua, confDir / "cache" / "remote", remotePolicy, serverModRoots(config.game),
                            config.luaTimeLimit);
    // Is there a newer BlockSmith? (a version file, read as data: UpdateCheck.h)
    update::Checker updates(confDir / "cache" / "update", config.updateUrl, remotePolicy, config.updateCheck);
    const auto serverStart = Clock::now();
    std::string lastUpdateLine;
    int failed = 0;
    // engine.find_texture(name) -> the picture's path, or nil: the same answer as in the game
    // (this mod's folder, then the game's textures, then any mod or game folder by file name).
    // The server draws nothing, but mods look their pictures up while loading - without this
    // every lookup said "not found" and they warned about pictures that are there.
    {
        auto index = std::make_shared<std::map<std::string, std::string>>();
        auto built = std::make_shared<bool>(false);
        const std::string game = config.game;
        lua["engine"]["find_texture"] = [&lua, index, built, game](const std::string& name) -> sol::optional<std::string> {
            if (name.empty() || name.find("..") != std::string::npos) return sol::nullopt;
            auto lower = [](std::string t) { for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return t; };
            std::error_code ec;
            const std::string modPath = lua["g_CurrentModPath"].get_or(std::string());
            std::vector<fs::path> tries;
            if (!modPath.empty()) { tries.push_back(fs::path(modPath) / "textures" / name); tries.push_back(fs::path(modPath) / name); }
            for (const std::string& g : { "games/" + game, "../../games/" + game }) tries.push_back(fs::path(g) / "textures" / name);
            for (const auto& t : tries) if (fs::is_regular_file(t, ec)) return fs::absolute(t, ec).string();
            if (!*built) {   // every .png in the game and mod folders, by file name (first found wins)
                *built = true;
                std::vector<fs::path> roots = serverModRoots(game);
                for (const std::string& g : { "games/" + game, "../../games/" + game }) if (fs::is_directory(g, ec)) roots.push_back(g);
                for (const auto& root : roots) {
                    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                        if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".png") continue;
                        index->emplace(lower(it->path().filename().string()), fs::absolute(it->path(), ec).string());
                    }
                }
            }
            const auto f = index->find(lower(fs::path(name).filename().string()));
            if (f == index->end()) return sol::nullopt;
            return f->second;
        };
    }
    for (const auto& file : findModFiles(config.game, config.disabledMods)) {
        lua["g_CurrentModPath"] = file.parent_path().string();
        try {
            lua.script_file(file.string());
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
        }
    }
    if (failed) std::cerr << "[Mod] " << failed << " mod file(s) failed to load (see above)" << std::endl;
    // The world's entities, now that the mods have registered their kinds
    {
        const int n = entityManager.load(worldDir.string());
        entityLua.activateAll();
        if (n > 0) std::cout << "[Server] " << n << " entities" << std::endl;
    }

    // ---- World generation on background threads ----
    // The main thread only ticks; new chunks are generated by worker threads, each with its
    // own copy of the mods' world generator. So exploring players never slow the server down.
    const auto modFiles = findModFiles(config.game, config.disabledMods);
    world.setSeed(meta.seed);
    CaveGenerator::setSeed(meta.seed);
    CaveGenerator::prepare();   // before several threads carve caves at once
    ChunkGenerator generator(config.generatorThreads, [&ctx, modFiles]() {
        auto workerLua = std::make_unique<sol::state>();
        setupLua(*workerLua, ctx, true);
        // Each thread runs its own copy of the mods (for generate_chunk). Their load errors are
        // the main thread's to report (it loads the same files, with the whole engine there);
        // here, where only world generation works, they'd be noise - kept quiet
        for (const auto& file : modFiles) {
            (*workerLua)["g_CurrentModPath"] = file.parent_path().string();
            try { workerLua->safe_script_file(file.string(), sol::script_pass_on_error); } catch (const std::exception&) {}
        }
        diag::installLuaTraceback(workerLua->lua_state());   // from now on (generating chunks): with the Lua stack
        return workerLua;
    }, meta.seed);
    world.setGenerator(&generator);
    dimWorlds.generator = &generator;   // (one set of threads for every dimension)
    dimWorlds.loadBudgetMs = 1000.0f / config.tickRate * 0.25f;
    dimWorlds.onLoaded = [&](const std::string& d, World& w) {
        if (server.running()) w.setRecordChanges(true);
        blockMods.loadDimension(d, w.saveDirectory());
    };
    dimWorlds.onUnloading = [&](const std::string& d, World& w) {
        blockMods.saveDimension(d, w.saveDirectory());
        blockMods.forgetDimension(d);
    };
    std::cout << "[Server] World generation: " << generator.threadCount() << " background thread"
              << (generator.threadCount() == 1 ? "" : "s") << std::endl;

    // ---- Spawn area ----
    const glm::vec3 spawnCenter(config.spawnX + 0.5f, 80.0f, config.spawnZ + 0.5f);
    world.setRenderDistance(3);
    world.setLoadBudgetMs(50.0f);
    const auto loadStart = Clock::now();
    while (world.areaLoadProgress(spawnCenter, 2) < 1.0f && Clock::now() - loadStart < std::chrono::seconds(60)) {
        world.update(spawnCenter, lua);
        if (!world.fatalError().empty()) {            // the world generator can't run: don't serve a void
            std::cerr << "[Server] " << world.fatalError() << std::endl;
            std::cerr << "[Server] Can't open the world - stopping (fix the game's world generator and start again)" << std::endl;
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));   // the threads do the work
    }
    world.setLoadBudgetMs(1000.0f / config.tickRate * 0.25f);   // reading saved chunks: at most 1/4 of a tick
    std::cout << "[Server] Spawn ready at " << config.spawnX << ", " << config.spawnZ << std::endl;

    // ---- Players: whitelist, bans, operators, accounts, positions, welcome ----
    NameList whitelist{confDir / "whitelist.txt", {}};
    NameList banned{confDir / "banned.txt", {}};
    NameList ops{confDir / "ops.txt", {}};
    NameList playerModes{confDir / "gamemodes.txt", {}};   // "Name | creative": modes given by /gamemode
    Accounts accounts{confDir / "accounts.txt", {}};
    // Player reports (reports.txt) and the chat they quote: the last 200 lines, with who and when
    ReportBook reports{confDir / "reports.txt", {}};
    reports.load();
    struct ChatLine { long long time; std::string name, text; };
    std::deque<ChatLine> recentChat;
    std::map<std::string, std::chrono::steady_clock::time_point> lastReport;   // (one report a minute each)
    // An operator's level (ops.txt "Name | level"; no level: op_level from server.conf); 0 = not one
    auto opLevelOf = [&](const std::string& name) {
        if (!ops.has(name)) return 0;
        try { return std::clamp(std::stoi(ops.reason(name)), 1, 4); } catch (...) { return config.opLevel; }
    };
    whitelist.load();
    banned.load();
    ops.load();
    playerModes.load();
    server.setDefaultMode(config.gameMode);
    server.modeFor = [&](const std::string& name) { return playerModes.reason(name); };
    server.isOperator = [&](const std::string& name) { return ops.has(name); };
    accounts.load();
    ctx.api.isOperator = [&](const std::string& name) { return ops.has(name); };
    ctx.api.changeDimension = [&](const std::string& name, const std::string& d, const glm::vec3& pos) {
        return server.setPlayerDimension(name, d, pos);
    };
    ctx.api.loadArea = [&](const std::string& d, int x, int z, int radius) {
        World* w = dimWorlds.get(d);
        if (!w) return false;
        dimWorlds.touch(d);
        const int r = (radius + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
        const int ccx = static_cast<int>(std::floor(x / static_cast<float>(CHUNK_WIDTH)));
        const int ccz = static_cast<int>(std::floor(z / static_cast<float>(CHUNK_WIDTH)));
        bool ok = true;
        for (int cx = ccx - r; cx <= ccx + r; ++cx)
            for (int cz = ccz - r; cz <= ccz + r; ++cz)
                if (!w->chunkData({cx, cz})) ok = w->loadChunk(cx, cz, lua) && ok;
        return ok;
    };
    ctx.api.loadDimension = [&](const std::string& d) {
        World* w = dimWorlds.get(d);
        if (w) dimWorlds.touch(d);
        return w != nullptr;
    };
    ctx.api.rememberMode = [&](const std::string& name, const std::string& mode) {
        if (mode == config.gameMode) playerModes.remove(name); else playerModes.add(name, mode);
    };
    ctx.api.fillServerInfo = [&](sol::table& t) {
        t["name"] = config.name;
        t["world"] = config.world;
        t["tick_rate"] = config.tickRate;
        t["view_distance"] = config.viewDistance;
        t["whitelist"] = config.whitelist;
    };

    auto spawnPoint = [&]() {
        const int ground = world.groundHeightAt(config.spawnX, config.spawnZ);
        return glm::vec3(config.spawnX + 0.5f, ground >= 0 ? static_cast<float>(ground) : 100.0f, config.spawnZ + 0.5f);
    };
    server.spawnFor = [&](const std::string& name, glm::vec3& pos) {
        if (loadPlayer(worldDir, name, pos)) return true;
        pos = spawnPoint();
        return true;
    };
    server.onLeave = [&](const std::string& name, const glm::vec3& pos) {
        savePlayer(worldDir, name, pos, server.playerDimension(name));
        ctx.api.playerLeft(name, pos);
    };
    // Dimensions: each player's, kept with them; the worlds for them
    server.dimensionFor = [&](const std::string& name) {
        glm::vec3 p;
        std::string d;
        loadPlayer(worldDir, name, p, &d);
        return d;
    };
    server.worldFor = [&dimWorlds](const std::string& d) { return dimWorlds.get(d); };
    server.forEachWorld = [&](const std::function<void(const std::string&, World&)>& fn) {
        fn(std::string(), world);
        dimWorlds.forEach(fn);
    };
    server.onDimensionChange = [&ctx](const std::string& name, const std::string& from, const std::string& to) {
        ctx.api.dimensionChanged(name, from, to);
    };
    // A player broke or placed a block: on_block_broken(x, y, z, block, meta, player) /
    // on_block_placed(x, y, z, block, player) - the same hooks as in singleplayer, so mods like
    // leaf decay work on servers (the server is the one that decides)
    server.onPlayerEdit = [&ctx](const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
        if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
        ctx.api.edited(name, pos, oldId, newId);
    };
    // Spawn protection (server.conf spawn_protection), then mods: can_edit_block may refuse an
    // edit. As in Minecraft it only applies once there are operators, and operators may build.
    server.canEdit = [&](const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t oldMeta,
                         uint8_t newId, uint8_t newMeta) -> std::string {
        const int r = config.spawnProtection;
        if (r > 0 && !ops.entries.empty() && !ops.has(name) &&
            std::abs(pos.x - config.spawnX) <= r && std::abs(pos.z - config.spawnZ) <= r) {
            return "The area around spawn is protected";
        }
        return ctx.api.canEdit(name, pos, oldId, oldMeta, newId, newMeta);
    };
    server.onChat = [&](const std::string& name, std::string& text) {
        const bool keep = ctx.api.chat(name, text);
        if (keep) {   // (for reports: what was said, by whom, when)
            recentChat.push_back({ static_cast<long long>(std::time(nullptr)), name, text.substr(0, 300) });
            while (recentChat.size() > 200) recentChat.pop_front();
        }
        return keep;
    };
    std::set<std::string> newAccounts;   // registered while joining (told about it once they're in)
    server.hasAccount = [&](const std::string& name) { return accounts.has(name); };
    // ban-ip: banned-ips.txt ("1.2.3.4 | reason")
    NameList bannedIps{confDir / "banned-ips.txt", {}};
    bannedIps.load();
    server.canJoinFrom = [&](const std::string& ip) -> std::string {
        if (!bannedIps.has(ip)) return "";
        const std::string why = bannedIps.reason(ip);
        return "Your IP address is banned from this server" + (why.empty() ? std::string() : ": " + why);
    };
    server.canJoin = [&](const std::string& name, const net::AuthProof& proof) -> std::string {
        if (banned.has(name)) {
            const std::string why = banned.reason(name);
            return "You are banned from this server" + (why.empty() ? std::string() : ": " + why);
        }
        if (config.whitelist && !whitelist.has(name)) return "You are not on this server's whitelist";
        // Accounts: a name that joined with a password before needs that password
        if (accounts.has(name)) {
            if (accounts.legacy(name)) {
                return "This server updated how passwords work. Ask an operator to type 'password reset " + name +
                       "', then join again to protect your name with your password.";
            }
            if (!proof.hasPassword()) return "The name '" + name + "' is protected on this server. Set your password in Settings > Player.";
            if (!accounts.check(name, proof)) return "Wrong password for '" + name + "'";
            return "";
        }
        if (!proof.hasPassword()) {
            if (config.requirePassword) return "This server needs a password: set one in Settings > Player, then join again";
            return "";
        }
        // First time with a password: this name is now theirs (stores only this server's key)
        if (!accounts.create(name, proof)) return "Your game sent a bad password key. Please update your game.";
        newAccounts.insert(name);
        std::cout << "[Server] New account: " << name << std::endl;
        return "";
    };
    server.onJoin = [&](const std::string& name) {
        if (!config.welcome.empty()) {
            std::string msg = config.welcome;
            for (size_t p; (p = msg.find("{player}")) != std::string::npos;) msg.replace(p, 8, name);
            server.messagePlayer(name, stripTags(msg), glm::vec3(1.0f, 0.85f, 0.3f));
        }
        const bool newAccount = newAccounts.count(name) > 0;
        if (newAccounts.erase(name)) {
            server.messagePlayer(name, "Your name is now protected by your password on this server.",
                                 glm::vec3(0.6f, 0.9f, 1.0f));
        } else if (!accounts.has(name)) {
            server.messagePlayer(name, "Tip: set a password in Settings > Player so nobody else can use your name here.",
                                 glm::vec3(0.6f, 0.9f, 1.0f));
        }
        if (ops.has(name)) server.messagePlayer(name, "You are an operator here: type /help for server commands.",
                                                glm::vec3(0.6f, 0.9f, 1.0f));
        // Which BlockSmith they have, and whether there's an update for them: this server's
        // version is newer, or the newest release (the server's update check) is
        {
            const std::string theirs = server.playerEngineVersion(name);
            update::Version pv, sv, lv;
            const bool known = update::parseVersion(theirs, pv);
            update::parseVersion(BLOCKSMITH_VERSION, sv);
            const update::Info& u = updates.info();
            const bool latestKnown = u.checked && update::parseVersion(u.latest, lv);
            const std::string where = u.download.empty() ? "" : " Download: " + u.download;
            const glm::vec3 gold(1.0f, 0.78f, 0.28f);
            std::cout << "[Server] " << name << " plays BlockSmith " << (known ? theirs : std::string("(an older game that doesn't say)"))
                      << std::endl;
            if (!known) {
                server.messagePlayer(name, std::string("An update is available: this server runs BlockSmith ") +
                                     BLOCKSMITH_VERSION + ", and your game is older." + where, gold);
            } else if (update::compare(pv, sv) < 0) {
                server.messagePlayer(name, std::string("An update is available: this server runs BlockSmith ") +
                                     BLOCKSMITH_VERSION + ", you have " + theirs + "." + where, gold);
            } else if (latestKnown && update::compare(pv, lv) < 0) {
                server.messagePlayer(name, "A new BlockSmith is out: " + u.latest + " (you have " + theirs + ")." + where, gold);
            }
            if (known && update::compare(pv, sv) > 0) {
                std::cout << "[Server] " << name << "'s game (BlockSmith " << theirs << ") is newer than this server ("
                          << BLOCKSMITH_VERSION << "): the server may need an update" << std::endl;
            }
        }
        // operators who handle reports hear about the open ones
        if (opLevelOf(name) >= 3 && reports.openCount() > 0) {
            server.messagePlayer(name, "There " + std::string(reports.openCount() == 1 ? "is 1 open report" : "are " +
                                 std::to_string(reports.openCount()) + " open reports") + ": type /reports to see them.",
                                 glm::vec3(1.0f, 0.55f, 0.45f));
        }
        // operators hear about a new version (they're the ones who can install it)
        if (ops.has(name) && (updates.info().available || updates.info().required)) {
            server.messagePlayer(name, "Server: " + updates.summary(), glm::vec3(1.0f, 0.78f, 0.28f));
        }
        ctx.api.playerJoined(name, newAccount);
    };

    // ---- Network ----
    auto applyLiveSettings = [&]() {
        server.setInfo(config.motd, config.maxPlayers);
        server.setIcon(loadIcon(confDir / config.icon));
        server.setMaxViewDistance(config.viewDistance);
        server.setGameTitle(ctx.game.title);
        server.setDefaultMode(config.gameMode);
        server.setShared(ClientModRules::kSharedKey, config.clientMods.encode());   // players' client mods
        dayClock.secondsPerDay = config.dayLengthMinutes * 60.0;                      // (day_length)
        server.setDayTime(dayClock);
        for (const auto& g : kGameRules) server.setShared(std::string("gamerule.") + g.name, ruleValue(g.name));
    };
    server.setServerId(loadOrCreateServerId(confDir / "server_id.txt"));   // passwords are bound to it
    applyLiveSettings();
    std::string error;
    if (!server.start(world, static_cast<uint16_t>(config.port), config.game, config.name, error, false)) {
        std::cerr << "[Server] " << error << " (another server already running? use --port)" << std::endl;
        return 1;
    }
    applyLiveSettings();   // again: starting the server clears shared values
    std::cout << "[Server] '" << config.name << "' (" << BLOCKSMITH_VERSION << ", commit " << BLOCKSMITH_COMMIT
              << ") is listening on port " << config.port << " (max "
              << config.maxPlayers << " players" << (config.whitelist ? ", whitelist on" : "")
              << (config.requirePassword ? ", passwords required" : "") << "). Type 'help' for commands (Tab completes them)." << std::endl;
    if (reports.openCount() > 0) {
        std::cout << "[Server] " << reports.openCount() << " open player report(s): type 'reports' to see them" << std::endl;
    }
    if (ops.entries.empty()) {
        std::cout << "[Server] No operators yet: join, then type 'op <your name>' here to manage the server "
                  << "from inside the game" << std::endl;
    }
    ctx.api.serverStarted();   // on_server_start()

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    // (filled in properly by the first tick; this is enough to complete commands right away)
    g_Console.start([](const std::string& line) {
        std::lock_guard<std::mutex> lock(g_ConsoleMutex);
        g_ConsoleLines.push_back(line);
    }, completeConsole);

    auto saveAll = [&]() {
        for (const auto& p : server.playerPlaces()) savePlayer(worldDir, p.name, p.pos, p.dimension);
        const int chunks = world.saveAll() + dimWorlds.saveAll();
        dimWorlds.forEach([&](const std::string& d, World& w) { blockMods.saveDimension(d, w.saveDirectory()); });
        blockMods.saveDimension(std::string(), worldDir.string());
        if (!entityManager.save(worldDir.string(), [&](Entity& e) { entityLua.saveData(e); })) {
            std::cerr << "[Server] Could not save entities.txt" << std::endl;
        }
        meta.timeLine = dayClock.saveLine();
        writeWorldMeta(worldDir / "world.dat", meta);
        if (ctx.storageDirty) {
            if (saveStorage(worldDir / "mod_storage.txt", ctx.storage)) ctx.storageDirty = false;
            else std::cerr << "[Server] Could not save mod_storage.txt (kept in memory, tried again later)" << std::endl;
        }
        return chunks;
    };

    double tps = config.tickRate;
    bool autosaveOn = true;                                 // save-off / save-on
    double slowestTickMs = 0.0, slowestThisWindow = 0.0;   // for 'tps'

    // ---- Commands: from the console (everything), or typed in the game as /command
    // (operators: everything except the console-only ones; everyone: help, list, msg) ----
    using Reply = std::function<void(const std::string&)>;
    // Operator levels, as in Minecraft: 1 builds at spawn, 2 game commands, 3 player management,
    // 4 everything. ops.txt: "Name | level" (no level: op_level in server.conf). The console is 4.
    auto levelOf = [&](const std::string& name) {
        if (!ops.has(name)) return 0;
        try { return std::clamp(std::stoi(ops.reason(name)), 1, 4); } catch (...) { return config.opLevel; }
    };
    static const std::map<std::string, int> kCommandLevel = {
        {"help", 0}, {"list", 0}, {"msg", 0}, {"tell", 0}, {"me", 0}, {"version", 0}, {"diag", 4}, {"report", 0}, {"reports", 3},
        {"seed", 2}, {"say", 2}, {"tps", 2}, {"gamemode", 2}, {"gm", 2}, {"time", 2}, {"gamerule", 2},
        {"tp", 2}, {"teleport", 2}, {"give", 2}, {"clear", 2}, {"kill", 2}, {"setworldspawn", 2}, {"spawnpoint", 2},
        {"kick", 3}, {"ban", 3}, {"unban", 3}, {"pardon", 3}, {"ban-ip", 3}, {"pardon-ip", 3}, {"banlist", 3},
        {"whitelist", 3}, {"op", 3}, {"deop", 3}, {"ops", 3},
        {"stop", 4}, {"save", 4}, {"save-all", 4}, {"save-on", 4}, {"save-off", 4}, {"reload", 4}, {"games", 4},
        {"mods", 4}, {"motd", 4}, {"name", 4}, {"password", 4},
    };

    // Usage and what it does, for the list Tab shows in players' chat
    static const std::map<std::string, std::pair<std::string, std::string>> kCommandUsage = {
        {"list", {"", "Who's online"}}, {"me", {"<action>", "Say what you're doing"}},
        {"msg", {"<player> <text>", "A private message"}}, {"tell", {"<player> <text>", "A private message"}},
        {"seed", {"", "The world's seed"}}, {"version", {"", "Which BlockSmith the server runs"}}, {"diag", {"", "Diagnostics: system, build, warnings"}},
        {"report", {"<player> <reason>", "Report a player to the server's staff"}},
        {"reports", {"[all|show|close|reopen|delete] [id] [note]", "Read and handle player reports"}}, {"say", {"<text>", "A message from the server"}},
        {"tps", {"", "How fast the server is running"}},
        {"gamemode", {"survival|creative [player]", "Change a game mode"}}, {"gm", {"survival|creative [player]", "Change a game mode"}},
        {"time", {"set|add|speed|query [value]", "The time of day"}}, {"gamerule", {"[rule] [true|false]", "Show or change the game rules"}},
        {"tp", {"<player|x y z> [x y z|player]", "Teleport"}}, {"teleport", {"<player|x y z> [x y z|player]", "Teleport"}},
        {"give", {"<player> <item> [count]", "Give items"}}, {"clear", {"[player]", "Empty an inventory"}},
        {"kill", {"[player]", "Kill a player"}}, {"setworldspawn", {"[x y z]", "Where new players start"}},
        {"spawnpoint", {"[player] [x y z]", "Where a player respawns"}},
        {"kick", {"<player> [reason]", "Remove a player"}}, {"ban", {"<player> [reason]", "Ban a player"}},
        {"unban", {"<player>", "Lift a ban"}}, {"pardon", {"<player>", "Lift a ban"}},
        {"ban-ip", {"<player|address> [reason]", "Ban an address"}}, {"pardon-ip", {"<address>", "Lift an address ban"}},
        {"banlist", {"[players|ips]", "Who's banned"}}, {"whitelist", {"on|off|add|remove|list [player]", "Who may join"}},
        {"op", {"<player> [level 1-4]", "Make an operator"}}, {"deop", {"<player>", "Remove an operator"}},
        {"ops", {"", "The operators"}}, {"stop", {"", "Save and stop the server"}}, {"save", {"", "Save the world now"}},
        {"save-all", {"", "Save the world now"}}, {"save-on", {"", "Turn autosave on"}}, {"save-off", {"", "Turn autosave off"}},
        {"reload", {"", "Reload the server's lists"}}, {"games", {"", "The games on this server"}},
        {"mods", {"[list|enable|disable] [id]", "The server's mods"}}, {"motd", {"[text]", "The message of the day"}},
        {"name", {"[text]", "The server's name"}}, {"password", {"reset <player>", "Reset a player's password"}},
    };

    auto handleCommand = [&](const std::string& line, const Reply& reply, const std::string& from) {
        const bool console = from.empty();
        const bool op = console || ops.has(from);       // (what mods' commands are told)
        const int level = console ? 4 : levelOf(from);
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::string rest;
        std::getline(ss, rest);
        rest = trim(rest);
        cmd = lower(cmd);
        auto firstWord = [](const std::string& s, std::string& after) {
            std::istringstream w(s);
            std::string word;
            w >> word;
            std::getline(w, after);
            after = trim(after);
            return word;
        };
        // Commands from mods (chat.register_command + on_chat_command(text, player, is_operator,
        // reply)): Lua decides who may use them
        static const std::set<std::string> builtIn = {"help", "list", "msg", "stop", "save", "reload", "tps", "seed", "version", "diag", "report", "reports",
            "games", "mods", "motd", "name", "say", "kick", "ban", "unban", "banlist", "whitelist", "op", "deop",
            "ops", "password", "respawn", "gamemode", "gm", "time", "gamerule", "tp", "teleport", "give", "clear",
            "kill", "me", "setworldspawn", "spawnpoint", "save-all", "save-on", "save-off", "ban-ip", "pardon-ip"};
        if (!builtIn.count(cmd)) {
            std::function<void(const std::string&)> luaReply = reply;
            bool handled = false;
            ctx.api.call("on_chat_command", [&](sol::protected_function_result& r) {
                handled = r.get_type() == sol::type::boolean && r.get<bool>();
            }, line, console ? std::string("(console)") : from, op, luaReply);
            if (handled) return;   // a mod handled it
        }
        // /respawn: back to the spawn point (sent by the game after dying when joined)
        if (cmd == "respawn" && !console) {
            // Like Minecraft: their own spawn point (/spawnpoint, a bed; "x y z [dimension]" -
            // a respawn anchor's is in its dimension), else the world's spawn - in the overworld,
            // wherever they died
            auto moveTo = [&](const std::string& d, const glm::vec3& p) {
                if (server.playerDimension(from) != d) server.setPlayerDimension(from, d, p);
                else server.teleportPlayer(from, p);
            };
            if (spawnPoints.has(from)) {
                std::istringstream sp(spawnPoints.reason(from));
                float x = 0, y = 0, z = 0;
                std::string d;
                if (sp >> x >> y >> z) {
                    sp >> d;
                    if (d == dims::kOverworld) d.clear();
                    if (d.empty() || dims::Registry::get().has(d)) { moveTo(d, glm::vec3(x, y, z)); return; }
                }
            }
            // the dimension they died in decides (its `respawn`: the overworld by default)
            const std::string target = dims::respawnDimension(server.playerDimension(from));
            if (!target.empty()) { moveTo(target, dims::Registry::get().type(target).spawn); return; }
            const int sx = config.spawnX, sz = config.spawnZ;
            int ground = world.groundHeightAt(sx, sz);
            if (ground < 0) ground = 80;
            moveTo(std::string(), glm::vec3(sx + 0.5f, static_cast<float>(ground), sz + 0.5f));
            return;
        }
        // /gamemode on its own: anyone may ask which mode they're in
        if ((cmd == "gamemode" || cmd == "gm") && rest.empty() && !console) {
            const std::string mode = server.playerMode(from);
            reply("Your game mode: " + (mode.empty() ? config.gameMode : mode) +
                  (op ? ". Change it with /gamemode survival|creative [name]" : ". Only operators can change it."));
            return;
        }
        {
            const auto need = kCommandLevel.find(cmd);
            if (need != kCommandLevel.end() && level < need->second) {
                reply(level == 0 ? "You need to be an operator to use /" + cmd + ". Type /help for your commands."
                                 : "/" + cmd + " needs operator level " + std::to_string(need->second) +
                                   " (yours is " + std::to_string(level) + ")");
                return;
            }
        }
        const std::string by = console ? "" : " (by " + from + ")";
        const std::string who = console ? std::string("Server") : from;

        // ---- Helpers for player arguments and coordinates (Minecraft's) ----
        auto words = [](const std::string& s) {
            std::vector<std::string> out;
            std::istringstream w(s);
            for (std::string x; w >> x;) out.push_back(x);
            return out;
        };
        auto positionOf = [&](const std::string& name, glm::vec3& out) {
            for (const auto& [n, p] : server.playerPositions()) if (n == name) { out = p; return true; }
            return false;
        };
        // A player argument: a name, or @a (everyone), @p (the nearest), @r (someone at random),
        // @s (yourself). Returns "" and the names, or why not.
        auto resolve = [&](const std::string& tok, std::vector<std::string>& out) -> std::string {
            out.clear();
            const auto names = server.playerNames();
            if (tok == "@s") {
                if (console) return "The console isn't a player: give a name";
                out.push_back(from);
                return "";
            }
            if (names.empty()) return "Nobody is online";
            if (tok == "@a") { out = names; return ""; }
            if (tok == "@r") {
                static std::mt19937 rng{std::random_device{}()};
                out.push_back(names[std::uniform_int_distribution<size_t>(0, names.size() - 1)(rng)]);
                return "";
            }
            if (tok == "@p") {
                glm::vec3 origin(config.spawnX + 0.5f, 64.0f, config.spawnZ + 0.5f);
                if (!console) positionOf(from, origin);
                float best = 1e30f;
                std::string nearest;
                for (const auto& [n, p] : server.playerPositions()) {
                    const glm::vec3 d = p - origin;
                    if (glm::dot(d, d) < best) { best = glm::dot(d, d); nearest = n; }
                }
                if (nearest.empty()) return "Nobody is online";
                out.push_back(nearest);
                return "";
            }
            for (const auto& n : names) if (lower(n) == lower(tok)) { out.push_back(n); return ""; }
            return "No player called '" + tok + "' is online";
        };
        // A coordinate: a number, or ~ / ~5 (relative to `base`). Whole numbers are block
        // centres for x and z, as in Minecraft.
        auto coord = [](const std::string& t, float base, bool centre, float& out) {
            try {
                if (!t.empty() && t[0] == '~') { out = base + (t.size() > 1 ? std::stof(t.substr(1)) : 0.0f); return true; }
                size_t used = 0;
                const float v = std::stof(t, &used);
                if (used != t.size()) return false;
                out = v + ((centre && t.find('.') == std::string::npos) ? 0.5f : 0.0f);
                return true;
            } catch (...) { return false; }
        };
        auto coords = [&](const std::vector<std::string>& a, size_t i, const glm::vec3& base, glm::vec3& out) {
            return a.size() >= i + 3 && coord(a[i], base.x, true, out.x) && coord(a[i + 1], base.y, false, out.y) &&
                   coord(a[i + 2], base.z, true, out.z);
        };
        auto fmt = [](const glm::vec3& p) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f, %.1f, %.1f", p.x, p.y, p.z);
            return std::string(buf);
        };

        if (cmd == "help") {
            reply("Everyone: list, msg <name> <text>, me <action>, report <player> <reason>, version, help");
            if (level >= 2) {
                reply("Game:     gamemode survival|creative [player], tp <player|x y z> [x y z|player], give <player> <item> [count],");
                reply("          clear [player], kill [player], time set|add|speed, gamerule [rule] [value], seed, say, tps");
                reply("          setworldspawn [x y z], spawnpoint [player] [x y z]   (players: a name, @a @p @r @s; ~ = relative)");
            }
            if (level >= 3) {
                reply("Players:  kick, ban, unban, ban-ip, pardon-ip, banlist [players|ips], whitelist on|off|add|remove|list");
                reply("Admins:   op <name> [level 1-4], deop <name>, ops");
                reply("Reports:  reports [all], reports show|close|reopen <id> [note], reports delete <id>");
            }
            if (level >= 4) {
                reply("Server:   stop, save (save-all), save-off, save-on, reload, games, mods [enable|disable <id>], diag,");
                reply("          motd [text], name [text], password reset <name>");
            }
            if (level == 0) reply("Operators have more: ask one, or see the server's help.");
            for (const auto& c : ctx.modCommands) reply("Mods:     " + c.usage + (c.description.empty() ? "" : " - " + c.description));
        } else if (cmd == "stop") {
            reply("Stopping the server...");
            std::cout << "[Server] Stop" << by << std::endl;
            g_Running = false;
        } else if (cmd == "save" || cmd == "save-all") {
            reply("Saved (" + std::to_string(saveAll()) + " changed chunks)");
        } else if (cmd == "save-off") {
            autosaveOn = false;
            reply("Autosave is off ('save-on' turns it back on; 'save-all' saves now)");
        } else if (cmd == "save-on") {
            autosaveOn = true;
            reply("Autosave is on (every " + std::to_string(config.autosaveSeconds) + " s)");
        } else if (cmd == "tps") {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "Ticks per second: %.1f (target %d), slowest tick %.0f ms",
                          tps, config.tickRate, slowestTickMs);
            reply(buf);
            std::snprintf(buf, sizeof(buf), "World generation: %d threads, %.1f chunks/s, %zu waiting, %zu chunks loaded",
                          generator.threadCount(), generator.chunksPerSecond(), world.generatingCount(),
                          world.loadedChunkCount());
            reply(buf);
        } else if (cmd == "version") {
            reply(std::string(BLOCKSMITH_VERSION_FULL) + ", protocol " + net::kVersionName);
            if (!updates.enabled()) reply("Update check: off (update_check in server.conf)");
            else if (updates.summary().empty()) reply("Update check: still asking...");
            else {
                reply(updates.summary());
                if (updates.warning()) for (const auto& t : updates.trace()) reply("  " + t);
            }
        } else if (cmd == "report") {
            // /report <player> <reason>: saved for the server's staff, with the chat around it
            std::string reason;
            const std::string target = firstWord(rest, reason);
            const std::string reporter = console ? std::string("(console)") : from;
            if (target.empty() || reason.empty()) { reply("Usage: report <player> <reason>"); return; }
            if (!console && lower(target) == lower(from)) { reply("You can't report yourself."); return; }
            // someone who is here, or has been (their save, or their account)
            std::string targetName;
            glm::vec3 tpos(0.0f);
            bool online = false;
            for (const auto& [n, p] : server.playerPositions()) if (lower(n) == lower(target)) { targetName = n; tpos = p; online = true; }
            if (targetName.empty()) {
                // someone who has played here: their save (players/<Name>.txt, any capitals), or account
                std::error_code fec;
                for (const auto& e : fs::directory_iterator(worldDir / "players", fec)) {
                    if (e.path().extension() == ".txt" && lower(e.path().stem().string()) == lower(target)) targetName = e.path().stem().string();
                }
                if (targetName.empty() && accounts.has(target)) targetName = target;
            }
            if (targetName.empty() || !net::validPlayerName(targetName)) { reply("There's no player called '" + target + "' on this server."); return; }
            if (!console) {
                const auto now = std::chrono::steady_clock::now();
                auto it = lastReport.find(lower(from));
                if (it != lastReport.end() && now - it->second < std::chrono::seconds(60)) {
                    reply("Please wait a minute before sending another report.");
                    return;
                }
                if (reports.openBy(from, targetName) >= 3) { reply("You already have 3 open reports about " + targetName + "."); return; }
                lastReport[lower(from)] = now;
            }
            Report r;
            r.id = reports.nextId++;
            r.time = static_cast<long long>(std::time(nullptr));
            r.reporter = reporter;
            r.target = targetName;
            r.reason = update::plainText(reason, 300);
            r.targetPos = online ? fmt(tpos) : "(offline)";
            r.reporterPos = "(console)";
            if (!console) for (const auto& [n, p] : server.playerPositions()) if (n == from) r.reporterPos = fmt(p);
            // the chat around it: what either of them said in the last 15 minutes (up to 30 lines)
            for (const auto& c : recentChat) {
                if (r.time - c.time > 15 * 60) continue;
                if (lower(c.name) != lower(targetName) && lower(c.name) != lower(from)) continue;
                r.chat.push_back("[" + dateText(c.time).substr(11) + "] <" + c.name + "> " + update::plainText(c.text, 300));
            }
            if (r.chat.size() > 30) r.chat.erase(r.chat.begin(), r.chat.end() - 30);
            reports.list.push_back(r);
            if (!reports.save()) std::cerr << "[Server] Could not write " << reports.file.string() << std::endl;
            reply("Thanks - your report (#" + std::to_string(r.id) + ") about " + targetName + " was sent to the server's staff.");
            std::cout << "[Report] #" << r.id << " " << reporter << " reported " << targetName << ": " << r.reason
                      << " (" << targetName << " at " << r.targetPos << ")" << std::endl;
            for (const auto& [n, p] : server.playerPositions()) {
                if (n != from && opLevelOf(n) >= 3) {
                    server.messagePlayer(n, "[Report #" + std::to_string(r.id) + "] " + reporter + " reported " + targetName + ": " +
                                         r.reason + "  (/reports show " + std::to_string(r.id) + ")", glm::vec3(1.0f, 0.55f, 0.45f));
                }
            }
            ctx.api.call("on_player_report", [](sol::protected_function_result&) {}, r.id, reporter, targetName, r.reason);
        } else if (cmd == "reports") {
            // reports [all] | show <id> | close <id> [note] | reopen <id> | delete <id>
            std::string args;
            const std::string sub = lower(firstWord(rest, args));
            std::string note;
            const std::string idText = firstWord(args, note);
            int id = 0;
            try { id = idText.empty() ? 0 : std::stoi(idText); } catch (...) { id = 0; }
            const std::string who = console ? std::string("(console)") : from;
            if (sub.empty() || sub == "all" || sub == "list") {
                const bool all = sub == "all";
                int shown = 0;
                for (auto it = reports.list.rbegin(); it != reports.list.rend(); ++it) {
                    if (!all && it->status != "open") continue;
                    if (++shown > 20) { reply("...and more (oldest not shown)"); break; }
                    reply("#" + std::to_string(it->id) + " " + (it->status == "open" ? "OPEN  " : "closed") + " " + agoText(it->time) +
                          ": " + it->reporter + " -> " + it->target + ": " + update::plainText(it->reason, 80));
                }
                if (shown == 0) reply(all ? "No reports yet." : "No open reports. ('reports all' shows the closed ones too)");
                else reply("'reports show <id>' for the details, 'reports close <id> [note]' when it's dealt with.");
            } else if (Report* r = reports.find(id); !r) {
                reply(idText.empty() ? "Usage: reports show|close|reopen|delete <id>" : "There's no report #" + idText + ".");
            } else if (sub == "show") {
                reply("Report #" + std::to_string(r->id) + " (" + r->status + "), " + dateText(r->time) + " (" + agoText(r->time) + ")");
                reply("  From: " + r->reporter + " at " + r->reporterPos);
                reply("  About: " + r->target + " at " + r->targetPos);
                reply("  Reason: " + r->reason);
                if (!r->handledBy.empty()) reply("  Closed by " + r->handledBy + (r->note.empty() ? "" : ": " + r->note));
                reply(r->chat.empty() ? "  Chat: (nothing either of them said)" : "  Chat (theirs, the 15 minutes before):");
                for (const auto& l : r->chat) reply("    " + l);
            } else if (sub == "close") {
                r->status = "closed";
                r->handledBy = who;
                r->note = update::plainText(note, 200);
                reports.save();
                reply("Report #" + std::to_string(r->id) + " closed.");
                std::cout << "[Report] #" << r->id << " closed by " << who << (note.empty() ? "" : ": " + note) << std::endl;
            } else if (sub == "reopen") {
                r->status = "open";
                reports.save();
                reply("Report #" + std::to_string(r->id) + " is open again.");
            } else if (sub == "delete") {
                if (level < 4) { reply("Only level 4 operators (and the console) can delete reports."); return; }
                const int gone = r->id;
                reports.list.erase(std::remove_if(reports.list.begin(), reports.list.end(),
                                                  [gone](const Report& x) { return x.id == gone; }), reports.list.end());
                reports.save();
                reply("Report #" + std::to_string(gone) + " deleted.");
            } else {
                reply("Usage: reports [all] | reports show|close|reopen|delete <id> [note]");
            }
        } else if (cmd == "diag") {
            // Everything that helps find a problem: the build and system, how it's running, the
            // update check and the warnings so far (most frequent first; full traces are in the log)
            for (const auto& l : diag::systemReport()) reply(l);
            char buf[200];
            std::snprintf(buf, sizeof(buf), "Running: %.1f ticks/s (target %d), slowest tick %.0f ms, %zu chunks loaded, %zu players",
                          tps, config.tickRate, slowestTickMs, world.loadedChunkCount(), server.playerPositions().size());
            reply(buf);
            reply("Updates: " + (updates.enabled() ? (updates.summary().empty() ? std::string("still asking") : updates.summary())
                                                   : std::string("check off")));
            const auto warnings = diag::warningSummary(15);
            reply(warnings.empty() ? "Warnings: none" : "Warnings (count x first message):");
            for (const auto& w : warnings) reply("  " + w);
        } else if (cmd == "seed") {
            reply("Seed: " + std::to_string(meta.seed));
        } else if (cmd == "reload") {
            const ServerConfig fresh = readSettings();
            if (fresh.game != config.game || fresh.world != config.world || fresh.port != config.port ||
                fresh.disabledMods != config.disabledMods) {
                reply("game, world, port and mod changes need a restart");
            }
            const std::string game = config.game, worldName = config.world;
            const int port = config.port;
            const auto mods = config.disabledMods;
            config = fresh;
            config.game = game; config.world = worldName; config.port = port; config.disabledMods = mods;
            whitelist.load(); banned.load(); ops.load(); accounts.load(); playerModes.load();
            applyLiveSettings();
            reply("Reloaded settings, whitelist, bans, ops and accounts");
        } else if (cmd == "motd") {
            if (rest.empty()) {
                reply("Description: " + config.motd + "  (set it with: motd <text>, using {gold}, {b}, {n}...)");
            } else {
                config.motd = rest;
                server.setInfo(config.motd, config.maxPlayers);
                updateConfValue(configFile, "motd", rest);
                reply("Description changed (and saved)");
            }
        } else if (cmd == "name") {
            if (rest.empty()) reply("Name: " + config.name);
            else {
                config.name = rest;
                updateConfValue(configFile, "name", rest);
                reply("Name saved - shown on the LAN list after a restart");
            }
        } else if (cmd == "games") {
            for (const auto& g : listGames()) reply("  " + g.folder + " - " + g.title + (g.folder == config.game ? "  (running)" : ""));
        } else if (cmd == "mods") {
            std::string id;
            const std::string sub = lower(firstWord(rest, id));
            const auto mods = listServerMods(config.game);
            auto find = [&](const std::string& want) -> const ModEntry* {
                for (const auto& m : mods) if (m.id == want) return &m;
                return nullptr;
            };
            if (sub.empty() || sub == "list") {
                // What's on now (from server.conf - changes since the start need a restart)
                const auto saved = parseConfig(readConfFile(configFile)).disabledMods;
                for (const auto& m : mods) {
                    const bool on = m.required || !saved.count(m.id);
                    reply(std::string(on ? "  [x] " : "  [ ] ") + m.id + " - " + m.title + " (" + m.source +
                          (m.required ? ", required" : "") + ")");
                }
                reply("mods enable <id> / mods disable <id>, then restart the server");
            } else if ((sub == "enable" || sub == "disable") && !id.empty()) {
                const ModEntry* m = find(id);
                if (!m) { reply("No mod called '" + id + "'. Type 'mods' to list them."); return; }
                if (m->required && sub == "disable") { reply(id + " is required - it can't be turned off"); return; }
                auto saved = parseConfig(readConfFile(configFile)).disabledMods;
                if (sub == "disable") saved.insert(id); else saved.erase(id);
                updateConfValue(configFile, "disabled_mods", joinIds(saved));
                reply("Mod " + id + " " + sub + "d - restart the server to apply (players need the same mods)");
            } else {
                reply("Usage: mods [list] | mods enable <id> | mods disable <id>");
            }
        } else if (cmd == "list") {
            const auto names = server.playerNames();
            std::string all;
            for (const auto& n : names) all += (all.empty() ? "" : ", ") + n + (ops.has(n) ? " (op)" : "");
            reply(std::to_string(names.size()) + "/" + std::to_string(config.maxPlayers) + " players online" +
                  (names.empty() ? "" : ": " + all));
        } else if (cmd == "say") {
            const std::string who = console ? "Server" : from;
            server.broadcastMessage("[" + who + "] " + rest, glm::vec3(1.0f, 0.4f, 1.0f));   // (the console shows it: [Chat])
        } else if (cmd == "msg" || cmd == "tell") {
            std::string text;
            const std::string name = firstWord(rest, text);
            const std::string sender = console ? "Server" : from;
            if (server.messagePlayer(name, "[" + sender + " -> you] " + text, glm::vec3(1.0f, 0.4f, 1.0f))) {
                reply("[you -> " + name + "] " + text);
            } else {
                reply("No player called '" + name + "' online");
            }
        } else if (cmd == "kick") {
            std::string reason;
            const std::string name = firstWord(rest, reason);
            if (server.kickPlayer(name, reason)) { reply("Kicked " + name); std::cout << "[Server] Kicked " << name << by << std::endl; }
            else reply("No player called '" + name + "' online");
        } else if (cmd == "ban") {
            std::string reason;
            const std::string name = firstWord(rest, reason);
            if (!net::validPlayerName(name)) { reply("Usage: ban <name> [reason]"); return; }
            if (!console && lower(name) == lower(from)) { reply("You can't ban yourself"); return; }
            banned.add(name, reason);
            server.kickPlayer(name, "You are banned from this server" + (reason.empty() ? "" : ": " + reason));
            reply("Banned " + name);
            std::cout << "[Server] Banned " << name << by << std::endl;
        } else if (cmd == "unban" || cmd == "pardon") {
            reply(banned.remove(rest) ? "Unbanned " + rest : "'" + rest + "' wasn't banned");
        } else if (cmd == "banlist") {
            const std::string which = lower(trim(rest));
            if (which.empty() || which == "players") reply("Banned players: " + banned.names());
            if (which.empty() || which == "ips") reply("Banned addresses: " + bannedIps.names());
        } else if (cmd == "whitelist") {
            std::string arg;
            const std::string sub = lower(firstWord(rest, arg));
            if (sub == "on" || sub == "off") {
                config.whitelist = (sub == "on");
                updateConfValue(configFile, "whitelist", config.whitelist ? "true" : "false");
                reply("Whitelist " + sub);
            } else if (sub == "add" && net::validPlayerName(arg)) {
                whitelist.add(arg);
                reply("Added " + arg + " to the whitelist");
            } else if (sub == "remove") {
                reply(whitelist.remove(arg) ? "Removed " + arg : "'" + arg + "' isn't on it");
            } else if (sub == "list") {
                reply("Whitelist (" + std::string(config.whitelist ? "on" : "off") + "): " + whitelist.names());
            } else {
                reply("Usage: whitelist on|off|add <name>|remove <name>|list");
            }
        } else if (cmd == "op") {
            std::string lvl;
            const std::string name = firstWord(rest, lvl);
            if (!net::validPlayerName(name)) { reply("Usage: op <name> [level 1-4]"); return; }
            int newLevel = config.opLevel;
            if (!lvl.empty()) {
                try { newLevel = std::stoi(lvl); } catch (...) { newLevel = 0; }
                if (newLevel < 1 || newLevel > 4) { reply("The level is 1 to 4"); return; }
            }
            if (!console && newLevel > level) { reply("You can't give a higher level than your own (" + std::to_string(level) + ")"); return; }
            ops.add(name, std::to_string(newLevel));
            server.setPlayerOperator(name, true);
            reply(name + " is now an operator (level " + std::to_string(newLevel) + ")");
            const std::string rest = name;      // (the lines below use it)
            server.messagePlayer(rest, "You are now an operator: type /help for server commands.", glm::vec3(0.6f, 0.9f, 1.0f));
            if (!accounts.has(rest)) {
                reply("Tip: " + rest + " should set a password (Settings > Player) and rejoin, so nobody else can use the name");
            }
        } else if (cmd == "deop") {
            reply(ops.remove(rest) ? rest + " is no longer an operator" : "'" + rest + "' isn't an operator");
            server.setPlayerOperator(rest, false);
        } else if (cmd == "gamemode" || cmd == "gm") {
            // /gamemode <survival|creative> [name] - operators and the console only (checked above)
            std::string who;
            const std::string word = lower(firstWord(rest, who));
            static const std::map<std::string, std::string> modes = {
                {"survival", "survival"}, {"s", "survival"}, {"0", "survival"},
                {"creative", "creative"}, {"c", "creative"}, {"1", "creative"}};
            const auto m = modes.find(word);
            if (m == modes.end()) { reply("Usage: /gamemode survival|creative [name]"); return; }
            if (who.empty()) who = from;
            if (who.empty()) { reply("Usage (console): gamemode survival|creative <name>"); return; }
            // Use the player's real spelling if they're online
            for (const auto& n : server.playerNames()) if (lower(n) == lower(who)) who = n;
            if (m->second == config.gameMode) playerModes.remove(who);   // back to the default
            else playerModes.add(who, m->second);
            const bool online = server.setPlayerMode(who, m->second);
            reply(who + "'s game mode is now " + m->second + (online ? "" : " (from when they next join)"));
            if (online && who != from) {
                server.messagePlayer(who, "Your game mode is now " + m->second + by, glm::vec3(0.6f, 0.9f, 1.0f));
            }
            std::cout << "[Server] " << who << " -> " << m->second << by << std::endl;
        } else if (cmd == "me") {
            if (rest.empty()) { reply("Usage: me <action>"); return; }
            server.broadcastMessage("* " + who + " " + rest, glm::vec3(1.0f, 1.0f, 1.0f));   // (the console shows it: [Chat])
        } else if (cmd == "tp" || cmd == "teleport") {
            // tp <x y z> | tp <player> | tp <players> <x y z> | tp <players> <player>
            const auto a = words(rest);
            std::vector<std::string> targets;
            size_t destAt = 0;
            if (a.size() == 1 || a.size() == 3) {
                if (console) { reply("Usage (console): tp <player> <x y z>  or  tp <player> <other player>"); return; }
                targets.push_back(from);
            } else if (a.size() == 2 || a.size() == 4) {
                const std::string err = resolve(a[0], targets);
                if (!err.empty()) { reply(err); return; }
                destAt = 1;
            } else {
                reply("Usage: tp <x y z> | tp <player> | tp <players> <x y z> | tp <players> <player>");
                return;
            }
            const bool toPlayer = (a.size() - destAt) == 1;
            glm::vec3 dest(0.0f);
            if (toPlayer) {
                std::vector<std::string> d;
                const std::string err = resolve(a[destAt], d);
                if (!err.empty()) { reply(err); return; }
                if (d.size() != 1) { reply("Teleport to one player (not @a)"); return; }
                positionOf(d[0], dest);
            }
            int moved = 0;
            for (const auto& t : targets) {
                glm::vec3 base(0.0f), to = dest;
                positionOf(t, base);
                if (!toPlayer && !coords(a, destAt, base, to)) { reply("Coordinates are numbers, or ~ for where you are (~5 = 5 more)"); return; }
                if (server.teleportPlayer(t, to)) {
                    ++moved;
                    if (t != from) server.messagePlayer(t, "You were teleported to " + fmt(to) + by, glm::vec3(0.6f, 0.9f, 1.0f));
                    if (targets.size() == 1) reply("Teleported " + t + " to " + fmt(to));
                }
            }
            if (targets.size() > 1) reply("Teleported " + std::to_string(moved) + " players");
        } else if (cmd == "give") {
            // give <players> <item> [count]
            const auto a = words(rest);
            if (a.size() < 2 || a.size() > 3) { reply("Usage: give <player> <item> [count]"); return; }
            std::vector<std::string> targets;
            const std::string err = resolve(a[0], targets);
            if (!err.empty()) { reply(err); return; }
            const std::string item = lower(a[1]);
            if (BlockRegistry::getInstance().getBlockId(item) == 0) { reply("There's no item called '" + a[1] + "'"); return; }
            int count = 1;
            if (a.size() == 3) {
                try { count = std::stoi(a[2]); } catch (...) { count = 0; }
                if (count < 1 || count > 64 * 64) { reply("The count is 1 to 4096"); return; }
            }
            for (const auto& t : targets) {
                server.sendAction(t, "give", item, std::to_string(count));
                reply("Gave " + std::to_string(count) + " " + item + " to " + t);
            }
        } else if (cmd == "clear" || cmd == "kill") {
            // clear [players] / kill [players]
            std::vector<std::string> targets;
            if (rest.empty()) {
                if (console) { reply("Usage (console): " + cmd + " <player>"); return; }
                targets.push_back(from);
            } else {
                const std::string err = resolve(trim(rest), targets);
                if (!err.empty()) { reply(err); return; }
            }
            for (const auto& t : targets) {
                if (cmd == "clear") {
                    server.sendAction(t, "clear");
                    reply("Cleared the inventory of " + t);
                } else {
                    server.sendAction(t, "kill", t == from ? "gave up" : "was killed" + by);
                    reply("Killed " + t);
                }
            }
            if (cmd == "kill") std::cout << "[Server] Killed " << targets.size() << " player(s)" << by << std::endl;
        } else if (cmd == "setworldspawn") {
            const auto a = words(rest);
            glm::vec3 base(config.spawnX + 0.5f, 64.0f, config.spawnZ + 0.5f), at = base;
            if (!console) positionOf(from, base), at = base;
            if (!a.empty() && !coords(a, 0, base, at)) { reply("Usage: setworldspawn [x y z]"); return; }
            if (console && a.empty()) { reply("Usage (console): setworldspawn <x y z>"); return; }
            config.spawnX = static_cast<int>(std::floor(at.x));
            config.spawnZ = static_cast<int>(std::floor(at.z));
            updateConfValue(configFile, "spawn", std::to_string(config.spawnX) + " " + std::to_string(config.spawnZ));
            reply("The world's spawn is now " + std::to_string(config.spawnX) + ", " + std::to_string(config.spawnZ) +
                  (config.spawnProtection > 0 ? " (protected " + std::to_string(config.spawnProtection) + " blocks around)" : ""));
        } else if (cmd == "spawnpoint") {
            // spawnpoint [players] [x y z]: where they come back after dying
            const auto a = words(rest);
            std::vector<std::string> targets;
            size_t at = 0;
            if (a.empty() || a.size() == 3) {
                if (console) { reply("Usage (console): spawnpoint <player> [x y z]"); return; }
                targets.push_back(from);
            } else {
                const std::string err = resolve(a[0], targets);
                if (!err.empty()) { reply(err); return; }
                at = 1;
            }
            for (const auto& t : targets) {
                glm::vec3 base(0.0f), p(0.0f);
                positionOf(t, base);
                p = base;
                if (a.size() > at && !coords(a, at, base, p)) { reply("Usage: spawnpoint [player] [x y z]"); return; }
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", p.x, p.y, p.z);
                spawnPoints.add(t, buf);
                reply(t + " will now respawn at " + fmt(p));
            }
        } else if (cmd == "gamerule") {
            const auto a = words(rest);
            if (a.empty()) {
                for (const auto& g : kGameRules) {
                    const std::string v = ruleValue(g.name);
                    reply(std::string(g.name) + " = " + (v.empty() ? "(the game decides)" : v) + "   - " + g.about);
                }
                return;
            }
            const GameRuleDef* g = findGameRule(a[0]);
            if (!g) { reply("No game rule called '" + a[0] + "'. Type 'gamerule' for the list."); return; }
            if (a.size() == 1) {
                const std::string v = ruleValue(g->name);
                reply(std::string(g->name) + " = " + (v.empty() ? "(the game decides)" : v));
                return;
            }
            const std::string v = lower(a[1]);
            const bool numeric = std::isdigit(static_cast<unsigned char>(g->defaultValue[0])) != 0;
            if (numeric) {
                bool ok = v == "default" || (!v.empty() && v.size() <= 4 && std::all_of(v.begin(), v.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }));
                if (!ok) { reply(std::string(g->name) + " is a number (0 to 9999) or default"); return; }
            } else if (v != "true" && v != "false" && v != "default") { reply("A game rule is true, false or default"); return; }
            if (v == "default") gameRules.erase(g->name); else gameRules[g->name] = v;
            saveGameRules();
            server.setShared(std::string("gamerule.") + g->name, ruleValue(g->name));
            reply(std::string("Game rule ") + g->name + " is now " + (ruleValue(g->name).empty() ? "the game's default" : ruleValue(g->name)));
            std::cout << "[Server] Game rule " << g->name << " = " << v << by << std::endl;
        } else if (cmd == "ban-ip") {
            std::string reason;
            const std::string target = firstWord(rest, reason);
            if (target.empty()) { reply("Usage: ban-ip <address|player> [reason]"); return; }
            std::string ip = target;
            for (const auto& n : server.playerNames()) if (lower(n) == lower(target)) ip = server.playerAddress(n);
            if (ip.find('.') == std::string::npos && ip.find(':') == std::string::npos) {
                reply("'" + target + "' isn't an IP address or a player who's online"); return;
            }
            bannedIps.add(ip, reason);
            int kicked = 0;
            for (const auto& n : server.playerNames()) {
                if (server.playerAddress(n) == ip) { server.kickPlayer(n, "Your IP address is banned" + (reason.empty() ? std::string() : ": " + reason)); ++kicked; }
            }
            reply("Banned the address " + ip + (kicked ? " (kicked " + std::to_string(kicked) + ")" : ""));
            std::cout << "[Server] Banned IP " << ip << by << std::endl;
        } else if (cmd == "pardon-ip") {
            reply(bannedIps.remove(trim(rest)) ? "Unbanned " + trim(rest) : "'" + trim(rest) + "' wasn't banned");
        } else if (cmd == "time") {
            // time [set <day|noon|night|midnight|sunrise|sunset|hh:mm|0..1>] [add <hours>] [speed <x>]
            std::string arg;
            const std::string sub = lower(firstWord(rest, arg));
            auto report = [&]() {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "Day %lld, %s (time %.3f). A day lasts %g minutes%s.",
                              static_cast<long long>(dayClock.day()), dayClock.clock().c_str(), dayClock.time(),
                              dayClock.secondsPerDay / 60.0,
                              ruleValue("doDaylightCycle") == "false" ? "; time is stopped (gamerule doDaylightCycle)"
                              : dayClock.speed == 0.0 ? "; time is stopped ('time speed 1' starts it)"
                              : dayClock.speed == 1.0 ? "" : (", running at " + std::to_string(dayClock.speed).substr(0, 4) + "x").c_str());
                reply(buf);
            };
            if (sub.empty() || sub == "query") {
                report();
            } else if (sub == "set") {
                const double t = DayClock::parseTimeOfDay(trim(arg));
                if (t < 0.0) { reply("Usage: time set day|noon|night|midnight|sunrise|sunset|<hh:mm>|<0..1>"); return; }
                dayClock.setTimeOfDay(t, true);     // forward to it, like Minecraft
                std::cout << "[Server] Time set to " << dayClock.clock() << by << std::endl;
                report();
            } else if (sub == "add") {
                double hours = 0.0;
                try { hours = std::stod(arg); } catch (...) { reply("Usage: time add <hours>"); return; }
                if (!std::isfinite(hours)) { reply("Usage: time add <hours>"); return; }
                dayClock.total = std::max(0.0, dayClock.total + hours / 24.0);
                report();
            } else if (sub == "speed") {
                double v = -1.0;
                try { v = std::stod(arg); } catch (...) {}
                if (!std::isfinite(v) || v < 0.0 || v > 1000.0) { reply("Usage: time speed <multiplier>   (1 normal, 0 stops time)"); return; }
                dayClock.speed = v;
                report();
            } else {
                reply("Usage: time | time set <day|noon|night|midnight|hh:mm> | time add <hours> | time speed <x>");
            }
        } else if (cmd == "ops") {
            std::string all;
            for (const auto& [key, v] : ops.entries) {
                const std::string n = v.substr(0, v.find('|'));
                all += (all.empty() ? "" : ", ") + n + " (" + std::to_string(levelOf(n)) + ")";
            }
            reply("Operators (level): " + (all.empty() ? std::string("(nobody)") : all));
        } else if (cmd == "password") {
            std::string name;
            const std::string sub = lower(firstWord(rest, name));
            if (sub == "reset" && !name.empty()) {
                reply(accounts.remove(name) ? "Password for " + name + " removed - the next person to join with a password gets the name"
                                            : "'" + name + "' has no password here");
            } else {
                reply("Usage: password reset <name>");
            }
        } else {
            reply("Unknown command '" + cmd + "'. Type " + std::string(console ? "help" : "/help") + ".");
        }
    };

    // Commands typed in the game (/kick Bob ...) arrive here
    server.onCommand = [&](const std::string& name, const std::string& command) {
        std::cout << "[Command] " << name << ": /" << command << std::endl;
        handleCommand(command, [&server, name](const std::string& text) {
            server.messagePlayer(name, text, glm::vec3(0.75f, 0.85f, 1.0f));
        }, name);
    };
    const Reply consoleReply = [](const std::string& text) { std::cout << text << std::endl; };

    // Tab in a player's chat: the same completions as the console, but only the commands their
    // operator level allows; mods add their commands' arguments with on_chat_complete(words,
    // word, player) -> { ... }. Plain chat: the players online.
    server.onComplete = [&](const std::string& name, const std::string& text) -> std::vector<std::string> {
        std::vector<std::string> out;
        if (text.empty() || text[0] != '/') return server.playerNames();
        const int level = levelOf(name);
        std::vector<std::string> before;
        std::string word;
        {
            std::string body = text.substr(1), cur;
            for (char c : body) {
                if (c == ' ') { if (!cur.empty()) before.push_back(cur); cur.clear(); }
                else cur += c;
            }
            word = cur;
        }
        auto allowed = [&](const std::string& cmd) {
            const auto it = kCommandLevel.find(lower(cmd));
            return it == kCommandLevel.end() || level >= it->second;
        };
        if (before.empty()) {                        // the command's name, "name\tlabel" (shown in the list)
            std::set<std::string> seen;
            auto label = [](const std::string& usage, const std::string& description) {
                std::string l = usage;
                for (char& ch : l) if (static_cast<unsigned char>(ch) < 32) ch = ' ';
                std::string d = description;
                for (char& ch : d) if (static_cast<unsigned char>(ch) < 32) ch = ' ';
                return d.empty() ? l : l + "  -  " + d;
            };
            for (const auto& c : kConsoleCommands) {
                if (c == "help" || !allowed(c) || !seen.insert(c).second) continue;
                const auto u = kCommandUsage.find(c);
                out.push_back(u == kCommandUsage.end() ? c : c + "\t" + label("/" + c + (u->second.first.empty() ? "" : " " + u->second.first), u->second.second));
            }
            for (const auto& c : ctx.modCommands) {          // server mods' commands (chat.register_command)
                if (!allowed(c.name) || !seen.insert(c.name).second) continue;
                std::string usage = c.usage.empty() ? "/" + c.name : c.usage;
                if (usage[0] != '/') usage = "/" + c.name + " " + usage;
                out.push_back(c.name + "\t" + label(usage, c.description));
            }
            return out;
        }
        if (!allowed(before[0])) return out;          // (nothing about commands they can't use)
        if (kCommandLevel.count(lower(before[0]))) out = completeConsole(before, word);
        ctx.api.call("on_chat_complete", [&](sol::protected_function_result& r) {
            if (r.get_type() != sol::type::table) return;
            sol::table t = r;
            for (size_t i = 1; i <= t.size() && out.size() < 200; ++i) {
                sol::object v = t[i];
                if (v.is<std::string>()) out.push_back(v.as<std::string>());
            }
        }, sol::as_table(before), word, name);
        std::vector<std::string> unique;              // (the console's list and mods' can overlap)
        for (auto& x : out) {
            if (x.empty() || std::find(unique.begin(), unique.end(), x) != unique.end()) continue;
            unique.push_back(std::move(x));
        }
        return unique;
    };

    // ---- Main loop: fixed ticks, sleeping in between ----
    const auto tick = std::chrono::microseconds(1000000 / config.tickRate);
    const float tickDt = 1.0f / static_cast<float>(config.tickRate);
    auto nextTick = Clock::now();
    auto lastSave = Clock::now();
    auto nextCompletionUpdate = Clock::now();               // (console Tab completion)
    auto nextModListUpdate = Clock::now();
    std::vector<std::string> completionModsOn, completionModsOff;
    auto lastLagWarning = Clock::now() - std::chrono::seconds(60);
    int ticksThisSecond = 0;
    auto tpsWindow = Clock::now();

    // The game's My Servers screen stops a server by creating stop.request next to its
    // server.conf (works even though the server runs in its own window).
    const fs::path stopRequest = confDir / "stop.request";
    fs::remove(stopRequest, ec);
    auto lastStopCheck = Clock::now();

    while (g_Running) {
        const auto tickStart = Clock::now();
        if (Clock::now() - lastStopCheck >= std::chrono::seconds(1)) {
            lastStopCheck = Clock::now();
            if (fs::exists(stopRequest, ec)) {
                fs::remove(stopRequest, ec);
                std::cout << "[Server] Stop requested from the game" << std::endl;
                g_Running = false;
                break;
            }
        }
        world.update(spawnCenter, lua);   // spawn + every player's surroundings stay loaded
        if (!world.fatalError().empty()) {    // the world generator stopped working: save and stop
            std::cerr << "[Server] " << world.fatalError() << " - saving and stopping" << std::endl;
            g_Running = false;
            break;
        }
        server.update(spawnCenter, 0.0f, 0.0f, tickDt);
        world.updateFluids(tickDt);   // each fluid at its own speed (Fluids.h)
        // The other dimensions with players in them: loading around those players, fluids...
        // and the ones left empty a while are saved and unloaded
        std::map<std::string, std::vector<glm::vec3>> playersByDim;
        for (const auto& p : server.playerPlaces()) playersByDim[p.dimension].push_back(p.pos);
        dimWorlds.update(lua, tickDt, std::chrono::duration<double>(Clock::now() - serverStart).count(), playersByDim);

        // Lua every tick: shared values that changed, then on_update (weather etc.)
        // (time-limited, and a repeating error is printed once - not every tick)
        if (!ctx.sharedChanged.empty()) {
            std::vector<std::string> changed;
            changed.swap(ctx.sharedChanged);
            for (const auto& k : changed) {
                const auto it = server.shared().find(k);
                ctx.api.call("on_shared_changed", [](sol::protected_function_result&) {},
                             k, it == server.shared().end() ? std::string() : it->second);
            }
        }
        ctx.api.call("on_update", [](sol::protected_function_result&) {}, tickDt);
        entityLua.step(tickDt);   // mobs move and think, items get picked up, new mobs spawn
        {                         // ABMs near players, LBMs on chunks that just loaded (each dimension's own)
            blockMods.update(tickDt, world, playersByDim[std::string()]);
            dimWorlds.forEach([&](const std::string& d, World& w) {
                dims::InDimension here(d);
                blockMods.update(tickDt, w, playersByDim[d]);
            });
        }
        remoteContent.update();   // finished downloads -> mods' callbacks
        updates.update(std::chrono::duration<double>(Clock::now() - serverStart).count());
        if (updates.takeNews()) {
            // said when it changes (not again every few hours when nothing's new)
            const std::string line = updates.summary();
            if (!line.empty() && line != lastUpdateLine) {
                // one line (the details: the "update" command)
                std::cout << "[Server] " << line << (updates.info().checked ? "" : " (" + updates.url() + ")") << std::endl;
                lastUpdateLine = line;
            }
        }

        // The time of day runs on the server, and goes to every player (every few seconds, and
        // at once when it jumps)
        const bool daylightCycle = ruleValue("doDaylightCycle") != "false";
        if (daylightCycle) dayClock.advance(tickDt);
        {
            DayClock sent = dayClock;
            if (!daylightCycle) sent.speed = 0.0;     // (players' games stop it too)
            server.setDayTime(sent);
        }

        for (const auto& line : server.takeChat()) std::cout << "[Chat] " << stripTags(line.text) << std::endl;

        // What the console's Tab completion offers: refreshed every second (mods: every 10 s, or
        // right after a 'mods' command, since that reads the mod folders)
        if (Clock::now() >= nextCompletionUpdate) {
            nextCompletionUpdate = Clock::now() + std::chrono::seconds(1);
            CompletionData d;
            for (const auto& c : ctx.modCommands) d.modCommands.push_back(c.name);
            d.players = server.playerNames();
            auto namesOf = [](const NameList& list) {
                std::vector<std::string> out;
                for (const auto& [key, v] : list.entries) out.push_back(v.substr(0, v.find('|')));
                return out;
            };
            d.banned = namesOf(banned);
            d.whitelisted = namesOf(whitelist);
            d.ops = namesOf(ops);
            d.bannedIps = namesOf(bannedIps);
            static std::vector<std::string> items;
            if (items.empty()) {
                for (int id = 1; id < 256; ++id) {
                    const std::string& n = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(id)).name;
                    if (!n.empty() && n != "air") items.push_back(n);
                }
            }
            d.items = items;
            for (const auto& [key, e] : accounts.entries) d.accounts.push_back(e.name);
            if (Clock::now() >= nextModListUpdate) {
                nextModListUpdate = Clock::now() + std::chrono::seconds(10);
                const auto saved = parseConfig(readConfFile(configFile)).disabledMods;
                completionModsOn.clear();
                completionModsOff.clear();
                for (const auto& m : listServerMods(config.game)) {
                    if (saved.count(m.id)) completionModsOff.push_back(m.id);
                    else if (!m.required) completionModsOn.push_back(m.id);
                }
            }
            d.modsOn = completionModsOn;
            d.modsOff = completionModsOff;
            std::lock_guard<std::mutex> lock(g_CompletionMutex);
            g_Completion = std::move(d);
        }

        std::deque<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(g_ConsoleMutex);
            lines.swap(g_ConsoleLines);
        }
        for (const std::string& line : lines) {
            std::string first = trim(line).substr(0, trim(line).find(' '));
            if (lower(first) == "mods") nextModListUpdate = Clock::now();   // (it may have changed them)
        }
        for (const std::string& line : lines) {
            if (!trim(line).empty()) handleCommand(trim(line), consoleReply, "");
        }
        ctx.api.flush();   // kicks asked for by mods this tick

        if (autosaveOn && Clock::now() - lastSave >= std::chrono::seconds(config.autosaveSeconds)) {
            const int n = saveAll();
            if (n > 0) std::cout << "[Server] Autosaved (" << n << " changed chunks)" << std::endl;
            lastSave = Clock::now();
        }

        ++ticksThisSecond;
        const auto now = Clock::now();
        slowestThisWindow = std::max(slowestThisWindow, std::chrono::duration<double, std::milli>(now - tickStart).count());
        if (now - tpsWindow >= std::chrono::seconds(1)) {
            tps = ticksThisSecond / std::chrono::duration<double>(now - tpsWindow).count();
            slowestTickMs = slowestThisWindow;
            slowestThisWindow = 0.0;
            ticksThisSecond = 0;
            tpsWindow = now;
        }

        nextTick += tick;
        if (now > nextTick + std::chrono::seconds(2)) {
            if (now - lastLagWarning > std::chrono::seconds(30)) {
                std::cout << "[Server] Can't keep up! Is the server overloaded?" << std::endl;
                lastLagWarning = now;
            }
            nextTick = now;
        }
        std::this_thread::sleep_until(nextTick);
    }

    std::cout << "[Server] Stopping..." << std::endl;
    ctx.api.serverStopping();   // on_server_stop(): mods save what they keep
    ctx.api.flush();
    world.finishFalling();      // sand / gravel still in the air lands first, so none is lost
    const int chunks = saveAll();
    server.stop("The server is shutting down");
    std::cout << "[Server] Saved " << chunks << " changed chunks. Bye!" << std::endl;
    g_Console.stop();   // the terminal back to normal
    return 0;
}
