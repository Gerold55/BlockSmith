// CraftRuinServer - dedicated (headless) multiplayer server for any game made with the engine.
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
//   CraftRuinServer --game skyblock --world sky --port 30001
//   CraftRuinServer --config survival.conf
//   CraftRuinServer --list-games

#include "World.h"
#include "ChunkData.h"
#include "CaveGenerator.h"
#include "Block.h"
#include "BlockDefs.h"
#include "Net.h"

#include <sol/sol.hpp>
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
    std::string name = "Craft & Ruin Server";
    std::string motd = "{gold}{b}A Craft & Ruin server{/b}{n}{gray}Running on a dedicated server";
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
    int tickRate = 30;
    int autosaveSeconds = 60;
};

void writeDefaultConfig(const fs::path& file, const ServerConfig& c) {
    std::ofstream out(file);
    out << "# Craft & Ruin dedicated server settings.\n"
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
        << "\n"
        << "# --- Performance ---\n"
        << "# Updates per second (20-60). Higher = smoother, more CPU.\n"
        << "tick_rate = " << c.tickRate << "\n"
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
    c.tickRate = std::clamp(toInt(kv["tick_rate"], c.tickRate), 10, 120);
    c.autosaveSeconds = std::clamp(toInt(kv["autosave_seconds"], c.autosaveSeconds), 10, 3600);
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
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    return !ec;
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

std::vector<fs::path> findModFiles(const std::string& game) {
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

        const bool isGlobal = searchPath.rfind("games/", 0) != 0;
        const auto settings = isGlobal ? readConfFile(fs::path(resolved) / "mod_settings.txt")
                                       : std::map<std::string, std::string>{};
        std::set<std::string> reported;

        for (const auto& entry : fs::recursive_directory_iterator(resolved)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
            if (isGlobal) {
                std::error_code relEc;
                const fs::path rel = fs::relative(entry.path(), resolved, relEc);
                const std::string id = (relEc || rel.empty()) ? "" : rel.begin()->string();
                if (!id.empty()) {
                    const auto it = settings.find(id);
                    const bool off = it != settings.end() && it->second == "false" &&
                                     !modIsRequired(fs::path(resolved) / id, id);
                    if (off) {
                        if (reported.insert(id).second) std::cout << "[Mod] Skipping disabled mod: " << id << std::endl;
                        continue;
                    }
                }
            }
            if (entry.path().filename() == "init.lua") initFolders.insert(entry.path().parent_path().string());
            modFiles.push_back(entry.path());
        }
    }

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
for _, name in ipairs({ "ui", "hotbar", "sky", "clouds", "fog", "underwater", "menu" }) do
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
};

void setupLua(sol::state& lua, ServerContext& ctx) {
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);

    lua.new_usertype<ChunkData>("Chunk",
        "set_block", &ChunkData::setBlock, "setBlock", &ChunkData::setBlock,
        "get_block", &ChunkData::getLocalBlock, "getBlock", &ChunkData::getLocalBlock,
        "set_block_with_meta", &ChunkData::setBlockWithMeta,
        "set_metadata", &ChunkData::setMetadata, "get_metadata", &ChunkData::getLocalMetadata
    );

    auto engine = lua.create_named_table("engine");
    engine["register_block"] = [](const std::string& name, float r, float g, float b, sol::object tex,
                                  sol::object overlay, sol::optional<sol::table> props) {
        registerBlockDefinition(name, r, g, b, tex, overlay, props, [](const std::string&) { return -1; });
    };
    engine["get_block_id"] = [](const std::string& name) { return BlockRegistry::getInstance().getBlockId(name); };
    engine["set_block"] = [&ctx](int x, int y, int z, const std::string& name) {
        ctx.world->setBlockAt(glm::ivec3(x, y, z), BlockRegistry::getInstance().getBlockId(name));
    };
    engine["get_block_name"] = [&ctx](int x, int y, int z) -> std::string {
        const uint8_t id = ctx.world->getBlockAt(glm::ivec3(x, y, z));
        return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name;
    };
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

    auto chat = lua.create_named_table("chat");
    chat["register_command"] = [](const std::string&, sol::optional<std::string>, sol::optional<std::string>) {};
    chat["send"] = [&ctx](const std::string& text, sol::optional<float> r, sol::optional<float> g,
                          sol::optional<float> b) {
        ctx.server->broadcastMessage(text, glm::vec3(r.value_or(1.0f), g.value_or(1.0f), b.value_or(0.6f)));
    };

    lua.script(kNoopLua);
}

