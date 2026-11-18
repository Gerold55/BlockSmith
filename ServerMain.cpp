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
#include "ChunkData.h"
#include "CaveGenerator.h"
#include "CaveLua.h"
#include "Block.h"
#include "BlockDefs.h"
#include "Net.h"
#include "Sha256.h"
#include "ChunkGenerator.h"
#include "Fluids.h"
#include "ModOrder.h"
#include "SafeReplace.h"
#include "ClientModRules.h"
#include "Auth.h"

#include <sol/sol.hpp>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cctype>
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
    int maxPlayers = 20;
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
        << "\n"
        << "# --- Performance ---\n"
        << "# Updates per second (20-60). Higher = smoother, more CPU.\n"
        << "tick_rate = " << c.tickRate << "\n"
        << "# Threads that generate new terrain (0 = automatic: CPU cores - 1)\n"
        << "generator_threads = 0\n"
        << "autosave_seconds = " << c.autosaveSeconds << "\n";
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

bool loadPlayer(const fs::path& worldDir, const std::string& name, glm::vec3& pos) {
    std::ifstream in(playerFile(worldDir, name));
    std::string key;
    return (in >> key >> pos.x >> pos.y >> pos.z) && key == "pos";
}

void savePlayer(const fs::path& worldDir, const std::string& name, const glm::vec3& pos) {
    std::error_code ec;
    fs::create_directories(worldDir / "players", ec);
    std::ofstream out(playerFile(worldDir, name), std::ios::trunc);
    out << std::fixed << std::setprecision(3) << "pos " << pos.x << " " << pos.y << " " << pos.z << "\n";
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

void consoleThread() {
    std::string line;
    while (g_Running && std::getline(std::cin, line)) {
        std::lock_guard<std::mutex> lock(g_ConsoleMutex);
        g_ConsoleLines.push_back(line);
    }
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
for _, name in ipairs({ "ui", "hotbar", "sky", "clouds", "fog", "underwater", "menu", "sound" }) do
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
    World* world = nullptr;
    GameServer* server = nullptr;
    ServerConfig* config = nullptr;
    GameInfo game;
    std::vector<std::string> sharedChanged;   // for on_shared_changed
    struct ModCommand { std::string name, usage, description; };
    std::vector<ModCommand> modCommands;      // chat.register_command (listed by help)
};

// worker = true: a Lua state for a world generator thread. Same mods, but nothing in it
// may touch the world or the block list (the main thread owns those), and it stays quiet.
void setupLua(sol::state& lua, ServerContext& ctx, bool worker = false) {
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);
    if (worker) lua["print"] = [](sol::variadic_args) {};

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
            return ctx.world->getMetadataAt(glm::ivec3(x, y, z));
        };
        engine["set_block_meta"] = [&ctx](int x, int y, int z, const std::string& name, int meta) {
            ctx.world->setBlockWithMetaAt(glm::ivec3(x, y, z), BlockRegistry::getInstance().getBlockId(name),
                                          static_cast<uint8_t>(meta & 0xFF));
        };
        engine["is_replaceable"] = [&ctx](int x, int y, int z) {
            const uint8_t id = ctx.world->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0) return true;
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            return FluidRegistry::get().forBlock(id) != nullptr || prop.pattern == "plantlike";
        };
        engine["set_block"] = [&ctx](int x, int y, int z, const std::string& name) {
            ctx.world->setBlockAt(glm::ivec3(x, y, z), BlockRegistry::getInstance().getBlockId(name));
        };
        engine["get_block_name"] = [&ctx](int x, int y, int z) -> std::string {
            const uint8_t id = ctx.world->getBlockAt(glm::ivec3(x, y, z));
            return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name;
        };
    }
    engine["is_server"] = []() { return true; };
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
    if (!worker) addFluidLuaApi(lua, engine, [&ctx]() { return ctx.world; });

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
        return ctx.world && ctx.world->hasSkyAbove(glm::ivec3(x, y, z));
    };

    if (worker) {
        // Generator threads must not touch the network either (only the main thread may)
        engine["send_chat"] = [](sol::variadic_args) { return false; };
        chat["send"] = [](sol::variadic_args) {};
    }
    lua.script(kNoopLua);
}

void printUsage() {
    std::cout << "Usage: BlockSmithServer [options]\n"
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
    std::cout << "BlockSmith dedicated server (" << net::kVersionName << ")" << std::endl;

    // ---- Command line ----
    fs::path configFile = "server.conf";
    std::map<std::string, std::string> overrides;
    bool setup = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (a == "--help" || a == "-h") { printUsage(); return 0; }
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
    ctx.server = &server;

    const fs::path worldDir = findFolder("saves") / safeName(config.world);
    fs::create_directories(worldDir / "chunks", ec);
    WorldMeta meta = readWorldMeta(worldDir / "world.dat");
    if (meta.exists && !meta.game.empty() && meta.game != config.game) {
        std::cerr << "[Server] World '" << config.world << "' was made with the game '" << meta.game
                  << "', not '" << config.game << "'. Pick another world (--world) or game." << std::endl;
        return 1;
    }
    if (!meta.exists) meta.seed = seedFromText(config.seed);
    meta.game = config.game;
    writeWorldMeta(worldDir / "world.dat", meta);
    world.setSaveDirectory(worldDir.string());
    world.setSeed(meta.seed);
    std::cout << "[Server] " << (meta.exists ? "Opened" : "Created") << " world '" << config.world
              << "' (seed " << meta.seed << ")" << std::endl;

    // ---- Mods ----
    sol::state lua;
    setupLua(lua, ctx);
    int failed = 0;
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
        for (const auto& file : modFiles) {
            (*workerLua)["g_CurrentModPath"] = file.parent_path().string();
            try { workerLua->script_file(file.string()); } catch (const std::exception&) {}
        }
        return workerLua;
    }, meta.seed);
    world.setGenerator(&generator);
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
    whitelist.load();
    banned.load();
    ops.load();
    playerModes.load();
    server.setDefaultMode(config.gameMode);
    server.modeFor = [&](const std::string& name) { return playerModes.reason(name); };
    server.isOperator = [&](const std::string& name) { return ops.has(name); };
    accounts.load();

    auto spawnPoint = [&]() {
        const int ground = world.groundHeightAt(config.spawnX, config.spawnZ);
        return glm::vec3(config.spawnX + 0.5f, ground >= 0 ? static_cast<float>(ground) : 100.0f, config.spawnZ + 0.5f);
    };
    server.spawnFor = [&](const std::string& name, glm::vec3& pos) {
        if (loadPlayer(worldDir, name, pos)) return true;
        pos = spawnPoint();
        return true;
    };
    server.onLeave = [&worldDir](const std::string& name, const glm::vec3& pos) { savePlayer(worldDir, name, pos); };
    // A player broke or placed a block: on_block_broken(x, y, z, block, meta) /
    // on_block_placed(x, y, z, block) - the same hooks as in singleplayer, so mods like leaf
    // decay work on servers (the server is the one that decides)
    server.onPlayerEdit = [&lua](const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
        auto& reg = BlockRegistry::getInstance();
        if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
        const char* hookName = newId == 0 ? "on_block_broken" : "on_block_placed";
        sol::protected_function hook = lua[hookName];
        if (!hook.valid()) return;
        auto r = newId == 0 ? hook(pos.x, pos.y, pos.z, reg.getBlock(oldId).name, 0)
                            : hook(pos.x, pos.y, pos.z, reg.getBlock(newId).name);
        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] " << hookName << ": " << err.what() << std::endl; }
    };
    std::set<std::string> newAccounts;   // registered while joining (told about it once they're in)
    server.hasAccount = [&](const std::string& name) { return accounts.has(name); };
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
        if (newAccounts.erase(name)) {
            server.messagePlayer(name, "Your name is now protected by your password on this server.",
                                 glm::vec3(0.6f, 0.9f, 1.0f));
        } else if (!accounts.has(name)) {
            server.messagePlayer(name, "Tip: set a password in Settings > Player so nobody else can use your name here.",
                                 glm::vec3(0.6f, 0.9f, 1.0f));
        }
        if (ops.has(name)) server.messagePlayer(name, "You are an operator here: type /help for server commands.",
                                                glm::vec3(0.6f, 0.9f, 1.0f));
    };

    // ---- Network ----
    auto applyLiveSettings = [&]() {
        server.setInfo(config.motd, config.maxPlayers);
        server.setIcon(loadIcon(confDir / config.icon));
        server.setMaxViewDistance(config.viewDistance);
        server.setGameTitle(ctx.game.title);
        server.setDefaultMode(config.gameMode);
        server.setShared(ClientModRules::kSharedKey, config.clientMods.encode());   // players' client mods
    };
    server.setServerId(loadOrCreateServerId(confDir / "server_id.txt"));   // passwords are bound to it
    applyLiveSettings();
    std::string error;
    if (!server.start(world, static_cast<uint16_t>(config.port), config.game, config.name, error, false)) {
        std::cerr << "[Server] " << error << " (another server already running? use --port)" << std::endl;
        return 1;
    }
    applyLiveSettings();   // again: starting the server clears shared values
    std::cout << "[Server] '" << config.name << "' is listening on port " << config.port << " (max "
              << config.maxPlayers << " players" << (config.whitelist ? ", whitelist on" : "")
              << (config.requirePassword ? ", passwords required" : "") << "). Type 'help' for commands." << std::endl;
    if (ops.entries.empty()) {
        std::cout << "[Server] No operators yet: join, then type 'op <your name>' here to manage the server "
                  << "from inside the game" << std::endl;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::thread(consoleThread).detach();

    auto saveAll = [&]() {
        for (const auto& [name, pos] : server.playerPositions()) savePlayer(worldDir, name, pos);
        const int chunks = world.saveAll();
        writeWorldMeta(worldDir / "world.dat", meta);
        return chunks;
    };

    double tps = config.tickRate;
    double slowestTickMs = 0.0, slowestThisWindow = 0.0;   // for 'tps'

    // ---- Commands: from the console (everything), or typed in the game as /command
    // (operators: everything except the console-only ones; everyone: help, list, msg) ----
    using Reply = std::function<void(const std::string&)>;
    auto handleCommand = [&](const std::string& line, const Reply& reply, const std::string& from) {
        const bool console = from.empty();
        const bool op = console || ops.has(from);
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
        static const std::set<std::string> builtIn = {"help", "list", "msg", "stop", "save", "reload", "tps", "seed",
            "games", "mods", "motd", "name", "say", "kick", "ban", "unban", "banlist", "whitelist", "op", "deop",
            "ops", "password", "respawn", "gamemode", "gm"};
        if (!builtIn.count(cmd)) {
            sol::protected_function fn = lua["on_chat_command"];
            if (fn.valid()) {
                std::function<void(const std::string&)> luaReply = reply;
                auto r = fn(line, console ? std::string("(console)") : from, op, luaReply);
                if (!r.valid()) {
                    sol::error err = r;
                    std::cerr << "[Lua] on_chat_command: " << err.what() << std::endl;
                } else if (r.get_type() == sol::type::boolean && r.get<bool>()) {
                    return;   // a mod handled it
                }
            }
        }
        // /respawn: back to the spawn point (sent by the game after dying when joined)
        if (cmd == "respawn" && !console) {
            const int sx = config.spawnX, sz = config.spawnZ;
            int ground = world.groundHeightAt(sx, sz);
            if (ground < 0) ground = 80;
            server.teleportPlayer(from, glm::vec3(sx + 0.5f, static_cast<float>(ground), sz + 0.5f));
            return;
        }
        // /gamemode on its own: anyone may ask which mode they're in
        if ((cmd == "gamemode" || cmd == "gm") && rest.empty() && !console) {
            const std::string mode = server.playerMode(from);
            reply("Your game mode: " + (mode.empty() ? config.gameMode : mode) +
                  (op ? ". Change it with /gamemode survival|creative [name]" : ". Only operators can change it."));
            return;
        }
        const std::set<std::string> everyone = {"help", "list", "msg"};
        if (!op && !everyone.count(cmd)) {
            reply("You need to be an operator to use /" + cmd + ". Commands: /help, /list, /msg");
            return;
        }
        const std::string by = console ? "" : " (by " + from + ")";

        if (cmd == "help") {
            if (!op) { reply("Commands: /list, /msg <name> <message>"); return; }
            reply("Server:  stop, save, reload, tps, seed, games, mods [enable|disable <id>]");
            reply("Look:    motd [text], name [text]");
            reply("Players: list, say, msg, kick, ban, unban, banlist, whitelist on|off|add|remove|list");
            reply("Admins:  op <name>, deop <name>, ops, password reset <name>");
            reply("Modes:   gamemode survival|creative [name]   (default for everyone: gamemode in server.conf)");
            for (const auto& c : ctx.modCommands) reply("Mods:    " + c.usage + (c.description.empty() ? "" : " - " + c.description));
        } else if (cmd == "stop") {
            reply("Stopping the server...");
            std::cout << "[Server] Stop" << by << std::endl;
            g_Running = false;
        } else if (cmd == "save") {
            reply("Saved (" + std::to_string(saveAll()) + " changed chunks)");
        } else if (cmd == "tps") {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "Ticks per second: %.1f (target %d), slowest tick %.0f ms",
                          tps, config.tickRate, slowestTickMs);
            reply(buf);
            std::snprintf(buf, sizeof(buf), "World generation: %d threads, %.1f chunks/s, %zu waiting, %zu chunks loaded",
                          generator.threadCount(), generator.chunksPerSecond(), world.generatingCount(),
                          world.loadedChunkCount());
            reply(buf);
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
            server.broadcastMessage("[" + who + "] " + rest, glm::vec3(1.0f, 0.4f, 1.0f));
            std::cout << "[Chat] [" << who << "] " << rest << std::endl;
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
            reply("Banned: " + banned.names());
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
            if (!net::validPlayerName(rest)) { reply("Usage: op <name>"); return; }
            ops.add(rest);
            server.setPlayerOperator(rest, true);
            reply(rest + " is now an operator");
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
        } else if (cmd == "ops") {
            reply("Operators: " + ops.names());
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

    // ---- Main loop: fixed ticks, sleeping in between ----
    const auto tick = std::chrono::microseconds(1000000 / config.tickRate);
    const float tickDt = 1.0f / static_cast<float>(config.tickRate);
    auto nextTick = Clock::now();
    auto lastSave = Clock::now();
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

        // Lua every tick: shared values that changed, then on_update (weather etc.)
        if (!ctx.sharedChanged.empty()) {
            std::vector<std::string> changed;
            changed.swap(ctx.sharedChanged);
            sol::protected_function onShared = lua["on_shared_changed"];
            for (const auto& k : changed) {
                if (!onShared.valid()) break;
                const auto it = server.shared().find(k);
                auto r = onShared(k, it == server.shared().end() ? std::string() : it->second);
                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_shared_changed: " << err.what() << std::endl; }
            }
        }
        {
            sol::protected_function onUpdate = lua["on_update"];
            if (onUpdate.valid()) {
                auto r = onUpdate(tickDt);
                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_update: " << err.what() << std::endl; }
            }
        }

        for (const auto& line : server.takeChat()) std::cout << "[Chat] " << stripTags(line.text) << std::endl;

        std::deque<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(g_ConsoleMutex);
            lines.swap(g_ConsoleLines);
        }
        for (const std::string& line : lines) {
            if (!trim(line).empty()) handleCommand(trim(line), consoleReply, "");
        }

        if (Clock::now() - lastSave >= std::chrono::seconds(config.autosaveSeconds)) {
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
    const int chunks = saveAll();
    server.stop("The server is shutting down");
    std::cout << "[Server] Saved " << chunks << " changed chunks. Bye!" << std::endl;
    return 0;
}