void printUsage() {
    std::cout << "Usage: CraftRuinServer [options]\n"
              << "  --config <file>   settings file (default server.conf, created if missing)\n"
              << "  --game <folder>   game to run (overrides 'game' in the settings)\n"
              << "  --world <name>    world folder in saves/ (overrides 'world')\n"
              << "  --port <number>   port (overrides 'port')\n"
              << "  --list-games      show installed games and exit\n";
}

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv) {
    std::cout << "Craft & Ruin dedicated server (" << net::kVersionName << ")" << std::endl;

    // ---- Command line ----
    fs::path configFile = "server.conf";
    std::map<std::string, std::string> overrides;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (a == "--help" || a == "-h") { printUsage(); return 0; }
        else if (a == "--list-games") { printGames(); return 0; }
        else if (a == "--config") configFile = next();
        else if (a == "--game") overrides["game"] = next();
        else if (a == "--world") overrides["world"] = next();
        else if (a == "--port") overrides["port"] = next();
        else if (i == 1 && a.rfind("--", 0) != 0) configFile = a;   // old style: CraftRuinServer my.conf
        else { std::cerr << "Unknown option '" << a << "'\n"; printUsage(); return 1; }
    }

    std::error_code ec;
    if (!fs::exists(configFile, ec)) {
        writeDefaultConfig(configFile, ServerConfig{});
        std::cout << "[Server] Created " << configFile.string() << " with default settings - edit it to "
                  << "set the name, description, icon and game" << std::endl;
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
    for (const auto& file : findModFiles(config.game)) {
        lua["g_CurrentModPath"] = file.parent_path().string();
        try {
            lua.script_file(file.string());
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
        }
    }
    if (failed) std::cerr << "[Mod] " << failed << " mod file(s) failed to load (see above)" << std::endl;

    // ---- Spawn area ----
    const glm::vec3 spawnCenter(config.spawnX + 0.5f, 80.0f, config.spawnZ + 0.5f);
    world.setRenderDistance(3);
    world.setLoadBudgetMs(50.0f);
    const auto loadStart = Clock::now();
    while (world.areaLoadProgress(spawnCenter, 2) < 1.0f && Clock::now() - loadStart < std::chrono::seconds(60)) {
        world.update(spawnCenter, lua);
    }
    world.setLoadBudgetMs(1000.0f / config.tickRate * 0.5f);
    std::cout << "[Server] Spawn ready at " << config.spawnX << ", " << config.spawnZ << std::endl;

    // ---- Players: whitelist, bans, positions, welcome ----
    NameList whitelist{confDir / "whitelist.txt", {}};
    NameList banned{confDir / "banned.txt", {}};
    whitelist.load();
    banned.load();

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
    server.canJoin = [&](const std::string& name) -> std::string {
        if (banned.has(name)) {
            const std::string why = banned.reason(name);
            return "You are banned from this server" + (why.empty() ? std::string() : ": " + why);
        }
        if (config.whitelist && !whitelist.has(name)) return "You are not on this server's whitelist";
        return "";
    };
    server.onJoin = [&](const std::string& name) {
        if (config.welcome.empty()) return;
        std::string msg = config.welcome;
        for (size_t p; (p = msg.find("{player}")) != std::string::npos;) msg.replace(p, 8, name);
        server.messagePlayer(name, stripTags(msg), glm::vec3(1.0f, 0.85f, 0.3f));
    };

    // ---- Network ----
    auto applyLiveSettings = [&]() {
        server.setInfo(config.motd, config.maxPlayers);
        server.setIcon(loadIcon(confDir / config.icon));
        server.setMaxViewDistance(config.viewDistance);
        server.setGameTitle(ctx.game.title);
    };
    applyLiveSettings();
    std::string error;
    if (!server.start(world, static_cast<uint16_t>(config.port), config.game, config.name, error, false)) {
        std::cerr << "[Server] " << error << " (another server already running? use --port)" << std::endl;
        return 1;
    }
    std::cout << "[Server] '" << config.name << "' is listening on port " << config.port << " (max "
              << config.maxPlayers << " players" << (config.whitelist ? ", whitelist on" : "")
              << "). Type 'help' for commands." << std::endl;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::thread(consoleThread).detach();

    auto saveAll = [&]() {
        for (const auto& [name, pos] : server.playerPositions()) savePlayer(worldDir, name, pos);
        const int chunks = world.saveAll();
        writeWorldMeta(worldDir / "world.dat", meta);
        return chunks;
    };

    // ---- Console commands ----
    auto handleCommand = [&](const std::string& line) {
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

        if (cmd == "help") {
            std::cout << "Server:   stop, save, reload, tps, seed, games\n"
                      << "Look:     motd [text], name [text]\n"
                      << "Players:  list, say <message>, msg <name> <message>, kick <name> [reason]\n"
                      << "          ban <name> [reason], unban <name>, banlist\n"
                      << "          whitelist on|off|add <name>|remove <name>|list" << std::endl;
        } else if (cmd == "stop") {
            g_Running = false;
        } else if (cmd == "save") {
            std::cout << "[Server] Saved (" << saveAll() << " changed chunks)" << std::endl;
        } else if (cmd == "reload") {
            const ServerConfig fresh = readSettings();
            if (fresh.game != config.game || fresh.world != config.world || fresh.port != config.port) {
                std::cout << "[Server] game, world and port changes need a restart" << std::endl;
            }
            const std::string game = config.game, worldName = config.world;
            const int port = config.port;
            config = fresh;
            config.game = game;
            config.world = worldName;
            config.port = port;
            whitelist.load();
            banned.load();
            applyLiveSettings();
            std::cout << "[Server] Reloaded settings, whitelist and bans" << std::endl;
        } else if (cmd == "motd") {
            if (rest.empty()) {
                std::cout << "Description: " << config.motd << "\n(set it with: motd <text>, using {gold}, {b}, {n}...)" << std::endl;
            } else {
                config.motd = rest;
                server.setInfo(config.motd, config.maxPlayers);
                updateConfValue(configFile, "motd", rest);
                std::cout << "[Server] Description changed (and saved)" << std::endl;
            }
        } else if (cmd == "name") {
            if (rest.empty()) std::cout << "Name: " << config.name << std::endl;
            else {
                config.name = rest;
                updateConfValue(configFile, "name", rest);
                std::cout << "[Server] Name saved - shown on the LAN list after a restart" << std::endl;
            }
        } else if (cmd == "games") {
            printGames();
            std::cout << "Running: " << ctx.game.folder << std::endl;
        } else if (cmd == "list") {
            const auto names = server.playerNames();
            std::string all;
            for (const auto& n : names) all += (all.empty() ? "" : ", ") + n;
            std::cout << names.size() << "/" << config.maxPlayers << " players online"
                      << (names.empty() ? "" : ": " + all) << std::endl;
        } else if (cmd == "say") {
            server.broadcastMessage("[Server] " + rest, glm::vec3(1.0f, 0.4f, 1.0f));
            std::cout << "[Chat] [Server] " << rest << std::endl;
        } else if (cmd == "msg") {
            std::string text;
            const std::string name = firstWord(rest, text);
            std::cout << (server.messagePlayer(name, "[Server -> you] " + text, glm::vec3(1.0f, 0.4f, 1.0f))
                          ? "[Server] Sent" : "[Server] No player called '" + name + "' online") << std::endl;
        } else if (cmd == "kick") {
            std::string reason;
            const std::string name = firstWord(rest, reason);
            std::cout << (server.kickPlayer(name, reason) ? "[Server] Kicked " + name
                                                          : "[Server] No player called '" + name + "' online") << std::endl;
        } else if (cmd == "ban") {
            std::string reason;
            const std::string name = firstWord(rest, reason);
            if (!net::validPlayerName(name)) { std::cout << "Usage: ban <name> [reason]" << std::endl; return; }
            banned.add(name, reason);
            server.kickPlayer(name, "You are banned from this server" + (reason.empty() ? "" : ": " + reason));
            std::cout << "[Server] Banned " << name << std::endl;
        } else if (cmd == "unban" || cmd == "pardon") {
            std::cout << (banned.remove(rest) ? "[Server] Unbanned " + rest : "[Server] '" + rest + "' wasn't banned") << std::endl;
        } else if (cmd == "banlist") {
            std::cout << "Banned: " << banned.names() << std::endl;
        } else if (cmd == "whitelist") {
            std::string arg;
            const std::string sub = lower(firstWord(rest, arg));
            if (sub == "on" || sub == "off") {
                config.whitelist = (sub == "on");
                updateConfValue(configFile, "whitelist", config.whitelist ? "true" : "false");
                std::cout << "[Server] Whitelist " << sub << std::endl;
            } else if (sub == "add" && net::validPlayerName(arg)) {
                whitelist.add(arg);
                std::cout << "[Server] Added " << arg << " to the whitelist" << std::endl;
            } else if (sub == "remove") {
                std::cout << (whitelist.remove(arg) ? "[Server] Removed " + arg : "[Server] '" + arg + "' isn't on it") << std::endl;
            } else if (sub == "list") {
                std::cout << "Whitelist (" << (config.whitelist ? "on" : "off") << "): " << whitelist.names() << std::endl;
            } else {
                std::cout << "Usage: whitelist on|off|add <name>|remove <name>|list" << std::endl;
            }
        } else if (cmd == "seed") {
            std::cout << "Seed: " << meta.seed << std::endl;
        } else {
            std::cout << "Unknown command '" << cmd << "'. Type 'help'." << std::endl;
        }
    };

    // ---- Main loop: fixed ticks, sleeping in between ----
    const auto tick = std::chrono::microseconds(1000000 / config.tickRate);
    const float tickDt = 1.0f / static_cast<float>(config.tickRate);
    auto nextTick = Clock::now();
    auto lastSave = Clock::now();
    auto lastLagWarning = Clock::now() - std::chrono::seconds(60);
    float fluidTimer = 0.0f;
    int ticksThisSecond = 0;
    double tps = config.tickRate;
    auto tpsWindow = Clock::now();

    while (g_Running) {
        world.update(spawnCenter, lua);   // spawn + every player's surroundings stay loaded
        server.update(spawnCenter, 0.0f, 0.0f, tickDt);
        fluidTimer += tickDt;
        if (fluidTimer >= 0.15f) { world.updateFluids(); fluidTimer = 0.0f; }

        for (const auto& line : server.takeChat()) std::cout << "[Chat] " << stripTags(line.text) << std::endl;

        std::deque<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(g_ConsoleMutex);
            lines.swap(g_ConsoleLines);
        }
        for (const std::string& line : lines) {
            if (trim(line) == "tps") {
                std::cout << "Ticks per second: " << std::fixed << std::setprecision(1) << tps
                          << " (target " << config.tickRate << ")" << std::endl;
            } else if (!trim(line).empty()) {
                handleCommand(line);
            }
        }

        if (Clock::now() - lastSave >= std::chrono::seconds(config.autosaveSeconds)) {
            const int n = saveAll();
            if (n > 0) std::cout << "[Server] Autosaved (" << n << " changed chunks)" << std::endl;
            lastSave = Clock::now();
        }

        ++ticksThisSecond;
        const auto now = Clock::now();
        if (now - tpsWindow >= std::chrono::seconds(1)) {
            tps = ticksThisSecond / std::chrono::duration<double>(now - tpsWindow).count();
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
