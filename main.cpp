#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Use the best graphics card. On PCs and laptops with both a built-in (integrated) GPU and
// a dedicated NVIDIA/AMD one, Windows starts programs on the built-in one unless the
// program asks otherwise. The NVIDIA and AMD drivers look for these two values in the
// game's .exe and switch to the dedicated GPU when they find them. (They must be in the
// .exe itself, not a DLL - which is why they're here in main.cpp.)
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <exception>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include "ModOrder.h"
#include "SafeReplace.h"
#include "ClientMods.h"
#include "Auth.h"
#include <cstdlib>
#include <iomanip>
#include <random>

#include "Types.h"
#include "Block.h"
#include "ChunkData.h"
#include "WorldRenderer.h"
#include "Net.h"
#include "BlockDefs.h"
#include "PlayerMovement.h"
#include "PlayerAvatars.h"
#include "World.h"
#include "Player.h"
#include "Raycast.h"
#include "DoorShape.h"
#include "FirstPerson.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include "ChatSystem.h"
#include "Lighting.h"
#include "Skybox.h"
#include "Light.h"
#include "ChunkMesh.h"
#include "Model.h"
#include "Clouds.h"
#include "Fog.h"
#include "MainMenu.h"
#include "Inventory.h" // Include your new dedicated Inventory module
#include "CaveGenerator.h"
#include "CaveLua.h"
#include "ChunkGenerator.h"
#include "Fluids.h"
#include "WeatherEffects.h"
#include "Audio.h"
#include "Firewall.h"
#include "Settings.h"
#include "ParticleSystem.h"

namespace fs = std::filesystem;

namespace {
    constexpr Dimensions2D baseResolution{1920, 1080};

    void scaleCursorToFramebuffer(GLFWwindow* window, double& x, double& y,
                                  int framebufferWidth, int framebufferHeight) {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        if (windowWidth == 0 || windowHeight == 0) return;

        x *= static_cast<double>(framebufferWidth) / windowWidth;
        y *= static_cast<double>(framebufferHeight) / windowHeight;
    }
}

// Underwater look. Clear, Minecraft-like water by default: you can see a good distance,
// with a light blue-green fog that only gets dark deep down. Games change these with
// underwater.set_shallow_color / set_deep_color / set_visibility / set_tint_strength.
struct UnderwaterSettings {
    glm::vec3 shallowColor      = glm::vec3(0.22f, 0.45f, 0.62f);   // soft blue-green, not deep blue
    glm::vec3 deepColor         = glm::vec3(0.05f, 0.14f, 0.28f);
    float       shallowVisibility = 48.0f;   // blocks until things fade fully into the fog
    float       deepVisibility    = 20.0f;
    float       fullDepth         = 48.0f;   // depth where it's fully "deep"
    float       fadeInSeconds     = 0.6f;
    float       fadeOutSeconds    = 0.4f;
    float       tintStrength      = 0.15f;   // how much everything is coloured blue
    float       fogStartFraction  = 0.2f;    // fog starts this far (of the visibility) away
    // Colours set from Lua (underwater.set_shallow_color / set_deep_color). If not, they come
    // from the water block itself (its texture and tint), so being underwater looks like the water.
    bool        colorsFromLua     = false;
};

std::string g_CurrentModPath = "";
std::string g_ActiveGameFolder = "craft_and_ruin";
std::string g_ActiveWorldName = "world_alpha";
std::string g_LoadedGameFolder;   // the game whose mods were loaded at startup (worlds are tied to it)
std::map<std::string, std::string> g_ModFileStatus;   // mod .lua file (canonical path) -> "" if it ran OK, else the error
Hotbar* g_Hotbar = nullptr;
FontRenderer* g_FontRenderer = nullptr;
ChatSystem* g_ChatSystem = nullptr;
LightingSystem* g_LightingSystem = nullptr;
Skybox* g_Skybox = nullptr;
Clouds* g_Clouds = nullptr;
WeatherEffects g_Weather;   // rain/snow/sky tint/flashes - decided in Lua (mods/weather)
AudioSystem g_Audio;        // OpenAL: block sounds, Lua's sound.*, music

// A block's sound (footstep, dig, dug, place - see BlockProperty), in the world at `pos`.
// Falls back like Luanti: no dig sound -> footstep; no dug sound -> dig, then footstep.
void playBlockSound(uint8_t id, const char* kind, const glm::vec3& pos, float gainScale = 1.0f) {
    if (id == 0 || !g_Audio.available()) return;
    const BlockProperty& b = BlockRegistry::getInstance().getBlock(id);
    const BlockProperty::SoundSpec* spec = nullptr;
    const std::string k = kind;
    auto pick = [&spec](const BlockProperty::SoundSpec& s) { if (!spec && !s.name.empty()) spec = &s; };
    if (k == "footstep") pick(b.soundFootstep);
    else if (k == "dig") { pick(b.soundDig); pick(b.soundFootstep); }
    else if (k == "dug") { pick(b.soundDug); pick(b.soundDig); pick(b.soundFootstep); }
    else if (k == "place") { pick(b.soundPlace); pick(b.soundFootstep); }
    if (!spec) return;
    SoundParams p;
    p.positional = true;
    p.position = pos;
    p.gain = spec->gain * gainScale;
    p.pitch = spec->pitch * (k == "dig" ? 0.9f + 0.2f * (std::rand() / static_cast<float>(RAND_MAX)) : 1.0f);
    p.maxDistance = k == "footstep" ? 16.0f : 24.0f;
    g_Audio.play(spec->name, p);
}
glm::vec3 g_PlayerPosition(0.0f);   // feet, updated every frame (engine.get_player_position)
float g_FootstepDistance = 1.3f;    // blocks walked between footstep sounds (engine.set_footstep_distance)

// Survival / creative (engine.set_creative). Creative: blocks break at once and placing is
// free. Survival: hold to dig (block hardness and tools), placing uses up the stack.
bool g_Creative = true;
struct DigState {
    bool active = false;
    glm::ivec3 pos{0};
    float progress = 0.0f;   // 0..1
    float seconds = 1.0f;    // how long this block takes with the held item
    float cooldown = 0.0f;   // short pause after each block, like Minecraft
    float particleTimer = 0.0f;
} g_Dig;
// Crack picture for survival digging (engine.set_dig_crack_texture): a vertical strip of
// `stages` square pictures, first stage at the top. 0 = no picture: a darkening box instead.
GLuint g_CrackTexture = 0;
int g_CrackStages = 10;
// A mod's own inventory screen (survival): open/closed (toggle_main_inventory decides)
bool g_LuaInventoryOpen = false;
sol::state* g_Lua = nullptr;   // the game's Lua, for engine code that needs to call a hook

// Mod storage (engine.get_storage / set_storage): text values saved with the world,
// in saves/<world>/mod_storage.txt, so mods remember health, hunger, game mode...
std::unordered_map<std::string, std::string> g_ModStorage;
bool g_ModStorageDirty = false;
uint8_t g_LastMoveKeys = 0;   // movement keys held this frame (after engine.set_player_controls)

// Puts items in the hotbar: onto a stack of the same item, else the first empty slot.
void hotbarScreenRect(const Hotbar& hb, float W, float H, float& x, float& y, float& w, float& h);   // Hotbar.cpp
void hotbarSlotRect(const Hotbar& hb, int i, float W, float H, float& x, float& y, float& size);       // Hotbar.cpp

// The player's client mods (clientmods/ folder, see ClientMods.h). Lives next to the game's Lua
// state in main(); null before it's made and after it's gone.
ClientMods* g_ClientMods = nullptr;
bool g_FlyAllowed = true;   // creative players may fly (engine.set_fly_allowed)

// Pick block: puts the block at `pos` in the player's hand - or the item its `pick` property
// names (crops pick their seeds). A mod's on_pick_block(x, y, z, item, meta, block) goes first
// (return true = handled). Then, in creative only: select the item if it's in the hotbar;
// otherwise put it in the selected slot if that's empty, else in the next empty slot (and select
// that), else replace what's in the selected slot - Minecraft's rules.
void pickBlock(sol::state& lua, World& world, const glm::ivec3& pos) {
    if (!g_Hotbar) return;
    const uint8_t blockId = world.getBlockAt(pos);
    if (blockId == 0) return;
    auto& reg = BlockRegistry::getInstance();
    const BlockProperty& block = reg.getBlock(blockId);
    const std::string item = block.pick.empty() ? block.name : block.pick;
    const uint8_t id = reg.getBlockId(item);
    if (id == 0) return;                      // `pick` names something that doesn't exist

    sol::protected_function hook = lua["on_pick_block"];
    if (hook.valid()) {
        auto r = hook(pos.x, pos.y, pos.z, item, static_cast<int>(world.getMetadataAt(pos)), block.name);
        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_pick_block: " << err.what() << std::endl; }
        else if (r.get_type() == sol::type::boolean && r.get<bool>()) return;
    }
    if (!g_Creative) return;

    const int n = g_Hotbar->slotCount;
    for (int i = 0; i < n; ++i) {
        if (g_Hotbar->getSlot(i).blockId == id) { g_Hotbar->activeSlot = i; return; }
    }
    int slot = g_Hotbar->activeSlot;
    if (g_Hotbar->getSlot(slot).blockId != 0) {
        for (int k = 1; k < n; ++k) {
            const int i = (g_Hotbar->activeSlot + k) % n;
            if (g_Hotbar->getSlot(i).blockId == 0) { slot = i; break; }
        }
    }
    g_Hotbar->setSlot(slot, id, 64);
    g_Hotbar->activeSlot = slot;
}

bool giveHotbarItem(const std::string& name, int count) {
    if (!g_Hotbar) return false;
    const uint8_t id = BlockRegistry::getInstance().getBlockId(name);
    if (id == 0) return false;
    const int n = std::max(1, count);
    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
        HotbarSlot s = g_Hotbar->getSlot(i);
        if (s.blockId == id && s.count + n <= 64) { g_Hotbar->setSlot(i, id, s.count + n); return true; }
    }
    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
        if (g_Hotbar->getSlot(i).blockId == 0) { g_Hotbar->setSlot(i, id, n); return true; }
    }
    // Hotbar full: a mod's inventory may take it - on_hotbar_full(name, count) -> true if stored
    if (g_Lua) {
        sol::protected_function hook = (*g_Lua)["on_hotbar_full"];
        if (hook.valid()) {
            auto r = hook(name, n);
            if (r.valid() && r.get_type() == sol::type::boolean && r.get<bool>()) return true;
        }
    }
    return false;
}

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
}   // rain/snow/sky tint/flashes - decided in Lua (mods/weather)
// Shared values (engine.set_shared): decided by whoever runs the world (singleplayer, the
// host, or the server) and seen by every player. Here: this game's own copy.
std::unordered_map<std::string, std::string> g_SharedValues;
std::vector<std::string> g_SharedChanged;   // keys to report to on_shared_changed
World* g_WorldInstance = nullptr;
float g_PlaceRepeatSeconds = 0.2f;   // holding right-click places again this often (0 = off)
FirstPersonView g_FirstPerson;        // the hand / held block in first person (FirstPerson.h)

// ---- Block light (Light.h) settings, from Lua (sky.set_min_light etc.) ----
struct LightSettings {
    float minLight = 0.02f;          // the darkest anything gets (a sealed cave)
    float brightness = 0.35f;        // 0 = moody (Minecraft's default), 1 = bright
    float blockLightScale = 1.0f;    // strength of torches and other light sources
    bool heldLight = true;           // holding a light source lights up around you
};
LightSettings g_LightSettings;

// The light of the item in the player's hand (a torch lights the cave as you walk), as one
// of LightingSystem's moving point lights
struct HeldLight {
    int id = -1;
    void update(const World& w);
};
HeldLight g_HeldLight;
WorldRenderer* g_WorldRenderer = nullptr;   // client-side drawing of g_WorldInstance

// Multiplayer: the server runs inside this game while hosting; the client while joined.
GameServer g_Server;
GameClient g_Client;
ServerPinger g_Pinger;    // server list: online? description? players?
LanScanner g_Lan;         // games hosted on the local network (Multiplayer screen)
std::string g_NetError;
// Why the last world couldn't be opened or was closed (world_select.lua shows it: engine.take_world_error)
std::string g_WorldError;
// LOADING timed out but the chunk under the player isn't loaded (e.g. its save file can't be
// read yet): the loading screen says so and offers Esc = back to the menu, while it keeps trying
bool g_SpawnChunkWaiting = false;
bool g_ShowDebugInfo = false;   // F3: FPS, ping and multiplayer info in the top-left corner
std::string g_GpuName;          // graphics card in use (shown on F3)
float g_Fps = 0.0f;       // frames per second, updated twice a second (shown by /netstats)   // why the last connection failed/ended (shown on the Multiplayer screen)
MainMenu* g_MainMenu = nullptr;
InventorySystem g_Inventory; // Dedicated C++ inventory system instance
float g_FieldOfView = 70.0f;
FogSettings g_Fog;
UnderwaterSettings g_Underwater;
float g_UnderwaterBlend = 0.0f;
bool g_Paused = false;   // pause menu open (drawn by pause_menu.lua)

// Loading screen state
bool g_SpawnPlaced = false;
double g_LoadStartTime = 0.0;
float g_LoadingProgress = 0.0f;
constexpr int kSpawnReadyRadius = 2;        // chunks around the spawn that must be ready
constexpr double kLoadTimeoutSec = 30.0;    // start anyway after this long

struct WaterColors {
    glm::vec3 shallow;
    glm::vec3 deep;
};

enum class GameState {
    MENU,
    WORLD_SELECT,   // singleplayer world list (drawn by world_select.lua)
    MODS,           // installed mods list (drawn by mods_menu.lua)
    TEXTURE_PACKS,  // texture pack picker (drawn by texture_packs.lua)
    SETTINGS,       // settings from the title screen (drawn by settings_menu.lua)
    MULTIPLAYER,    // server list / joining (drawn by multiplayer.lua)
    LOADING,
    PLAYING
};
GameState g_CurrentGameState = GameState::MENU;

struct GameInfo {
    std::string folderName;
    std::string title = "Unnamed Game";
    std::string author = "Unknown";
    std::string description = "";
};

GameInfo parseGameConfig(const fs::path& gameDirPath) {
    GameInfo info;
    info.folderName = gameDirPath.filename().string();

    fs::path confPath = gameDirPath / "game.conf";
    if (!fs::exists(confPath)) {
        confPath = gameDirPath / "mod.conf";
    }
    if (!fs::exists(confPath)) {
        info.title = info.folderName;
        return info;
    }

    std::ifstream file(confPath);
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        size_t delimiter = line.find('=');
        if (delimiter != std::string::npos) {
            std::string key = line.substr(0, delimiter);
            std::string val = line.substr(delimiter + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);

            if (key == "title" || key == "name") info.title = val;
            else if (key == "author") info.author = val;
            else if (key == "description") info.description = val;
        }
    }
    return info;
}

std::vector<GameInfo> getAvailableGames() {
    std::vector<GameInfo> games;
    std::string gamesDir = "games";
    if (!fs::exists(gamesDir) && fs::exists("../../games")) gamesDir = "../../games";

    if (fs::exists(gamesDir)) {
        for (const auto& entry : fs::directory_iterator(gamesDir)) {
            if (entry.is_directory()) {
                fs::path confPath = entry.path() / "game.conf";
                if (fs::exists(confPath) || fs::exists(entry.path() / "mod.conf")) {
                    games.push_back(parseGameConfig(entry.path()));
                }
            }
        }
    }

    if (games.empty()) {
        games.push_back({"craft_and_ruin", "Craft & Ruin", "Brandon", "Default Voxel Survival Game"});
    }
    return games;
}

std::vector<std::string> getAvailableWorlds() {
    std::vector<std::string> worlds;
    std::string savesDir = "saves";
    if (!fs::exists(savesDir) && fs::exists("../../saves")) savesDir = "../../saves";

    if (fs::exists(savesDir)) {
        for (const auto& entry : fs::directory_iterator(savesDir)) {
            if (entry.is_directory()) {
                worlds.push_back(entry.path().filename().string());
            }
        }
    }
    if (worlds.empty()) worlds.push_back("world_alpha");
    return worlds;
}

// ===========================================================================
// World saves
//   saves/<name>/world.dat      seed, player position/look, hotbar (text key=value)
//   saves/<name>/chunks/*.bin   chunks that were changed (written by World)
// ===========================================================================

Player* g_PlayerInstance = nullptr;
bool g_PendingWorldOpen = false;          // open g_ActiveWorldName at the start of LOADING
double g_LastAutosave = 0.0;
constexpr double kAutosaveSeconds = 120.0;

fs::path savesRoot() {
    if (!fs::exists("saves") && fs::exists("../../saves")) return fs::path("../../saves");
    return fs::path("saves");
}

uint32_t randomSeed() {
    std::random_device rd;
    return rd() ^ (rd() << 16);
}

// Empty -> random. A number -> that number. Any other text -> a hash of it.
uint32_t seedFromText(std::string t) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    if (t.empty()) return randomSeed();

    const bool numeric = t.size() <= 10 &&
        std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        const unsigned long long v = std::stoull(t);
        if (v <= 0xFFFFFFFFull) return static_cast<uint32_t>(v);
    }

    uint32_t h = 2166136261u;   // FNV-1a
    for (unsigned char c : t) { h ^= c; h *= 16777619u; }
    return h;
}

// Keeps letters, digits, spaces, '-' and '_' so the name is a safe folder name.
std::string sanitizeWorldName(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "New World";
    if (out.size() > 32) out.resize(32);
    return out;
}

struct WorldMeta {
    bool valid = false;
    uint32_t seed = 0;
    bool hasPlayer = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    std::vector<std::pair<std::string, int>> hotbar;   // block name ("" = empty), count
    std::string game;                                  // games/<folder> this world belongs to ("" = older save)
};

bool readWorldMeta(const fs::path& file, WorldMeta& meta) {
    std::ifstream in(file);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        std::istringstream val(line.substr(eq + 1));

        if (key == "seed") {
            val >> meta.seed;
            meta.valid = !val.fail();
        } else if (key == "player") {
            val >> meta.position.x >> meta.position.y >> meta.position.z;
            meta.hasPlayer = !val.fail();
        } else if (key == "look") {
            val >> meta.yaw >> meta.pitch;
        } else if (key == "game") {
            meta.game = line.substr(eq + 1);
        } else if (key.rfind("hotbar", 0) == 0) {
            const int idx = std::atoi(key.c_str() + 6);
            std::string name;
            int count = 0;
            val >> name >> count;
            if (idx >= 1 && idx <= 64) {
                if (static_cast<int>(meta.hotbar.size()) < idx) meta.hotbar.resize(idx);
                meta.hotbar[idx - 1] = { name == "-" ? "" : name, count };
            }
        }
    }
    return meta.valid;
}

// withHotbar: write the hotbar that's on screen now. Only for the world being played - a world
// that is just being created must not get it (that was the hotbar of the last world played, so
// every new world started with the previous world's items).
bool writeWorldMeta(const fs::path& file, uint32_t seed, const Player* player, const std::string& game,
                    bool withHotbar = true) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << std::fixed << std::setprecision(3);
        out << "version=1\n";
        if (!game.empty()) out << "game=" << game << "\n";
        out << "seed=" << seed << "\n";
        if (player) {
            out << "player=" << player->position.x << " " << player->position.y << " " << player->position.z << "\n";
            out << "look=" << player->yaw << " " << player->pitch << "\n";
        }
        if (g_Hotbar && withHotbar) {
            for (int i = 0; i < g_Hotbar->slotCount; ++i) {
                const HotbarSlot s = g_Hotbar->getSlot(i);
                const std::string name = s.blockId ? BlockRegistry::getInstance().getBlock(s.blockId).name : "-";
                out << "hotbar" << (i + 1) << "=" << name << " " << (s.blockId ? s.count : 0) << "\n";
            }
        }
        if (!out) return false;
    }

    if (!replaceFile(tmp, file, ec)) {   // the old file stays as it was
        std::cerr << "[Save] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

// Writes changed chunks + world.dat for the world that is currently open.
void saveCurrentWorld() {
    World* w = g_WorldInstance;
    if (!w || w->saveDirectory().empty()) return;

    const int written = w->saveAll();
    {
        std::ofstream f(fs::path(w->saveDirectory()) / "mod_storage.txt");
        for (const auto& [k, v] : g_ModStorage) f << escapeStorage(k) << '\t' << escapeStorage(v) << '\n';
        g_ModStorageDirty = false;
    }
    const bool metaOk = writeWorldMeta(fs::path(w->saveDirectory()) / "world.dat", w->seed(),
                                       g_PlayerInstance, g_LoadedGameFolder);
    std::cout << "[Save] Saved '" << g_ActiveWorldName << "' (" << written << " changed chunks"
              << (metaOk ? "" : ", world.dat FAILED") << ")" << std::endl;
}

// Can this world be played with the game that is currently loaded?
// Older saves without a "game=" line belong to whichever game opens them first.
bool worldBelongsToLoadedGame(const WorldMeta& meta, std::string* why = nullptr) {
    if (meta.game.empty() || meta.game == g_LoadedGameFolder) return true;
    if (why) *why = "This world was made with '" + meta.game + "'. Restart the game with that game selected to play it.";
    return false;
}

// Opens g_ActiveWorldName: creates it if new, otherwise restores seed, player and hotbar.
// Returns false (and opens nothing) if the world belongs to a different game.
bool openActiveWorld(World& world, Player& player) {
    const fs::path dir = savesRoot() / g_ActiveWorldName;

    WorldMeta meta;
    const bool existing = readWorldMeta(dir / "world.dat", meta);

    std::string why;
    if (existing && !worldBelongsToLoadedGame(meta, &why)) {
        std::cerr << "[World] Refusing to open '" << g_ActiveWorldName << "': " << why << std::endl;
        return false;
    }

    std::error_code ec;
    fs::create_directories(dir / "chunks", ec);

    world.reset();
    g_SharedValues.clear();   // shared values (weather...) belong to a world
    g_SharedChanged.clear();
    g_ModStorage.clear();     // so does mod storage
    {
        std::ifstream f(dir / "mod_storage.txt");
        std::string line;
        while (std::getline(f, line)) {
            const size_t tab = line.find('\t');
            if (tab != std::string::npos) g_ModStorage[unescapeStorage(line.substr(0, tab))] = unescapeStorage(line.substr(tab + 1));
        }
    }
    g_Dig = DigState{};
    world.setSaveDirectory(dir.string());
    world.setSeed(existing ? meta.seed : randomSeed());

    // Hotbar: empty, then restore what was saved
    if (g_Hotbar) {
        for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
        for (size_t i = 0; i < meta.hotbar.size() && static_cast<int>(i) < g_Hotbar->slotCount; ++i) {
            const auto& entry = meta.hotbar[i];
            if (entry.first.empty()) continue;
            const uint8_t id = BlockRegistry::getInstance().getBlockId(entry.first);
            if (id != 0) g_Hotbar->setSlot(static_cast<int>(i), id, entry.second > 0 ? entry.second : 64);
        }
    }

    // Player: saved position, or a fresh spawn (LOADING finds the ground)
    if (existing && meta.hasPlayer) {
        player.position = meta.position;
        player.yaw = meta.yaw;
        player.pitch = meta.pitch;
        g_SpawnPlaced = true;
    } else {
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);
        g_SpawnPlaced = false;
    }

    // Make sure the seed is on disk straight away
    // (also stamps older saves with the game that opened them)
    writeWorldMeta(dir / "world.dat", world.seed(), (existing && meta.hasPlayer) ? &player : nullptr,
                   g_LoadedGameFolder);
    g_LastAutosave = glfwGetTime();

    std::cout << "[World] " << (existing ? "Loaded" : "Created") << " world '" << g_ActiveWorldName
              << "' (seed " << world.seed() << ", game " << g_LoadedGameFolder << ")" << std::endl;
    return true;
}

// Switches to the loading screen; the world itself is opened from the main loop.
void beginLoadingWorld(GLFWwindow* win) {
    g_CurrentGameState = GameState::LOADING;
    g_PendingWorldOpen = true;
    g_SpawnPlaced = false;
    g_LoadStartTime = glfwGetTime();
    g_LoadingProgress = 0.0f;
    g_Paused = false;
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

struct SavedWorldInfo {
    std::string name;
    std::string seed;
    std::string game;
    std::string gameMode;   // "world:game_mode" from its mod storage ("" = not set)
    fs::file_time_type lastPlayed;
};

// One value from a world's mod storage (saves/<world>/mod_storage.txt), "" if not there
std::string readWorldStorage(const fs::path& worldDir, const std::string& key) {
    std::ifstream f(worldDir / "mod_storage.txt");
    std::string line;
    while (std::getline(f, line)) {
        const size_t tab = line.find('\t');
        if (tab != std::string::npos && unescapeStorage(line.substr(0, tab)) == key) {
            return unescapeStorage(line.substr(tab + 1));
        }
    }
    return "";
}

// Worlds for the game currently selected on the title screen, most recently played first.
// Older saves with no game recorded are shown for every game until one opens them.
std::vector<SavedWorldInfo> listSavedWorlds() {
    std::vector<SavedWorldInfo> list;
    const fs::path root = savesRoot();
    std::error_code ec;
    if (!fs::exists(root, ec)) return list;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        SavedWorldInfo info;
        info.name = entry.path().filename().string();

        WorldMeta meta;
        const fs::path dat = entry.path() / "world.dat";
        info.seed = readWorldMeta(dat, meta) ? std::to_string(meta.seed) : "?";
        info.game = meta.game;
        if (!info.game.empty() && info.game != g_ActiveGameFolder) continue;   // another game's world

        info.gameMode = readWorldStorage(entry.path(), "world:game_mode");
        info.lastPlayed = fs::exists(dat, ec) ? fs::last_write_time(dat, ec) : fs::last_write_time(entry.path(), ec);
        list.push_back(info);
    }

    std::sort(list.begin(), list.end(),
              [](const SavedWorldInfo& a, const SavedWorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return list;
}

// Creates saves/<name>/ with a world.dat holding the seed. `options` (e.g. game_mode =
// "survival") go into the new world's mod storage as world:<key>, for mods to read with
// engine.get_storage("world:game_mode"). Returns the folder name used.
std::string createWorld(const std::string& rawName, const std::string& seedText,
                        const std::map<std::string, std::string>& options = {}) {
    const std::string base = sanitizeWorldName(rawName);
    std::string name = base;
    for (int n = 2; fs::exists(savesRoot() / name); ++n) {
        name = base + " (" + std::to_string(n) + ")";
    }

    std::error_code ec;
    fs::create_directories(savesRoot() / name / "chunks", ec);
    writeWorldMeta(savesRoot() / name / "world.dat", seedFromText(seedText), nullptr, g_LoadedGameFolder,
                   false);   // a new world starts with an empty hotbar
    if (!options.empty()) {
        std::ofstream f(savesRoot() / name / "mod_storage.txt");
        for (const auto& [k, v] : options) f << escapeStorage("world:" + k) << '\t' << escapeStorage(v) << '\n';
    }
    std::cout << "[World] Created save folder '" << name << "' for game '" << g_LoadedGameFolder << "'" << std::endl;
    return name;
}

// Can the world list be used right now? Only if the selected game is the one whose mods are loaded.
bool selectedGameIsLoaded() {
    return g_ActiveGameFolder == g_LoadedGameFolder;
}

// Permanently deletes saves/<name>. Refuses anything that isn't a world folder directly
// inside saves/, a world from another game, or the world that is currently open.
bool deleteWorld(const std::string& name, std::string& error) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        error = "Invalid world name.";
        return false;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(savesRoot(), ec);
    const fs::path dir = fs::weakly_canonical(savesRoot() / name, ec);
    if (ec || dir.parent_path() != root || !fs::is_directory(dir, ec)) {
        error = "World folder not found.";
        return false;
    }

    WorldMeta meta;
    readWorldMeta(dir / "world.dat", meta);
    if (!meta.game.empty() && meta.game != g_ActiveGameFolder) {
        error = "That world belongs to another game.";
        return false;
    }

    if (g_WorldInstance && !g_WorldInstance->saveDirectory().empty() &&
        fs::weakly_canonical(g_WorldInstance->saveDirectory(), ec) == dir) {
        error = "That world is currently open.";
        return false;
    }

    const auto removed = fs::remove_all(dir, ec);
    if (ec) {
        error = "Could not delete: " + ec.message();
        return false;
    }
    std::cout << "[World] Deleted '" << name << "' (" << removed << " files)" << std::endl;
    return true;
}

// ===========================================================================
// Installed mods (for the Mods screen)
// ===========================================================================

std::string modPathKey(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return (ec ? p : c).string();
}

struct ModRoot {
    fs::path path;
    std::string source;   // "Global" or "Game: <folder>"
};

// Same folders the startup loader reads, each listed once.
std::vector<ModRoot> modRoots() {
    std::vector<ModRoot> roots;
    std::set<std::string> seen;
    auto add = [&](const std::string& rel, const std::string& source) {
        std::string resolved = rel;
        if (!fs::exists(resolved) && fs::exists("../../" + rel)) resolved = "../../" + rel;
        std::error_code ec;
        if (!fs::is_directory(resolved, ec)) return;
        const std::string key = modPathKey(resolved);
        if (seen.insert(key).second) roots.push_back({ fs::path(resolved), source });
    };
    add("mods", "Global");
    add("../mods", "Global");
    add("../../mods", "Global");
    add("games/" + g_LoadedGameFolder + "/mods", "Game: " + g_LoadedGameFolder);
    return roots;
}

// key = value lines (mod.conf style)
std::map<std::string, std::string> readConfFile(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            const size_t e = s.find_last_not_of(" \t");
            s.erase(e == std::string::npos ? 0 : e + 1);
        };
        trim(key);
        trim(val);
        out[key] = val;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Turning mods in the mods/ folder on and off
//   Saved in mods/mod_settings.txt as lines like:   my_mod = false
//   Mods are on unless listed as false. Changes apply on the next start.
//   Mods in games/<game>/mods are part of the game and can't be switched off here.
// ---------------------------------------------------------------------------

std::map<std::string, bool> g_ModEnabledAtStartup;   // what the loader actually did this run

fs::path globalModsFolder() {
    for (const auto& r : modRoots()) {
        if (r.source == "Global") return r.path;
    }
    return fs::path("mods");
}

fs::path modSettingsFile() {
    return globalModsFolder() / "mod_settings.txt";
}

std::map<std::string, bool> loadModSettings() {
    std::map<std::string, bool> settings;
    for (const auto& [key, val] : readConfFile(modSettingsFile())) {
        settings[key] = !(val == "false" || val == "0" || val == "off" || val == "no");
    }
    return settings;
}

bool saveModSettings(const std::map<std::string, bool>& settings) {
    const fs::path file = modSettingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# Mods switched on/off on the Mods screen. Changes apply when the game restarts.\n";
        for (const auto& [id, on] : settings) out << id << " = " << (on ? "true" : "false") << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    if (!replaceFile(tmp, file, ec)) {   // the old file stays as it was
        std::cerr << "[Mods] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

// A mod that can't be switched off: the base mod (it contains the Mods screen itself),
// or any mod whose mod.conf says "required = true".
bool modIsRequired(const fs::path& modFolder, const std::string& id) {
    if (id == "base") return true;
    std::error_code ec;
    const fs::path conf = modFolder / "mod.conf";
    if (!fs::exists(conf, ec)) return false;
    auto kv = readConfFile(conf);
    return kv["required"] == "true";
}

bool modIsEnabled(const fs::path& rootFolder, const std::string& id) {
    if (modIsRequired(rootFolder / id, id)) return true;
    const auto settings = loadModSettings();
    const auto it = settings.find(id);
    return it == settings.end() || it->second;
}

struct ModInfo {
    std::string id;           // folder or file name
    std::string title, author, description, version, depends;
    std::string source;       // Global / Game: x
    std::string path;
    std::string status;       // "loaded", "error", "not_loaded", "disabled"
    std::string error;
    int luaFiles = 0;
    bool isFolder = false;
    bool enabled = true;      // switched on (takes effect on restart)
    bool required = false;    // can't be switched off
    bool restartNeeded = false;
};

// Fills status/error from what happened when the files were run at startup.
void applyLoadStatus(ModInfo& m, const std::vector<fs::path>& files) {
    bool ran = false;
    for (const auto& f : files) {
        const auto it = g_ModFileStatus.find(modPathKey(f));
        if (it == g_ModFileStatus.end()) continue;
        ran = true;
        if (!it->second.empty() && m.error.empty()) m.error = it->second;
    }
    m.status = !m.error.empty() ? "error" : (ran ? "loaded" : "not_loaded");
}

// Every mod folder and loose .lua file in the mods/ folder (game mods are not listed).
std::vector<ModInfo> listInstalledMods() {
    std::vector<ModInfo> mods;
    for (const auto& root : modRoots()) {
        if (root.source != "Global") continue;   // only the mods/ folder

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root.path, ec)) {
            if (entry.is_directory() && entry.path().filename() == "clientmods") continue;   // client mods: listed separately
            ModInfo m;
            m.source = root.source;
            m.path = entry.path().string();
            std::vector<fs::path> files;

            if (entry.is_directory()) {
                m.isFolder = true;
                m.id = entry.path().filename().string();
                for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
                    if (f.is_regular_file() && f.path().extension() == ".lua") files.push_back(f.path());
                }
                if (files.empty()) continue;   // not a mod (e.g. a textures folder)

                const fs::path conf = entry.path() / "mod.conf";
                if (fs::exists(conf, ec)) {
                    auto kv = readConfFile(conf);
                    m.title = kv.count("title") ? kv["title"] : kv["name"];
                    m.author = kv["author"];
                    m.description = kv["description"];
                    m.version = kv["version"];
                    m.depends = kv["depends"];
                }
            } else if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                m.id = entry.path().filename().string();
                files.push_back(entry.path());
            } else {
                continue;
            }

            if (m.title.empty()) m.title = m.id;
            m.luaFiles = static_cast<int>(files.size());
            m.required = modIsRequired(entry.path(), m.id);
            m.enabled = modIsEnabled(root.path, m.id);

            applyLoadStatus(m, files);
            const auto started = g_ModEnabledAtStartup.find(m.id);
            const bool wasOn = started == g_ModEnabledAtStartup.end() || started->second;
            if (!wasOn) m.status = "disabled";
            m.restartNeeded = (wasOn != m.enabled);

            mods.push_back(m);
        }
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        return a.title < b.title;
    });
    return mods;
}

// Switches a mod in mods/ on or off (applies on restart). Returns false + reason on failure.
bool setModEnabled(const std::string& id, bool on, std::string& error) {
    if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos ||
        id.find("..") != std::string::npos) {
        error = "Invalid mod name.";
        return false;
    }
    const fs::path root = globalModsFolder();
    std::error_code ec;
    if (!fs::exists(root / id, ec)) {
        error = "Mod not found.";
        return false;
    }
    if (!on && modIsRequired(root / id, id)) {
        error = "This mod is required and can't be switched off.";
        return false;
    }

    auto settings = loadModSettings();
    settings[id] = on;
    if (!saveModSettings(settings)) {
        error = "Could not save " + modSettingsFile().string();
        return false;
    }
    std::cout << "[Mods] " << id << " will be " << (on ? "enabled" : "disabled") << " after a restart" << std::endl;
    return true;
}

// Opens the main mods folder in Explorer / Finder / the desktop file manager.
void openModsFolder() {
    auto roots = modRoots();
    fs::path target;
    for (const auto& r : roots) {
        if (r.source == "Global") { target = r.path; break; }
    }
    if (target.empty()) {
        std::error_code ec;
        fs::create_directories("mods", ec);
        target = "mods";
    }
    std::error_code ec;
    std::string abs = fs::absolute(target, ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());   // keep the command well-formed

#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    std::cout << "[Mods] Opening " << abs << std::endl;
    (void)std::system(cmd.c_str());
}

// Calls a Lua UI function if it exists. Returns true only if it returned true.
template <typename... Args>
bool callLuaUI(sol::state* lua, const char* fn, Args&&... args) {
    if (!lua) return false;
    try {
        sol::protected_function f = (*lua)[fn];
        if (!f.valid()) return false;
        sol::protected_function_result r = f(std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Lua] " << fn << ": " << err.what() << std::endl;
            return false;
        }
        if (r.return_count() > 0 && r.get_type() == sol::type::boolean) return r.get<bool>();
    } catch (...) {}
    return false;
}

struct EngineContext {
    Player* player = nullptr;
    sol::state* lua = nullptr;
};

const int TEX_SIZE = 16;
const int MAX_TEXTURE_LAYERS = 256;
GLuint g_TextureArrayID = 0;
int g_NextTextureLayer = 0;

void initTextureArray() {
    glGenTextures(1, &g_TextureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, MAX_TEXTURE_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// ===========================================================================
// Texture packs
//   texturepacks/<pack>/          one folder per pack
//   texturepacks/<pack>/pack.conf title, author, description (all optional)
//   Any image in a pack with the same file name as a game texture replaces it,
//   e.g. texturepacks/MyPack/stone.png (or .../textures/stone.png).
//   The chosen pack is saved in texturepacks/active_pack.txt.
// ===========================================================================

struct TextureSource {
    std::string requested;   // name the game asked for, e.g. "stone.png"
    std::string resolved;    // file it was found at without a pack ("" = not found)
};

std::vector<TextureSource> g_LayerSources;                 // block texture layer -> source
std::map<GLuint, TextureSource> g_StandardTextureSources;  // 2D UI textures (hotbar, etc.)
std::string g_ActiveTexturePack;                           // "" = default textures

fs::path texturePacksRoot() {
    if (!fs::exists("texturepacks") && fs::exists("../../texturepacks")) return fs::path("../../texturepacks");
    return fs::path("texturepacks");
}

bool isSafeFolderName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos && name.find("..") == std::string::npos;
}

// The file in `pack` that replaces `requested`, or "".
std::string packOverrideFor(const std::string& pack, const std::string& requested) {
    if (pack.empty() || requested.empty()) return "";
    const fs::path root = texturePacksRoot() / pack;
    const fs::path req(requested);
    if (req.is_absolute() || requested.find("..") != std::string::npos) return "";
    const fs::path candidates[] = { root / req, root / "textures" / req.filename(), root / req.filename() };
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) return c.string();
    }
    return "";
}

std::string packOverride(const std::string& requested) {
    return packOverrideFor(g_ActiveTexturePack, requested);
}

// Loads an image as RGBA, resized (nearest neighbour) to size x size.
// Tall animation strips (frames stacked vertically) use their first frame.
bool loadImageScaled(const std::string& file, int size, std::vector<unsigned char>& out) {
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!d) return false;

    int srcH = h;
    if (h > w && w > 0 && h % w == 0) srcH = w;   // first frame of an animation strip

    out.resize(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        const int sy = y * srcH / size;
        for (int x = 0; x < size; ++x) {
            const int sx = x * w / size;
            const unsigned char* s = d + (static_cast<size_t>(sy) * w + sx) * 4;
            unsigned char* o = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3];
        }
    }
    stbi_image_free(d);
    return true;
}

// (Re)uploads one block texture layer: pack image, else the original, else white.
void uploadLayer(int layer, const TextureSource& src) {
    std::vector<unsigned char> px;
    const std::string override = packOverride(src.requested);
    bool ok = !override.empty() && loadImageScaled(override, TEX_SIZE, px);
    if (!ok && !src.resolved.empty()) ok = loadImageScaled(src.resolved, TEX_SIZE, px);
    if (!ok) px.assign(static_cast<size_t>(TEX_SIZE) * TEX_SIZE * 4, 255);   // missing texture: white

    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

// Every .png in the game's folder and the mods folders, by file name (lower case), like
// Luanti: a picture can be used by its name alone, whichever mod or subfolder it's in.
// Built when mods are about to load; used when the usual places don't have a file.
std::unordered_map<std::string, std::string> g_TextureIndex;
std::vector<std::string> g_TextureIndexRoots;

void buildTextureIndex() {
    g_TextureIndex.clear();
    g_TextureIndexRoots.clear();
    const std::vector<std::string> roots = {
        "games/" + g_ActiveGameFolder, "../../games/" + g_ActiveGameFolder,
        "mods", "../mods", "../../mods", "textures", "../../textures" };
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        g_TextureIndexRoots.push_back(fs::absolute(root, ec).string());
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".png") continue;
            std::string key = it->path().filename().string();
            for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            g_TextureIndex.emplace(key, fs::absolute(it->path(), ec).string());   // first one found wins
        }
    }
    std::cout << "[Texture] Indexed " << g_TextureIndex.size() << " pictures in the game and mod folders" << std::endl;
}

// A picture by file name from the index ("" if none)
std::string indexedTexture(const std::string& path) {
    std::string key = fs::path(path).filename().string();
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto it = g_TextureIndex.find(key);
    return it == g_TextureIndex.end() ? std::string() : it->second;
}

std::string textureSearchHint() {
    std::string where;
    for (const auto& r : g_TextureIndexRoots) where += (where.empty() ? "" : ", ") + r;
    return where.empty() ? "no game or mods folder found" : where;
}

int loadTextureToLayer(const std::string& path) {
    std::string finalPath = path;

    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = p;
            found = true;
            break;
        }
    }
    if (!found) {   // anywhere in the game or a mod, by name
        const std::string indexed = indexedTexture(path);
        if (!indexed.empty()) { finalPath = indexed; found = true; }
    }

    if (!found && packOverride(path).empty()) {
        std::cerr << "[Texture] Missing block texture: " << path << std::endl;
    }
    if (found && packOverride(path).empty()) {
        int w, h, n;
        if (!stbi_info(finalPath.c_str(), &w, &h, &n)) return -1;   // unreadable image
    }
    if (g_NextTextureLayer >= MAX_TEXTURE_LAYERS) {
        std::cerr << "[Texture] Out of texture layers (max " << MAX_TEXTURE_LAYERS << "): " << path << std::endl;
        return -1;
    }

    const int layer = g_NextTextureLayer++;
    if (static_cast<int>(g_LayerSources.size()) <= layer) g_LayerSources.resize(layer + 1);
    g_LayerSources[layer] = TextureSource{ path, found ? finalPath : "" };
    uploadLayer(layer, g_LayerSources[layer]);
    return layer;
}

// (Re)uploads a 2D texture at its own size: pack image, else the original.
bool uploadStandard(GLuint texID, const TextureSource& src) {
    std::string file = packOverride(src.requested);
    int width = 0, height = 0, nrChannels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = file.empty() ? nullptr : stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    if (!data && !src.resolved.empty()) {
        file = src.resolved;
        data = stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    }
    if (!data) return false;

    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[Texture] Loaded " << file << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

// Every folder a mod was loaded from. Pictures asked for after loading (the HUD, items
// used later) are looked for in all of them, since by then the "current mod" is just
// whichever loaded last.
std::vector<std::string> g_ModFolders;

// Where a picture is. The calling mod's own folder comes first, so a mod always gets its
// own picture even if the game or another folder has a file with the same name.
// "" if nowhere.
std::string findTextureFile(const std::string& path) {
    std::vector<std::string> searchPaths = {
        g_CurrentModPath + "/textures/" + path,
        g_CurrentModPath + "/" + path,
        "../../" + g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        path,
        "textures/" + path,
        "../../textures/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };
    for (const auto& dir : g_ModFolders) {
        searchPaths.push_back(dir + "/textures/" + path);
        searchPaths.push_back(dir + "/" + path);
    }
    for (const auto& p : searchPaths) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec)) return fs::absolute(p, ec).string();
    }
    return indexedTexture(path);   // anywhere in the game or a mod, by name
}

GLuint loadStandardTexture(const std::string& path) {
    std::string finalPath = path;
    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };
    for (const auto& dir : g_ModFolders) {   // any loaded mod's folder (see g_ModFolders)
        searchPaths.push_back(dir + "/textures/" + path);
        searchPaths.push_back(dir + "/" + path);
    }

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = fs::absolute(p).string();
            found = true;
            break;
        }
    }
    if (!found) {   // anywhere in the game or a mod, by name
        const std::string indexed = indexedTexture(path);
        if (!indexed.empty()) { finalPath = indexed; found = true; }
    }
    if (!found && packOverride(path).empty()) {
        std::cerr << "[Texture] NOT FOUND: " << path << " - no file with that name anywhere in "
                  << textureSearchHint() << std::endl;
        return 0;
    }

    GLuint texID = 0;
    glGenTextures(1, &texID);
    const TextureSource src{ path, found ? finalPath : "" };
    if (!uploadStandard(texID, src)) {
        std::cerr << "[Texture] Failed to decode: " << (found ? finalPath : path) << std::endl;
        glDeleteTextures(1, &texID);
        return 0;
    }
    g_StandardTextureSources[texID] = src;
    return texID;
}

// Re-uploads every texture with the current pack. No restart needed.
void reloadAllTextures() {
    for (int i = 0; i < static_cast<int>(g_LayerSources.size()); ++i) uploadLayer(i, g_LayerSources[i]);
    for (const auto& [id, src] : g_StandardTextureSources) uploadStandard(id, src);
    std::cout << "[Texture] Reloaded " << g_LayerSources.size() << " block textures and "
              << g_StandardTextureSources.size() << " UI textures"
              << (g_ActiveTexturePack.empty() ? " (default)" : " from pack '" + g_ActiveTexturePack + "'")
              << std::endl;
}

std::string readActiveTexturePack() {
    std::ifstream in(texturePacksRoot() / "active_pack.txt");
    std::string name;
    std::getline(in, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    std::error_code ec;
    if (!isSafeFolderName(name) || !fs::is_directory(texturePacksRoot() / name, ec)) return "";
    return name;
}

struct TexturePackInfo {
    std::string id, title, author, description, path;
    int images = 0;      // image files in the pack
    int replaces = 0;    // how many of the game's textures it replaces
};

// "Default" first, then every folder in texturepacks/.
std::vector<TexturePackInfo> listTexturePacks() {
    std::vector<TexturePackInfo> packs;
    TexturePackInfo def;
    def.title = "Default";
    def.description = "The game's own textures.";
    packs.push_back(def);

    const fs::path root = texturePacksRoot();
    std::error_code ec;
    fs::create_directories(root, ec);

    std::vector<TexturePackInfo> found;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        TexturePackInfo p;
        p.id = entry.path().filename().string();
        p.path = entry.path().string();

        const fs::path conf = entry.path() / "pack.conf";
        if (fs::exists(conf, ec)) {
            auto kv = readConfFile(conf);
            p.title = kv.count("title") ? kv["title"] : kv["name"];
            p.author = kv["author"];
            p.description = kv["description"];
        }
        if (p.title.empty()) p.title = p.id;

        for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file() && f.path().extension() == ".png") ++p.images;
        }

        std::set<std::string> counted;
        auto count = [&](const std::string& requested) {
            if (counted.insert(requested).second && !packOverrideFor(p.id, requested).empty()) ++p.replaces;
        };
        for (const auto& s : g_LayerSources) count(s.requested);
        for (const auto& [id, s] : g_StandardTextureSources) count(s.requested);

        found.push_back(p);
    }
    std::sort(found.begin(), found.end(),
              [](const TexturePackInfo& a, const TexturePackInfo& b) { return a.title < b.title; });
    packs.insert(packs.end(), found.begin(), found.end());
    return packs;
}

// Switches pack ("" = default), saves the choice and reloads every texture.
bool setTexturePack(const std::string& id, std::string& error) {
    std::error_code ec;
    if (!id.empty() && (!isSafeFolderName(id) || !fs::is_directory(texturePacksRoot() / id, ec))) {
        error = "Texture pack not found.";
        return false;
    }
    g_ActiveTexturePack = id;

    fs::create_directories(texturePacksRoot(), ec);
    std::ofstream out(texturePacksRoot() / "active_pack.txt", std::ios::trunc);
    out << id << "\n";
    if (!out) std::cerr << "[Texture] Could not save the chosen texture pack" << std::endl;

    reloadAllTextures();
    return true;
}

void openTexturePacksFolder() {
    std::error_code ec;
    fs::create_directories(texturePacksRoot(), ec);
    std::string abs = fs::absolute(texturePacksRoot(), ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    (void)std::system(cmd.c_str());
}

// ===========================================================================
// Models: finding model files, and models placed in the world from Lua
// ===========================================================================

// Finds a mod asset (model or texture). Tries the path as given, then the current
// mod's folder (and its models/ or textures/ subfolder), then the game folder.
std::string resolveAssetPath(const std::string& path, const std::string& subfolder) {
    if (path.empty()) return "";
    const std::string file = fs::path(path).filename().string();
    const std::vector<std::string> candidates = {
        path,
        "../../" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/" + subfolder + "/" + file,
        "games/" + g_ActiveGameFolder + "/" + path,
        "../../games/" + g_ActiveGameFolder + "/" + path,
        subfolder + "/" + file,
        "../../" + subfolder + "/" + file,
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec)) return p;
    }
    return "";
}

struct ModelInstance {
    std::shared_ptr<AnimatedModel> model;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;        // degrees
    float scale = 1.0f;
    bool visible = true;
};

std::map<int, ModelInstance> g_ModelInstances;   // models placed with engine.add_model
int g_NextModelInstance = 1;
bool g_ThirdPerson = false;                      // F5 camera (the player model is only drawn in third person)

// ===========================================================================
// Settings (settings.txt next to the mods folder) and key bindings
//   key_<action> = <GLFW key code>      e.g. key_forward = 87
//   fov = 70, render_distance = 10, vsync = 1
// ===========================================================================

// Mouse buttons can be bound to controls too: they're stored as kMouseKeyBase + the GLFW mouse
// button (1000 = left, 1001 = right, 1002 = middle, 1003+ = side buttons), next to the keyboard
// keys (GLFW key codes all lie below 1000).
constexpr int kMouseKeyBase = 1000;
constexpr int kMouseKeyLast = kMouseKeyBase + GLFW_MOUSE_BUTTON_LAST;
inline bool isMouseKey(int key) { return key >= kMouseKeyBase && key <= kMouseKeyLast; }

struct KeyAction {
    const char* id;
    const char* label;
    int defaultKey;
};

// Every rebindable action, in the order shown on the Controls tab.
const std::vector<KeyAction>& keyActions() {
    static const std::vector<KeyAction> actions = {
        { "forward",   "Walk Forward",   GLFW_KEY_W },
        { "back",      "Walk Backward",  GLFW_KEY_S },
        { "left",      "Strafe Left",    GLFW_KEY_A },
        { "right",     "Strafe Right",   GLFW_KEY_D },
        { "jump",      "Jump",           GLFW_KEY_SPACE },
        { "sneak",     "Sneak",          GLFW_KEY_LEFT_SHIFT },
        { "sprint",    "Sprint",         GLFW_KEY_LEFT_CONTROL },
        { "pick_block", "Pick Block",    kMouseKeyBase + GLFW_MOUSE_BUTTON_MIDDLE },
        { "inventory", "Open Inventory", GLFW_KEY_E },
        { "chat",      "Open Chat",      GLFW_KEY_T },
        { "command",   "Open Command",   GLFW_KEY_SLASH },
        { "third_person", "Toggle Camera View", GLFW_KEY_F5 },
        { "debug_info",  "Show FPS / Network Info", GLFW_KEY_F3 },
        { "player_list", "List Players",  GLFW_KEY_TAB },
        { "hotbar1",   "Hotbar Slot 1",  GLFW_KEY_1 },
        { "hotbar2",   "Hotbar Slot 2",  GLFW_KEY_2 },
        { "hotbar3",   "Hotbar Slot 3",  GLFW_KEY_3 },
        { "hotbar4",   "Hotbar Slot 4",  GLFW_KEY_4 },
        { "hotbar5",   "Hotbar Slot 5",  GLFW_KEY_5 },
        { "hotbar6",   "Hotbar Slot 6",  GLFW_KEY_6 },
        { "hotbar7",   "Hotbar Slot 7",  GLFW_KEY_7 },
        { "hotbar8",   "Hotbar Slot 8",  GLFW_KEY_8 },
        { "hotbar9",   "Hotbar Slot 9",  GLFW_KEY_9 },
    };
    return actions;
}

std::map<std::string, std::string> g_Settings;
bool g_SettingsInGame = false;   // settings opened from the pause menu (vs the title screen)

fs::path settingsFile() {
    const fs::path mods = globalModsFolder();
    return (mods.has_parent_path() ? mods.parent_path() : fs::path(".")) / "settings.txt";
}

void loadSettings() {
    g_Settings = readConfFile(settingsFile());
}

void saveSettings() {
    const fs::path file = settingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "[Settings] Could not write " << file.string() << std::endl;
            return;
        }
        out << "# Game settings. Edited from the Settings screen.\n";
        for (const auto& [k, v] : g_Settings) out << k << " = " << v << "\n";
    }
    std::error_code ec;
    if (!replaceFile(tmp, file, ec)) {   // the old settings stay as they were
        std::cerr << "[Settings] Could not replace " << file.string() << ": " << ec.message() << std::endl;
    }
}

int settingInt(const std::string& name, int fallback) {
    const auto it = g_Settings.find(name);
    if (it == g_Settings.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

// ===========================================================================
// Multiplayer: player name (settings.txt) and saved servers (servers.txt)
// ===========================================================================

std::string playerName() {
    const auto it = g_Settings.find("player_name");
    return it != g_Settings.end() ? it->second : std::string();
}

// Makes sure settings.txt always has a valid player name (a new one like "Player4821"
// is made and saved the first time). Returns the name.
std::string ensurePlayerName() {
    std::string name = playerName();
    if (net::validPlayerName(name)) return name;
    std::random_device rd;
    name = "Player" + std::to_string(1000 + rd() % 9000);
    g_Settings["player_name"] = name;
    saveSettings();
    std::cout << "[Settings] No player name set - using " << name << std::endl;
    return name;
}

// Changes the player name (settings.txt + chat). Not allowed while online, since the
// server already knows you by your old name.
bool changePlayerName(const std::string& name, std::string& error) {
    if (!net::validPlayerName(name)) {
        error = "Names are 1-16 letters, numbers, _ or -";
        return false;
    }
    if (name != playerName() && (g_Server.running() || g_Client.active())) {
        error = "You can't change your name while playing online";
        return false;
    }
    if (playerName() != name) {
        g_Settings["player_name"] = name;
        saveSettings();
    }
    if (g_ChatSystem) g_ChatSystem->setPlayerName(name);
    return true;
}

// Server settings (Settings > Server): what other players see in their server list.
std::string serverMotd() {
    const auto it = g_Settings.find("server_motd");
    return it != g_Settings.end() ? it->second : std::string("A BlockSmith server");
}
int serverMaxPlayers() { return std::clamp(settingInt("server_max_players", 20), 2, 200); }
int serverPort() {
    const int p = settingInt("server_port", net::kDefaultPort);
    return (p > 0 && p <= 65535) ? p : net::kDefaultPort;
}

// server_icon.png next to settings.txt: shown beside your server in other players' lists.
fs::path serverIconFile() { return settingsFile().parent_path() / "server_icon.png"; }

std::vector<uint8_t> loadServerIcon() {
    std::vector<uint8_t> bytes;
    std::error_code ec;
    const fs::path file = serverIconFile();
    if (!fs::exists(file, ec)) return bytes;
    if (fs::file_size(file, ec) > net::kMaxIconBytes) {
        std::cerr << "[Server] server_icon.png is too big (max " << net::kMaxIconBytes / 1024
                  << " KB - use a 64x64 image)" << std::endl;
        return bytes;
    }
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return bytes;
}

// Server icons received from other servers, uploaded as textures (keyed by address).
struct ServerIconTexture { GLuint tex = 0; size_t bytes = 0; uint32_t hash = 0; bool failed = false; };
std::map<std::string, ServerIconTexture> g_ServerIcons;

uint32_t hashBytes(const std::vector<uint8_t>& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) { h ^= c; h *= 16777619u; }
    return h;
}

// Texture for a server's icon, or 0 if it has none / it couldn't be read.
GLuint serverIconTexture(const std::string& address, const std::vector<uint8_t>& png) {
    ServerIconTexture& t = g_ServerIcons[address];
    const uint32_t h = hashBytes(png);
    if (t.bytes == png.size() && t.hash == h) return t.failed ? 0 : t.tex;   // unchanged

    if (t.tex) glDeleteTextures(1, &t.tex);
    t = ServerIconTexture{};
    t.bytes = png.size();
    t.hash = h;
    if (png.empty()) { t.failed = true; return 0; }

    int w = 0, hgt = 0, n = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &hgt, &n, 4);
    if (!d) { t.failed = true; return 0; }
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hgt, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // keep pixel art crisp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(d);
    return t.tex;
}

// Draws a texture at pixel position (top-left x, y) with the textured UI shader.
void drawUITexture(GLuint program, GLuint tex, float x, float y, float w, float h, int fbW, int fbH) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    const float x0 = x / fbW * 2.0f - 1.0f, x1 = (x + w) / fbW * 2.0f - 1.0f;
    const float y0 = 1.0f - y / fbH * 2.0f, y1 = 1.0f - (y + h) / fbH * 2.0f;   // y0 = top
    const float v[24] = { x0, y0, 0, 0,  x1, y0, 1, 0,  x1, y1, 1, 1,
                          x0, y0, 0, 0,  x1, y1, 1, 1,  x0, y1, 0, 1 };
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(program, "u_Texture"), 0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

fs::path serversFile() { return settingsFile().parent_path() / "servers.txt"; }

// ===========================================================================
// My Servers: dedicated servers set up from the game (servers/<name>/server.conf),
// run by BlockSmithServer in their own window.
// ===========================================================================
fs::path g_ExeDir;   // folder of the game's .exe (set in main)

// ---------------------------------------------------------------------------
// lua_api.txt: what this build of the engine (with the mods that loaded) offers modders.
// Written after the mods load, from what really exists in Lua, so it is never out of date.
// The full guide with explanations is docs/MODDING.md.
// ---------------------------------------------------------------------------
// Custom end marker (LUA): the Lua below contains a closing bracket followed by a quote,
// which would end a plain raw string early.
const char* kApiReferenceLua = R"LUA(
local out = {}
local function add(s) out[#out + 1] = s end
local function sorted_keys(t, want)
    local k = {}
    for name, v in pairs(t) do
        if type(name) == "string" and want(v) then k[#k + 1] = name end
    end
    table.sort(k)
    return k
end
local is_fn = function(v) return type(v) == "function" end
local not_fn = function(v) return type(v) ~= "function" end

add("BlockSmith - Lua API reference (written by the engine at startup)")
add("Explanations and examples: docs/MODDING.md. Fluids: docs/FLUIDS.md")
add("")

-- 1. Engine tables
local engine_tables = { "engine", "world", "ui", "hotbar", "chat", "sky", "clouds", "fog", "underwater", "menu", "sound" }
local known = {}
local total = 0
add("=== Functions the engine gives you ===")
for _, tname in ipairs(engine_tables) do
    known[tname] = true
    local t = rawget(_G, tname)
    if type(t) == "table" then
        local fns = sorted_keys(t, is_fn)
        local vals = sorted_keys(t, not_fn)
        add("")
        add(tname .. " (" .. #fns .. " functions)")
        for _, f in ipairs(fns) do add("  " .. tname .. "." .. f .. "(...)"); total = total + 1 end
        for _, v in ipairs(vals) do add("  " .. tname .. "." .. v .. "  [" .. type(t[v]) .. "]") end
    end
end
add("")
add("Chunk (passed to generate_chunk): chunk:set_block(x, y, z, id), chunk:get_block(x, y, z),")
add("  chunk:set_block_with_meta(x, y, z, id, meta), chunk:set_metadata(x, y, z, meta), chunk:get_metadata(x, y, z)")

-- 2. Hooks: global functions the engine calls if a mod defines them
local hooks = {
    { "generate_chunk",           "(chunk, cx, cz)  build a new 16x16 chunk's terrain (runs on generator threads)" },
    { "set_world_seed",           "(seed)  the world's seed, before any generate_chunk" },
    { "on_update",                "(dt)  every frame" },
    { "on_chat_command",          "(text, player, is_operator, reply) -> true if handled  a /command" },
    { "on_game_mode",             "(mode, is_operator)  the server set your game mode (mode nil = left the server)" },
    { "on_shared_changed",        "(key, value)  a shared value changed (engine.set_shared; e.g. the weather)" },
    { "render_hud",               "(W, H)  draw HUD pieces every frame in game (hearts, hunger...)" },
    { "on_player_click",          "(\"left\"|\"right\") -> true if used  before digging/placing (death screen...)" },
    { "on_pick_block",            "(x, y, z, item, meta, block) -> true if handled  pick block (middle mouse); else creative picks it" },
    { "handle_settings_mouse",    "(code) -> true if used  a mouse button while Settings is open (1000 + button, for keybinds)" },
    { "on_dig_block",             "(x, y, z, block, item) -> true if you handled the drops  (survival digging)" },
    { "get_dig_time",             "(block, item) -> seconds  override how long a block takes to dig" },
    { "on_right_click_block",     "(x, y, z, block, held_item, sneaking, repeat) -> true if handled" },
    { "on_block_broken",          "(x, y, z, block, meta)  a player broke a block (after it's gone)" },
    { "on_block_placed",          "(x, y, z, block)  a player placed a block" },
    { "on_use_hoe",               "(x, y, z, block)  right-click with a hoe" },
    { "on_use_scythe",            "(x, y, z, block)  left-click with a scythe" },
    { "update_player_animation",  "(dt, is_moving, pitch)" },
    { "get_water_color",          "(x, z) -> { r, g, b, deep_r, deep_g, deep_b }  underwater fog colour here" },
    { "on_char_input",            "(codepoint)  typed text (unicode)" },
    { "on_scroll_input",          "(dx, dy)  mouse wheel" },
    { "toggle_main_inventory",    "() -> true if now open  the inventory key in survival (frees the mouse)" },
    { "handle_inventory_click",   "(mx, my, W, H, \"left\"|\"right\", shift)  click while your inventory is open" },
    { "on_hotbar_full",           "(item, count) -> true if stored  a dug item didn't fit in the hotbar" },
    { "render_pause_menu",        "(W, H)  draw the pause menu" },
    { "handle_pause_click",       "(mx, my, W, H)" },
    { "render_settings",          "(W, H)  settings screen (also handle_settings_click/_key/_char/_scroll)" },
    { "render_world_select",      "(W, H)  world list (also handle_world_select_click/_key/_char/_scroll)" },
    { "render_multiplayer",       "(W, H)  server list (also handle_multiplayer_click/_key/_char/_scroll)" },
    { "render_mods",              "(W, H)  mods screen (also handle_mods_click/_key/_scroll)" },
    { "render_texture_packs",     "(W, H)  texture packs (also handle_texture_packs_click/_key/_scroll)" },
    { "render_player_list",       "(W, H)  player list (hold Tab)" },
    { "render_creative_inventory","(W, H)  creative inventory (also handle_creative_click/_key/_char/_scroll)" },
    { "render_inventory",         "(W, H)  survival inventory" },
    { "on_settings_opened",       "()  also on_world_select_opened, on_multiplayer_opened, on_mods_opened," },
    { "on_creative_opened",       "()  on_texture_packs_opened, on_creative_closed" },
}
add("")
add("=== Hooks: functions YOU define; the engine calls them ===")
add("  [x] = a loaded mod defines it")
for _, h in ipairs(hooks) do
    local used = type(rawget(_G, h[1])) == "function" and "[x]" or "[ ]"
    add("  " .. used .. " " .. h[1] .. h[2])
end

-- 3. Tables mods made for other mods (e.g. buckets.register)
local lua_std = { _G = true, string = true, table = true, math = true, package = true, io = true,
                  os = true, coroutine = true, debug = true, utf8 = true, bit32 = true, jit = true }
local mod_tables = sorted_keys(_G, function(v) return type(v) == "table" end)
local any = false
for _, tname in ipairs(mod_tables) do
    if not known[tname] and not lua_std[tname] then
        local fns = sorted_keys(_G[tname], is_fn)
        if #fns > 0 then
            if not any then add(""); add("=== APIs that loaded mods offer to other mods ==="); any = true end
            for _, f in ipairs(fns) do add("  " .. tname .. "." .. f .. "(...)") end
        end
    end
end

-- 4. Fluids
if engine and engine.list_fluids then
    add("")
    add("=== Fluids ===")
    for _, f in ipairs(engine.list_fluids()) do
        add(string.format("  %s: source %s, flowing %s, spreads %d, step %.2f s%s", f.name, f.source,
            (f.flowing ~= "" and f.flowing) or "-", f.spread, f.tick, f.infinite and ", infinite" or ""))
    end
end
return table.concat(out, "\n"), total
)LUA";

// Writes lua_api.txt next to the game (or in the working folder). Returns the path, "" on failure.
std::string writeApiReference(sol::state& lua) {
    try {
        sol::protected_function_result r = lua.safe_script(kApiReferenceLua, sol::script_pass_on_error);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[API] Couldn't build lua_api.txt: " << err.what() << std::endl;
            return "";
        }
        std::string text = r.get<std::string>(0);
        const int functions = r.get<int>(1);

        // Every registered block (only C++ knows the full list)
        std::string blockList;
        int blocks = 0;
        auto& reg = BlockRegistry::getInstance();
        for (int id = 1; id < 256; ++id) {
            const auto& b = reg.getBlock(static_cast<uint8_t>(id));
            if (b.name.empty() || b.name == "air" || reg.getBlockId(b.name) != id) continue;
            blockList += "  " + std::to_string(id) + "  " + b.name + "  (" + (b.pattern.empty() ? "cube" : b.pattern)
                       + (b.isWalkable ? ", solid" : "") + (b.isTransparent ? ", transparent" : "")
                       + (b.inventoryImage.empty() ? "" : ", item") + ")\n";
            ++blocks;
        }
        text += "\n\n=== Blocks (" + std::to_string(blocks) + " of 255) ===\n" + blockList;

        fs::path path = (g_ExeDir.empty() ? fs::path(".") : g_ExeDir) / "lua_api.txt";
        std::ofstream f(path);
        if (!f) { path = "lua_api.txt"; f.open(path); }
        if (!f) return "";
        f << text << "\n";
        std::cout << "[API] " << functions << " Lua functions, " << blocks << " blocks - see " << path.string() << std::endl;
        return path.string();
    } catch (const std::exception& e) {
        std::cerr << "[API] Couldn't write lua_api.txt: " << e.what() << std::endl;
        return "";
    }
}

// ---------------------------------------------------------------------------
// World generator threads (see ChunkGenerator.h): each gets its own Lua with the same
// mod files, but only what a world generator needs. Drawing, sound, chat, the world and
// the player belong to the main thread, so everything else is a harmless no-op there.
// ---------------------------------------------------------------------------
const char* kWorkerNoopLua = R"(
local function noop_table()
    return setmetatable({}, {
        __index = function(_, _) return noop_table() end,
        __call = function() return nil end,
    })
end
for _, name in ipairs({ "ui", "hotbar", "sky", "clouds", "fog", "underwater", "menu", "chat",
                        "player", "inventory", "audio", "sound", "input", "settings", "net" }) do
    if _G[name] == nil then _G[name] = noop_table() end
end
local function fallback(t)
    return setmetatable(t, { __index = function(_, _) return function() return nil end end })
end
fallback(engine)
fallback(world)
)";

std::unique_ptr<sol::state> makeWorldGenLua(const std::vector<fs::path>& modFiles) {
    auto lua = std::make_unique<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);
    (*lua)["print"] = [](sol::variadic_args) {};
    lua->new_usertype<ChunkData>("Chunk",
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
    auto engine = lua->create_named_table("engine");
    engine["register_block"] = [](sol::variadic_args) {};   // the main thread already did it
    engine["get_block_id"] = [](const std::string& name) { return BlockRegistry::getInstance().getBlockId(name); };
    engine["set_block"] = [](sol::variadic_args) {};
    engine["get_block_name"] = [](sol::variadic_args) { return std::string("air"); };
    engine["is_server"] = []() { return false; };
    engine["is_world_generator"] = []() { return true; };
    auto world = lua->create_named_table("world");
    world["get_caves_enabled"] = []() { return CaveGenerator::isEnabled(); };
    world["set_caves_enabled"] = [](sol::variadic_args) {};   // the main thread's setting wins
    world["set_cave_settings"] = [](sol::variadic_args) {};   // same
    world["get_cave_settings"] = [](sol::this_state ts) { return caveSettingsTable(ts); };
    lua->script(kWorkerNoopLua);
    for (const auto& file : modFiles) {
        (*lua)["g_CurrentModPath"] = file.parent_path().string();
        try { lua->script_file(file.string()); } catch (const std::exception&) {}   // errors shown by the main thread
    }
    return lua;
}

fs::path serverProfilesRoot() { return settingsFile().parent_path() / "servers"; }

int settingIntFrom(const std::map<std::string, std::string>& kv, const std::string& key, int fallback) {
    const auto it = kv.find(key);
    if (it == kv.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

std::vector<std::string> listServerProfiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(serverProfilesRoot(), ec)) {
        if (e.is_directory() && fs::exists(e.path() / "server.conf")) out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Same settings, defaults and comments as BlockSmithServer's own server.conf.
struct ServerSettingInfo { const char* key; const char* defaultValue; const char* comment; };
const std::vector<ServerSettingInfo>& serverSettingInfo() {
    static const std::vector<ServerSettingInfo> info = {
        {"name", "My Server", "Shown on the LAN list"},
        {"motd", "{gold}{b}A BlockSmith server{/b}", "Description in server lists. {gold} {#ff8800} {b}bold{/b} {n} = 2nd line"},
        {"icon", "server_icon.png", "Icon file in this folder (PNG, 64x64)"},
        {"game", "craft_and_ruin", "Game to run (folder in games/)"},
        {"world", "server_world", "World folder in saves/"},
        {"seed", "", "Seed for a new world (empty = random)"},
        {"port", "30000", "UDP port players connect to"},
        {"max_players", "20", ""},
        {"view_distance", "8", "Furthest players may see, in chunks"},
        {"spawn", "8 8", "Where new players appear (x z)"},
        {"welcome", "Welcome to the server, {player}!", "Sent to each player who joins"},
        {"whitelist", "false", "Only players in whitelist.txt may join"},
        {"gamemode", "survival", "survival or creative. Only operators can change a player's mode (/gamemode)"},
        {"disabled_mods", "", "Mods this server doesn't load (comma separated mod folder names)"},
        {"tick_rate", "30", "Updates per second (20-60)"},
        {"autosave_seconds", "60", ""},
    };
    return info;
}

std::map<std::string, std::string> readServerProfile(const std::string& profile) {
    auto kv = readConfFile(serverProfilesRoot() / profile / "server.conf");
    for (const auto& s : serverSettingInfo()) {
        if (!kv.count(s.key)) kv[s.key] = s.defaultValue;
    }
    return kv;
}

bool writeServerProfile(const std::string& profile, const std::map<std::string, std::string>& values,
                        std::string& error) {
    const fs::path dir = serverProfilesRoot() / profile;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(dir / "server.conf", std::ios::trunc);
    if (!out) { error = "Could not write " + (dir / "server.conf").string(); return false; }
    out << "# BlockSmith dedicated server - set up from the game's My Servers screen.\n"
        << "# You can also edit this file by hand (then type 'reload' in the server window).\n\n";
    for (const auto& s : serverSettingInfo()) {
        auto it = values.find(s.key);
        std::string v = it != values.end() ? it->second : s.defaultValue;
        v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\n' || c == '\r'; }), v.end());
        if (s.comment[0]) out << "# " << s.comment << "\n";
        out << s.key << " = " << v << "\n";
    }
    return true;
}

// A safe folder name for a new server ("My Server!" -> "My Server")
std::string serverProfileFolder(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.size() > 32) out.resize(32);
    return out;
}

fs::path findGameDir(const std::string& game) {
    for (const std::string& base : {std::string("games/"), std::string("../../games/")}) {
        std::error_code ec;
        if (fs::is_directory(base + game, ec)) return fs::path(base + game);
    }
    return {};
}

// Every mod a server running `game` could load: the game's own mods and the global mods/ folder.
struct ServerModEntry {
    std::string id, title, description, source;
    bool required = false;
};
std::vector<ServerModEntry> listModsForGame(const std::string& game) {
    std::vector<ServerModEntry> out;
    std::set<std::string> seen;
    auto scan = [&](const fs::path& root, const std::string& source) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) return;
        std::vector<fs::directory_entry> entries;
        for (const auto& e : fs::directory_iterator(root, ec)) entries.push_back(e);
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
        for (const auto& e : entries) {
            ServerModEntry m;
            if (e.is_directory()) m.id = e.path().filename().string();
            else if (e.path().extension() == ".lua") m.id = e.path().stem().string();
            else continue;
            if (!seen.insert(m.id).second) continue;
            m.source = source;
            m.title = m.id;
            if (e.is_directory()) {
                auto conf = readConfFile(e.path() / "mod.conf");
                if (!conf["title"].empty()) m.title = conf["title"];
                else if (!conf["name"].empty()) m.title = conf["name"];
                m.description = conf["description"];
                m.required = modIsRequired(e.path(), m.id);
            } else {
                m.required = (m.id == "base");
            }
            out.push_back(m);
        }
    };
    const fs::path gameDir = findGameDir(game);
    if (!gameDir.empty()) scan(gameDir / "mods", "Game");
    scan(globalModsFolder(), "Global");
    return out;
}

// BlockSmithServer.exe is built next to the game's .exe; also check the usual build folders
// (and the server's old name, CraftRuinServer, from before the engine was named).
// Firewall updates Windows was still applying when asked (Firewall.h): rule -> the setting to
// record once it finishes. Polled from the main loop (pollFirewallUpdates).
std::map<std::string, std::string> g_FirewallWatch;

void reportFirewallResult(FirewallStatus st, const std::string& settingKey, const std::string& ruleName) {
    if (st == FirewallStatus::Allowed) {
        g_Settings[settingKey] = "1";
        saveSettings();
        std::cout << "[Network] Firewall rule added for " << ruleName << std::endl;
    } else if (st == FirewallStatus::Pending) {
        g_FirewallWatch[ruleName] = settingKey;   // recorded when Windows finishes, not before
        std::cout << "[Network] Windows is still updating the firewall for " << ruleName << std::endl;
        if (g_ChatSystem) {
            g_ChatSystem->addMessage("Windows is still updating the firewall for " + ruleName + "...",
                                     glm::vec3(1.0f, 1.0f, 0.4f));
        }
    } else if (st == FirewallStatus::Failed) {
        std::cout << "[Network] Firewall rule not added (declined) - other PCs may not be able to join" << std::endl;
        if (g_ChatSystem) {
            g_ChatSystem->addMessage("Firewall not changed - if friends can't join, allow " + ruleName +
                                     " in Windows Security > Firewall > Allow an app.", glm::vec3(1.0f, 0.7f, 0.4f));
        }
    }
}

// Once a second: did a pending firewall update finish?
void pollFirewallUpdates(float dt) {
    static float wait = 0.0f;
    if (g_FirewallWatch.empty() || (wait -= dt) > 0.0f) return;
    wait = 1.0f;
    for (auto it = g_FirewallWatch.begin(); it != g_FirewallWatch.end();) {
        const FirewallStatus st = firewallStatus(it->first);
        if (st == FirewallStatus::Pending) { ++it; continue; }
        const std::string rule = it->first, key = it->second;
        it = g_FirewallWatch.erase(it);
        if (st != FirewallStatus::Idle) reportFirewallResult(st, key, rule);
    }
}

// The first time something is hosted here, ask Windows Firewall to let other PCs in (one
// "allow" prompt). Without it the LAN list shows the game but joining it times out.
void askFirewallOnce(const std::string& settingKey, const std::string& program, const std::string& ruleName) {
#ifdef _WIN32
    if (g_Settings[settingKey] == "1") return;
    if (g_FirewallWatch.count(ruleName)) return;   // an update for it is still running: no second prompt
    if (g_ChatSystem) {
        g_ChatSystem->addMessage("Windows will ask to let " + ruleName + " through the firewall, so other PCs on your network can join.",
                                 glm::vec3(1.0f, 1.0f, 0.4f));
    }
    reportFirewallResult(allowThroughFirewall(program, ruleName), settingKey, ruleName);
#else
    (void)settingKey; (void)program; (void)ruleName;
#endif
}

fs::path findServerExe() {
#ifdef _WIN32
    const std::vector<std::string> names = {"BlockSmithServer.exe", "CraftRuinServer.exe"};
#else
    const std::vector<std::string> names = {"BlockSmithServer", "CraftRuinServer"};
#endif
    std::vector<fs::path> places = {g_ExeDir, ".", "build/Release", "build/Debug", "build", "../Release", "../Debug"};
    for (const auto& exe : names) {
        for (const auto& p : places) {
            std::error_code ec;
            if (!p.empty() && fs::exists(p / exe, ec)) return fs::absolute(p / exe, ec);
        }
    }
    return {};
}

// Starts a server in its own console window (it keeps running if the game is closed).
bool startServerProfile(const std::string& profile, std::string& error) {
    const fs::path exe = findServerExe();
    if (exe.empty()) {
        error = "BlockSmithServer isn't built yet - build the BlockSmithServer target";
        return false;
    }
    std::error_code ec;
    const fs::path dir = fs::absolute(serverProfilesRoot() / profile, ec);
    const fs::path conf = dir / "server.conf";
    if (!fs::exists(conf, ec)) { error = "That server has no settings file"; return false; }
    fs::remove(dir / "stop.request", ec);
    auto quoted = [](std::string s) {
        s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
        return "\"" + s + "\"";
    };
    const std::string cwd = fs::current_path(ec).string();   // where games/ mods/ saves/ are found
#if defined(_WIN32)
    const std::string cmd = "start " + quoted("BlockSmith Server - " + profile) + " /D " + quoted(cwd) + " " +
                            quoted(exe.string()) + " --config " + quoted(conf.string());
#else
    const std::string cmd = quoted(exe.string()) + " --config " + quoted(conf.string()) + " > " +
                            quoted((dir / "server.log").string()) + " 2>&1 < /dev/null &";
#endif
    std::cout << "[Servers] Starting '" << profile << "': " << cmd << std::endl;
    if (std::system(cmd.c_str()) != 0) {
        error = "Couldn't start the server program";
        return false;
    }
    return true;
}

// Asks a running server to save and stop (it checks for this file every second).
void stopServerProfile(const std::string& profile) {
    std::ofstream(serverProfilesRoot() / profile / "stop.request") << "stop\n";
}

void openFolderInExplorer(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string abs = fs::absolute(folder, ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    std::system(cmd.c_str());
}

struct SavedServer { std::string name, address; };

// servers.txt: one server per line, "name|address"
std::vector<SavedServer> loadServers() {
    std::vector<SavedServer> out;
    std::ifstream in(serversFile());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t bar = line.find('|');
        if (line.empty() || line[0] == '#' || bar == std::string::npos) continue;
        out.push_back({line.substr(0, bar), line.substr(bar + 1)});
    }
    return out;
}

void saveServers(const std::vector<SavedServer>& servers) {
    std::ofstream out(serversFile(), std::ios::trunc);
    if (!out) {
        std::cerr << "[Multiplayer] Could not write " << serversFile().string() << std::endl;
        return;
    }
    out << "# Saved servers (name|address). Edited from the Multiplayer screen.\n";
    for (const auto& s : servers) {
        std::string name = s.name, address = s.address;
        for (auto* str : {&name, &address}) {
            str->erase(std::remove_if(str->begin(), str->end(),
                                      [](char ch) { return ch == '|' || ch == '\n' || ch == '\r'; }),
                       str->end());
        }
        out << name << "|" << address << "\n";
    }
}

const KeyAction* findKeyAction(const std::string& id) {
    for (const auto& a : keyActions()) if (id == a.id) return &a;
    return nullptr;
}

// Key currently bound to an action (GLFW_KEY_UNKNOWN if the action doesn't exist).
int keyForAction(const std::string& action) {
    const KeyAction* a = findKeyAction(action);
    if (!a) return GLFW_KEY_UNKNOWN;
    return settingInt("key_" + action, a->defaultKey);
}

// True while the key bound to `action` is held. Use this instead of glfwGetKey for game controls.
bool isActionDown(GLFWwindow* window, const std::string& action) {
    const int key = keyForAction(action);
    if (isMouseKey(key)) return glfwGetMouseButton(window, key - kMouseKeyBase) == GLFW_PRESS;
    return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && glfwGetKey(window, key) == GLFW_PRESS;
}

std::string keyName(int key) {
    if (key == GLFW_KEY_UNKNOWN) return "None";
    if (isMouseKey(key)) {
        switch (key - kMouseKeyBase) {
            case GLFW_MOUSE_BUTTON_LEFT:   return "Left Mouse";
            case GLFW_MOUSE_BUTTON_RIGHT:  return "Right Mouse";
            case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle Mouse";
            default: return "Mouse " + std::to_string(key - kMouseKeyBase + 1);
        }
    }
    if (const char* n = glfwGetKeyName(key, 0)) {
        std::string s(n);
        for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return "Numpad " + std::to_string(key - GLFW_KEY_KP_0);
    switch (key) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_KP_ENTER:      return "Numpad Enter";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_PAGE_UP:       return "Page Up";
        case GLFW_KEY_PAGE_DOWN:     return "Page Down";
        default:                     return "Key " + std::to_string(key);
    }
}

bool setKeybind(const std::string& action, int key, std::string& error) {
    if (!findKeyAction(action)) { error = "Unknown action."; return false; }
    if (key == GLFW_KEY_ESCAPE) { error = "Escape is reserved for menus."; return false; }
    if (isMouseKey(key)) {
        // Controls the game reads every frame can be on a mouse button; the rest (chat, inventory,
        // hotbar slots...) are keyboard-only
        static const std::set<std::string> mouseOk = { "forward", "back", "left", "right", "jump",
                                                       "sneak", "sprint", "pick_block" };
        if (!mouseOk.count(action)) { error = "That control needs a keyboard key."; return false; }
        const int b = key - kMouseKeyBase;
        if (b == GLFW_MOUSE_BUTTON_LEFT || b == GLFW_MOUSE_BUTTON_RIGHT) {
            error = "Left and right mouse dig and place.";
            return false;
        }
    } else if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST) { error = "That key can't be used."; return false; }
    g_Settings["key_" + action] = std::to_string(key);
    saveSettings();
    return true;
}

void resetKeybinds() {
    for (auto it = g_Settings.begin(); it != g_Settings.end();) {
        if (it->first.rfind("key_", 0) == 0) it = g_Settings.erase(it);
        else ++it;
    }
    saveSettings();
}

// Video settings: value limits, current value, and applying them.
struct VideoSetting { const char* id; int minV, maxV, step; };
const VideoSetting kVideoSettings[] = {
    { "fov",             30, 110, 5 },
    { "render_distance",  2,  16, 1 },
    { "vsync",            0,   1, 1 },
    { "main_hand",        0,   1, 1 },   // 0 = right, 1 = left (left-handed players)
    { "volume_master",    0, 100, 5 },
    { "volume_music",     0, 100, 5 },
    { "volume_effects",   0, 100, 5 },
    { "volume_ambient",   0, 100, 5 },
};

const VideoSetting* findVideoSetting(const std::string& id) {
    for (const auto& s : kVideoSettings) if (id == s.id) return &s;
    return nullptr;
}

int currentVideoSetting(const std::string& id) {
    if (id == "fov") return static_cast<int>(std::lround(g_FieldOfView));
    if (id == "render_distance") return g_WorldInstance ? g_WorldInstance->renderDistance() : settingInt(id, 10);
    if (id == "vsync") return settingInt(id, 1);
    if (id == "main_hand") return settingInt(id, 0);
    if (id == "volume_music") return settingInt(id, 60);
    if (id.rfind("volume_", 0) == 0) return settingInt(id, 100);
    return 0;
}

void applyVideoSetting(const std::string& id, int value) {
    if (id == "fov") g_FieldOfView = static_cast<float>(value);
    else if (id == "render_distance") { if (g_WorldInstance) g_WorldInstance->setRenderDistance(value); }
    else if (id == "vsync") glfwSwapInterval(value ? 1 : 0);
    else if (id.rfind("volume_", 0) == 0) g_Audio.setVolume(id.substr(7), value / 100.0f);   // master, music...
}

bool setVideoSetting(const std::string& id, int value, std::string& error) {
    const VideoSetting* s = findVideoSetting(id);
    if (!s) { error = "Unknown setting."; return false; }
    value = std::clamp(value, s->minV, s->maxV);
    g_Settings[id] = std::to_string(value);
    saveSettings();
    applyVideoSetting(id, value);
    return true;
}

// Re-applies saved video settings (after mods, so the player's choices win).
void applyStoredVideoSettings() {
    for (const auto& s : kVideoSettings) {
        if (g_Settings.count(s.id)) applyVideoSetting(s.id, std::clamp(settingInt(s.id, s.minV), s.minV, s.maxV));
    }
}

// What the player's client mods may do right now: no limits in singleplayer or as a LAN host;
// on a server, what it says (ClientModRules.h). Settings: client_mods = false turns them off.
ClientModRules currentClientModRules() {
    ClientModRules r = ClientModRules::unlimited();
    if (g_Client.state() == GameClient::State::Joined) {
        const std::string v = g_Client.getShared(ClientModRules::kSharedKey);
        r = v.empty() ? ClientModRules() : ClientModRules::decode(v);   // a server that doesn't say: defaults
    }
    return r;
}
// The rules a LAN host gives players who join (settings.txt: lan_client_mods, lan_client_mods_chat,
// lan_client_mods_players, lan_client_mods_block_range). Friends: no limits unless changed.
ClientModRules lanClientModRules() {
    return ClientModRules::fromConfig(g_Settings, "lan_client_mods", ClientModRules::unlimited());
}

// Engine functions for client mods that read the world, held to the server's rules
// (block range, other players). Added to the client state's `client` table before mods load.
void addClientModWorldApi(ClientMods& cm) {
    sol::table client = cm.api();
    ClientMods* mods = &cm;
    auto near3 = [mods](int x, int y, int z) {
        const int r = mods->rules().blockRange;
        if (r <= 0) return true;
        const glm::vec3 d = glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) - g_PlayerPosition;
        return glm::dot(d, d) <= static_cast<float>(r) * static_cast<float>(r);
    };
    auto near2 = [mods](int x, int z) {
        const int r = mods->rules().blockRange;
        if (r <= 0) return true;
        const float dx = x + 0.5f - g_PlayerPosition.x, dz = z + 0.5f - g_PlayerPosition.z;
        return dx * dx + dz * dz <= static_cast<float>(r) * static_cast<float>(r);
    };
    auto blockName = [](uint8_t id) -> std::string {
        return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name;
    };

    // client.get_block(x, y, z) -> name, meta  (nil when not loaded or out of the allowed range)
    client["get_block"] = [near3, blockName](int x, int y, int z, sol::this_state ts) {
        sol::state_view lv(ts);
        const glm::ivec3 p(x, y, z);
        if (!g_WorldInstance || !near3(x, y, z) || (y >= 0 && y < CHUNK_HEIGHT && !g_WorldInstance->isLoaded(p))) {
            return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, sol::lua_nil));
        }
        return std::make_tuple(sol::make_object(lv, blockName(g_WorldInstance->getBlockAt(p))),
                               sol::make_object(lv, static_cast<int>(g_WorldInstance->getMetadataAt(p))));
    };
    // client.get_light_level(x, y, z) -> 0..15 like engine.get_light_level (nil out of range)
    client["get_light_level"] = [near3](int x, int y, int z, sol::this_state ts) -> sol::object {
        if (!g_WorldInstance || !near3(x, y, z)) return sol::make_object(ts, sol::lua_nil);
        const uint16_t l = g_WorldInstance->getLightAt(glm::ivec3(x, y, z));
        float day = 1.0f;
        if (g_LightingSystem) day = std::max(0.0f, std::min(1.0f, g_LightingSystem->getSunDirection().y * 2.0f + 0.5f));
        const int skyNow = std::max(0, light::sky(l) - static_cast<int>(std::lround((1.0f - day) * 11.0f)));
        return sol::make_object(ts, std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l)))));
    };
    // client.get_surface(x, z) -> y, name: the highest block in that column (for maps), or nil
    client["get_surface"] = [near2, blockName](int x, int z, sol::this_state ts) {
        sol::state_view lv(ts);
        const auto nil = std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, sol::lua_nil));
        if (!g_WorldInstance || !near2(x, z) || !g_WorldInstance->isLoaded(glm::ivec3(x, 0, z))) return nil;
        for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
            const uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
            if (id != 0) return std::make_tuple(sol::make_object(lv, y), sol::make_object(lv, blockName(id)));
        }
        return nil;
    };
    // client.get_block_color(name) -> r, g, b (0..1): the average colour of its top picture,
    // tinted like the block (for maps). Worked out once per block.
    client["get_block_color"] = [](const std::string& name) {
        static std::map<std::string, glm::vec3> cache;
        const auto it = cache.find(name);
        if (it != cache.end()) return std::make_tuple(it->second.r, it->second.g, it->second.b);
        glm::vec3 c(0.5f);
        auto& reg = BlockRegistry::getInstance();
        const uint8_t id = reg.getBlockId(name);
        if (id != 0) {
            const BlockProperty& b = reg.getBlock(id);
            int layer = b.faces[2].baseLayer;
            for (int f : { 4, 0, 1, 5, 3 }) if (layer < 0) layer = b.faces[f].baseLayer;
            c = b.defaultTint;
            if (layer >= 0 && layer < static_cast<int>(g_LayerSources.size())) {
                const TextureSource& src = g_LayerSources[layer];
                std::string file = packOverride(src.requested);
                if (file.empty()) file = src.resolved;
                int w = 0, h = 0, n = 0;
                if (unsigned char* px = file.empty() ? nullptr : stbi_load(file.c_str(), &w, &h, &n, 4)) {
                    double sum[3] = { 0, 0, 0 };
                    long count = 0;
                    for (int i = 0; i < w * h; ++i) {
                        if (px[i * 4 + 3] < 128) continue;
                        for (int k = 0; k < 3; ++k) sum[k] += px[i * 4 + k];
                        ++count;
                    }
                    stbi_image_free(px);
                    if (count > 0) {
                        glm::vec3 avg(sum[0] / count / 255.0, sum[1] / count / 255.0, sum[2] / count / 255.0);
                        c = b.faces[2].tintBase || b.faces[2].baseLayer < 0 ? avg * b.defaultTint : avg;
                    }
                }
            }
        }
        cache[name] = c;
        return std::make_tuple(c.r, c.g, c.b);
    };
    // client.get_players() -> { { name, x, y, z, yaw }, ... } other players (empty if the server says no)
    client["get_players"] = [mods](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table list = lv.create_table();
        if (!mods->rules().players) return list;
        const net::PlayerMap& others = g_Server.running() ? g_Server.players() : g_Client.players();
        int i = 1;
        for (const auto& [id, v] : others) {
            sol::table e = lv.create_table();
            e["name"] = v.name;
            e["x"] = v.position.x; e["y"] = v.position.y; e["z"] = v.position.z;
            e["yaw"] = v.yaw;
            list[i++] = e;
        }
        return list;
    };
    // client.get_server_info() -> { mode = "singleplayer" | "lan_host" | "server", address }
    client["get_server_info"] = [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        const bool joined = g_Client.state() == GameClient::State::Joined;
        t["mode"] = g_Server.running() ? "lan_host" : joined ? "server" : "singleplayer";
        if (joined) t["address"] = g_Client.address();
        else t["world"] = g_ActiveWorldName;
        return t;
    };
    // client.send_chat_message(text) -> true if sent: chat (or a /command) as if the player typed
    // it. Only if the server allows it.
    client["send_chat_message"] = [mods](const std::string& text) {
        if (!mods->rules().chat || !g_ChatSystem || text.empty()) return false;
        g_ChatSystem->sendText(text);
        return true;
    };
}

bool settingsOpen() {
    return g_CurrentGameState == GameState::SETTINGS ||
           (g_CurrentGameState == GameState::PLAYING && g_Paused && g_SettingsInGame);
}

void closeSettings() {
    if (g_CurrentGameState == GameState::SETTINGS) g_CurrentGameState = GameState::MENU;
    g_SettingsInGame = false;   // in-game: back to the pause menu
}


// engine.register_block(...): the shared definition (BlockDefs.cpp), with textures
// uploaded to the GPU here.
void registerBlockFromLua(const std::string& name, float r, float g, float b, sol::object texObj, sol::object overlayObj, sol::optional<sol::table> propsTable) {
    registerBlockDefinition(name, r, g, b, texObj, overlayObj, propsTable,
                            [](const std::string& path) { return loadTextureToLayer(path); });
}

uint8_t getBlockIdLua(const std::string& name) {
    return BlockRegistry::getInstance().getBlockId(name);
}

bool isWaterBlockName(const std::string& name) {
    return name == "water" || name == "flowing_water";
}

// engine.register_item_use(item, fn): what right-clicking with an item does (buckets etc.)
std::unordered_map<std::string, sol::protected_function> g_ItemUseHandlers;

// Is the block at p a full source of any fluid (water, lava, modded fluids)?
bool isFluidSourceAt(const World& world, const glm::ivec3& p) {
    FluidRegistry::get().ensureFresh();
    return FluidRegistry::get().cell(world.getBlockAt(p), world.getMetadataAt(p)).source;
}

// Walks the ray block by block (the blocks the ray passes through, in order) and returns
// the first fluid block, or nothing if a solid block or the distance comes first.
struct FluidRayHit { bool hit = false; glm::ivec3 pos{0}; };
FluidRayHit raycastFluid(const glm::vec3& origin, const glm::vec3& dir, float maxDist, const World& world) {
    FluidRayHit out;
    glm::ivec3 cell(static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z)));
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        step[a] = dir[a] > 0 ? 1 : (dir[a] < 0 ? -1 : 0);
        tDelta[a] = dir[a] != 0 ? std::abs(1.0f / dir[a]) : 1e30f;
        const float edge = dir[a] > 0 ? std::floor(origin[a]) + 1.0f - origin[a] : origin[a] - std::floor(origin[a]);
        tMax[a] = dir[a] != 0 ? edge * tDelta[a] : 1e30f;
    }
    for (float t = 0.0f; t <= maxDist;) {
        const uint8_t id = world.getBlockAt(cell);
        if (id != 0) {
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            if (FluidRegistry::get().forBlock(id)) { out.hit = true; out.pos = cell; return out; }
            if (prop.isSelectable || prop.isWalkable) return out;   // something solid in the way
        }
        const int a = (tMax.x < tMax.y) ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        t = tMax[a];
        tMax[a] += tDelta[a];
        cell[a] += step[a];
    }
    return out;
}

float underwaterDepth(const World& world, const glm::vec3& eye) {
    const int bx = static_cast<int>(std::floor(eye.x));
    const int bz = static_cast<int>(std::floor(eye.z));
    const int by = static_cast<int>(std::floor(eye.y));

    auto isWaterAt = [&](int y) {
        const uint8_t id = world.getBlockAt(glm::ivec3(bx, y, bz));
        return id != 0 && isWaterBlockName(BlockRegistry::getInstance().getBlock(id).name);
    };

    if (!isWaterAt(by)) return -1.0f;

    int top = by;
    for (int y = by + 1; y < by + 128 && isWaterAt(y); ++y) {
        top = y;
    }

    const float surfaceY = static_cast<float>(top) + 1.0f;
    return glm::max(surfaceY - eye.y, 0.0f);
}

// The water block's own colour: the average of its top texture (what you see on the surface),
// times its tint colour if the texture is tinted. Worked out once (again after a texture pack
// change). Shallow = that colour, deep = a darker version of it.
WaterColors waterBlockColors() {
    static WaterColors cached{ g_Underwater.shallowColor, g_Underwater.deepColor };
    static int cachedLayer = -2;
    static size_t cachedSources = 0;
    auto& reg = BlockRegistry::getInstance();
    const uint8_t id = reg.getBlockId("water");
    if (id == 0) return WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor };
    const BlockProperty& w = reg.getBlock(id);
    int layer = w.faces[2].baseLayer;
    for (int f = 0; layer < 0 && f < 6; ++f) layer = w.faces[f].baseLayer;
    if (layer == cachedLayer && cachedSources == g_LayerSources.size()) return cached;
    cachedLayer = layer;
    cachedSources = g_LayerSources.size();
    if (layer < 0 || layer >= static_cast<int>(g_LayerSources.size())) return cached;

    std::vector<unsigned char> px;
    const TextureSource& src = g_LayerSources[layer];
    const std::string override = packOverride(src.requested);
    bool ok = !override.empty() && loadImageScaled(override, TEX_SIZE, px);
    if (!ok && !src.resolved.empty()) ok = loadImageScaled(src.resolved, TEX_SIZE, px);
    if (!ok) return cached;
    glm::vec3 sum(0.0f);
    float weight = 0.0f;
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        const float a = px[i + 3] / 255.0f;
        sum += glm::vec3(px[i], px[i + 1], px[i + 2]) / 255.0f * a;
        weight += a;
    }
    if (weight <= 0.0f) return cached;
    glm::vec3 color = sum / weight;
    if (w.faces[2].tintBase) color *= w.defaultTint;
    cached = WaterColors{ color, color * 0.35f };
    std::cout << "[Underwater] Colour from the water block: " << color.r << ", " << color.g << ", " << color.b << std::endl;
    return cached;
}

WaterColors queryWaterColors(sol::state& lua, const glm::vec3& eye) {
    const WaterColors fallback = g_Underwater.colorsFromLua ? WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor }
                                                            : waterBlockColors();
    try {
        sol::function fn = lua["get_water_color"];
        if (!fn.valid()) return fallback;
        sol::table t = fn(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)));
        if (t.size() < 6) return fallback;
        return WaterColors{
            glm::vec3(t.get<float>(1), t.get<float>(2), t.get<float>(3)),
            glm::vec3(t.get<float>(4), t.get<float>(5), t.get<float>(6))
        };
    } catch (...) {
        return fallback;
    }
}

void HeldLight::update(const World& w) {
    if (!g_LightingSystem) return;
    uint8_t held = 0;
    if (g_LightSettings.heldLight && g_Hotbar && w.lighting()) held = g_Hotbar->getActiveBlockId();
    const light::BlockLight& bl = light::blockLight(held);
    if (!bl.emits()) {
        if (id >= 0) { g_LightingSystem->removePointLight(id); id = -1; }
        return;
    }
    const int level = std::max<int>(bl.emit[0], std::max<int>(bl.emit[1], bl.emit[2]));
    const glm::vec3 color(bl.emit[0] / 15.0f, bl.emit[1] / 15.0f, bl.emit[2] / 15.0f);
    const glm::vec3 pos = g_PlayerPosition + glm::vec3(0.0f, 1.2f, 0.0f);
    const float radius = 1.0f + level * 0.6f;
    const auto& all = g_LightingSystem->getPointLights();
    if (id >= static_cast<int>(all.size()) || (id >= 0 && !all[id].active)) id = -1;   // cleared by sky.reset
    if (id < 0) id = g_LightingSystem->addPointLight(pos, color, radius, 0.9f);
    else g_LightingSystem->updatePointLight(id, pos, color, radius, 0.9f);
}

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTintColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aBaseLayer;
layout (location = 4) in float aOverlayLayer;
layout (location = 5) in float aTintBase;
layout (location = 6) in float aTintOverlay;
layout (location = 7) in float aFaceShading;
layout (location = 8) in vec4 aLight;   // sunlight, red, green, blue (0..1), see Light.h

out vec3 TintColor;
out vec4 VoxelLight;
out vec2 TexCoord;
out float BaseLayer;
out float OverlayLayer;
out float TintBase;
out float TintOverlay;
out float LightIntensity;
out vec3 FragPos;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_SunDir;
uniform int u_Unlit;
uniform float u_Time;

void main() {
    vec3 pos = aPos;
    TintColor = aTintColor;
    TexCoord = aTexCoord;
    BaseLayer = aBaseLayer;
    OverlayLayer = aOverlayLayer;
    TintBase = aTintBase;
    TintOverlay = aTintOverlay;

    WorldPos = (model * vec4(pos, 1.0)).xyz;

    if (u_Unlit == 1) {
        LightIntensity = 1.0f;
    } else {
        // Face direction and ambient occlusion; the light itself comes per corner (aLight)
        LightIntensity = clamp(aFaceShading, 0.2f, 1.0f);
    }
    VoxelLight = aLight;

    vec4 viewPos = view * model * vec4(pos, 1.0);
    FragPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 TintColor;
in vec2 TexCoord;
in float BaseLayer;
in float OverlayLayer;
in float TintBase;
in float TintOverlay;
in float LightIntensity;
in vec3 FragPos;
in vec3 WorldPos;
in vec4 VoxelLight;

out vec4 FragColorOut;
uniform sampler2DArray textureArray;
uniform int u_IsGUI;
uniform int u_Unlit;
uniform vec3 u_SunDir;
uniform float u_Time;

struct PointLight {
    vec3 position;
    vec3 color;
    float radius;
    float intensity;
};
uniform int u_NumPointLights;
uniform PointLight u_PointLights[16];

uniform int   u_FogEnabled;
uniform int   u_FogExp;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform float u_FogDensity;
uniform vec3  u_FogColor;

uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_WeatherLight;
uniform float u_ChunkFade;

// Block light (Light.h). Chunks carry light per corner; anything else drawn with this shader
// (other players, particles...) uses the light where the camera is.
uniform int   u_VoxelLight;       // 1 = use the vertex light (chunks)
uniform vec4  u_FallbackLight;    // sun, r, g, b (0..1) for everything else
uniform float u_MinLight;         // the darkest anything gets (sky.set_min_light)
uniform float u_Brightness;       // 0 = moody, 1 = bright (sky.set_brightness, like Minecraft's slider)
uniform float u_BlockLightScale;  // how strong block light is (sky.set_block_light_strength)

// Light level (0..1) -> brightness: Minecraft's curve (each level ~20% dimmer), lifted by u_Brightness
float lightCurve(float l) {
    // Clamped: blending across a triangle can give a hair over 1, and pow() of a negative
    // number is NaN on most GPUs (drawn as black specks on brightly lit surfaces)
    l = clamp(l, 0.0, 1.0);
    float v = l / (4.0 - 3.0 * l);
    float inv = clamp(1.0 - v, 0.0, 1.0);
    float lifted = 1.0 - inv * inv * inv * inv;
    return mix(v, lifted, u_Brightness);
}

void main() {
    vec4 finalColor = vec4(TintColor, 1.0);

    if (u_Unlit == 0 && u_IsGUI == 0 && u_ChunkFade < 1.0) {
        float threshold = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        if (threshold > u_ChunkFade) discard;
    }

    if (BaseLayer >= 0.0) {
        vec2 animatedUV = TexCoord;

        if (TintColor.b > TintColor.r && TintColor.b > TintColor.g && BaseLayer > 0.0 && u_IsGUI == 0) {
            animatedUV.x += sin(u_Time * 1.2f + WorldPos.z * 0.5f) * 0.02f;
            animatedUV.y += u_Time * 0.08f;
        }

        vec4 baseTex = texture(textureArray, vec3(animatedUV, BaseLayer));
        if (baseTex.a < 0.05) discard;

        vec3 baseRgb = (TintBase > 0.5) ? (baseTex.rgb * TintColor) : baseTex.rgb;
        finalColor = vec4(baseRgb, baseTex.a);

        if (OverlayLayer >= 0.0) {
            vec4 overlayTex = texture(textureArray, vec3(TexCoord, OverlayLayer));
            if (overlayTex.a > 0.05) {
                vec3 overlayRgb = (TintOverlay > 0.5) ? (overlayTex.rgb * TintColor) : overlayTex.rgb;
                finalColor.rgb = mix(finalColor.rgb, overlayRgb, overlayTex.a);
            }
        }
    }

    if (u_IsGUI == 1) {
        FragColorOut = finalColor;
        return;
    }

    vec3 dayLightColor = vec3(1.0f, 0.98f, 0.92f);
    vec3 sunsetLightColor = vec3(1.0f, 0.62f, 0.32f);
    vec3 nightLightColor = vec3(0.35f, 0.5f, 0.75f);

    vec3 currentSunLightColor = mix(sunsetLightColor, dayLightColor, clamp(u_SunDir.y + 0.15f, 0.0f, 1.0f));
    if (u_SunDir.y < 0.0f) {
        currentSunLightColor = mix(sunsetLightColor, nightLightColor, clamp(-u_SunDir.y * 2.0f, 0.0f, 1.0f));
    }

    vec3 shadedColor;
    if (u_Unlit == 1) {
        shadedColor = finalColor.rgb * LightIntensity * currentSunLightColor;
    } else {
        vec4 L = (u_VoxelLight == 1) ? VoxelLight : u_FallbackLight;
        // Sunlight follows the time of day and the weather (sky.set_light); block light doesn't
        float daylight = clamp(u_SunDir.y * 0.6f + 0.4f, 0.28f, 1.0f);
        vec3 sunPart = lightCurve(L.x) * daylight * currentSunLightColor * u_WeatherLight;
        vec3 blockPart = vec3(lightCurve(L.y), lightCurve(L.z), lightCurve(L.w)) * u_BlockLightScale;
        // Screen blend: torches still show at dusk, and coloured lights mix
        vec3 lightAmount = vec3(1.0f) - (vec3(1.0f) - sunPart) * (vec3(1.0f) - min(blockPart, vec3(1.0f)));
        lightAmount = max(lightAmount, vec3(u_MinLight));
        shadedColor = finalColor.rgb * LightIntensity * lightAmount;
    }

    vec3 pointLightContribution = vec3(0.0f);
    for(int i = 0; i < u_NumPointLights && i < 16; i++) {
        float dist = distance(WorldPos, u_PointLights[i].position);
        if(dist < u_PointLights[i].radius) {
            float attenuation = 1.0f - (dist / u_PointLights[i].radius);
            attenuation = attenuation * attenuation;
            pointLightContribution += u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        }
    }
    shadedColor += finalColor.rgb * pointLightContribution;

    if (u_Unlit == 0) {
        shadedColor *= mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    }

    float dist = length(FragPos);
    float fogFactor = 1.0;
    if (u_FogEnabled == 1) {
        if (u_FogExp == 1) {
            float d = dist * u_FogDensity;
            fogFactor = exp(-d * d);
        } else {
            fogFactor = clamp((u_FogEnd - dist) / max(u_FogEnd - u_FogStart, 0.001), 0.0, 1.0);
        }
    }

    vec3 finalRgb = mix(u_FogColor, shadedColor, fogFactor);
    FragColorOut = vec4(finalRgb, finalColor.a);
}
)";

const char* sunVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* sunFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D u_Texture;
uniform int u_HasTexture;

void main() {
    if (u_HasTexture == 1) {
        vec4 texColor = texture(u_Texture, TexCoord);
        if (texColor.a < 0.1) discard;
        FragColor = texColor;
    } else {
        FragColor = vec4(1.0, 0.95, 0.6, 1.0);
    }
}
)";

const char* wireframeVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* wireframeFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 u_Color;   // outline: black; survival digging: a darkening box
void main() {
    FragColor = u_Color;
}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 FragColor;
void main() {
    FragColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec4 FragColor;
out vec4 finalColor;
void main() {
    finalColor = FragColor;
}
)";

const char* texturedUIVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* texturedUIFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D u_Texture;
void main() {
    vec4 texColor = texture(u_Texture, TexCoords);
    if (texColor.a < 0.01) discard;
    FragColor = texColor;
}
)";

const char* fontVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
uniform mat4 projection;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* fontFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D fontTexture;
uniform vec4 textColor;
void main() {
    float alpha = texture(fontTexture, TexCoords).r;
    if (alpha < 0.05) discard;
    FragColor = vec4(textColor.rgb, textColor.a * alpha);
}
)";

const char* modelVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* modelFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture_diffuse1;
uniform vec3 u_LightDir;
uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_WeatherLight;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -u_LightDir), 0.2f);
    vec4 texColor = texture(texture_diffuse1, TexCoords);
    if(texColor.a < 0.1) discard;

    vec3 litColor = texColor.rgb * diff * u_WeatherLight * mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    FragColor = vec4(litColor, texColor.a);
}
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;
    glViewport(0, 0, width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_ChatSystem && g_ChatSystem->isOpen) return;
    EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->player) return;

    if (settingsOpen()) {
        callLuaUI(ctx->lua, "handle_settings_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::WORLD_SELECT) {
        callLuaUI(ctx->lua, "handle_world_select_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::MULTIPLAYER) {
        callLuaUI(ctx->lua, "handle_multiplayer_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::MODS) {
        callLuaUI(ctx->lua, "handle_mods_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
        callLuaUI(ctx->lua, "handle_texture_packs_scroll", yoffset);
        return;
    }

    if (g_Inventory.isCreativeOpen()) {
        // Creative inventory open: scroll its item grid (handled in inventory.lua)
        g_Inventory.handleCreativeScroll(yoffset);

        try {
            sol::state* lua = ctx->lua;
            if (lua) {
                sol::function scrollLua = (*lua)["on_scroll_input"];
                if (scrollLua.valid()) scrollLua(xoffset, yoffset);
            }
        } catch (...) {}
        return;
    }

    if (g_Hotbar) {
        if (yoffset > 0) g_Hotbar->previousSlot();
        else if (yoffset < 0) g_Hotbar->nextSlot();
    }
}

// Puts OpenGL into a clean state for 2D UI and text (menus, loading screen, chat).
// Lua menu text (ui.draw_text / ui.text_width / ui.text_height) should follow the menus' own
// scale, not the window size. FontRenderer stretches text with the window (relative to its
// 1920x1080 base), so cancel that out here. kUiTextSize = how big scale 1.0 looks: the font
// is baked at 32 px, so 2/3 gives ~21 px text (capitals ~14 px, letters ~10 px wide).
constexpr float kUiTextSize = 2.0f / 3.0f;
constexpr float kFontBaseW = 1920.0f, kFontBaseH = 1080.0f;   // FontRenderer's base resolution

glm::vec2 uiTextScale(float scale, const Dimensions2D& fb) {
    const float w = fb.width > 0 ? static_cast<float>(fb.width) : kFontBaseW;
    const float h = fb.height > 0 ? static_cast<float>(fb.height) : kFontBaseH;
    return glm::vec2(scale * kUiTextSize * kFontBaseW / w, scale * kUiTextSize * kFontBaseH / h);
}

void resetUIRenderState() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindVertexArray(0);
}

void drawLoadingScreen(GLuint uiProgram, GLuint fontProgram, float progress, int width, int height) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    auto quad = [&](float x0, float y0, float x1, float y1, glm::vec4 c) {
        const float v[36] = {
            x0, y0, c.r, c.g, c.b, c.a,  x1, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,
            x0, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,  x0, y1, c.r, c.g, c.b, c.a
        };
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glUseProgram(uiProgram);
    quad(-1.0f, -1.0f, 1.0f, 1.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));
    quad(-0.40f, -0.12f, 0.40f, -0.06f, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
    const float fillX = -0.40f + 0.80f * glm::clamp(progress, 0.0f, 1.0f);
    quad(-0.40f, -0.12f, fillX, -0.06f, glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));
    glBindVertexArray(0);

    if (g_FontRenderer) {
        const Dimensions2D screen{width, height};
        glUseProgram(fontProgram);
        g_FontRenderer->renderText(fontProgram, "Loading world...",
                                   width * 0.5f - 120.0f, height * 0.5f + 60.0f,
                                   1.2f, 1.2f, glm::vec4(1.0f), screen);
        const int percent = static_cast<int>(glm::clamp(progress, 0.0f, 1.0f) * 100.0f);
        g_FontRenderer->renderText(fontProgram, std::to_string(percent) + "%",
                                   width * 0.5f - 20.0f, height * 0.5f - 60.0f,
                                   0.9f, 0.9f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), screen);
        if (g_SpawnChunkWaiting) {
            const glm::vec4 warn(1.0f, 0.8f, 0.4f, 1.0f);
            const std::string a = "The ground where you are can't be loaded yet - still trying.";
            const std::string b = "Press Esc to go back to the menu.";
            g_FontRenderer->renderText(fontProgram, a, width * 0.5f - g_FontRenderer->textWidth(a, 0.8f, screen) * 0.5f,
                                       height * 0.5f - 120.0f, 0.8f, 0.8f, warn, screen);
            g_FontRenderer->renderText(fontProgram, b, width * 0.5f - g_FontRenderer->textWidth(b, 0.8f, screen) * 0.5f,
                                       height * 0.5f - 160.0f, 0.8f, 0.8f, warn, screen);
        }
    }
}

int main(int argc, char** argv) {
    // Remember where the game's .exe is: BlockSmithServer is built next to it (My Servers screen)
    if (argc > 0 && argv[0]) {
        std::error_code exeEc;
        const fs::path exe = fs::absolute(fs::path(argv[0]), exeEc);
        if (!exeEc) g_ExeDir = exe.parent_path();
    }
    try {
        if (!glfwInit()) return -1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Window title: the game being played, on the BlockSmith engine
        std::string windowTitle = "BlockSmith";
        for (const auto& g : getAvailableGames()) {
            if (g.folderName == g_ActiveGameFolder) windowTitle = g.title + " - BlockSmith";
        }
        GLFWwindow* window = glfwCreateWindow(1280, 720, windowTitle.c_str(), NULL, NULL);
        if (!window) { glfwTerminate(); return -1; }
        // The window's icon is always BlockSmith's. On Windows it comes from the .exe (the
        // GLFW_ICON resource in BlockSmith.rc); elsewhere from icons/blocksmith.png.
#ifndef _WIN32
        {
            const std::vector<std::string> iconFiles = { (g_ExeDir / "icons/blocksmith.png").string(),
                                                         "icons/blocksmith.png", "../../icons/blocksmith.png" };
            for (const auto& f : iconFiles) {
                int iw = 0, ih = 0, ic = 0;
                stbi_set_flip_vertically_on_load(false);
                unsigned char* pixels = stbi_load(f.c_str(), &iw, &ih, &ic, 4);
                if (!pixels) continue;
                GLFWimage icon{ iw, ih, pixels };
                glfwSetWindowIcon(window, 1, &icon);
                stbi_image_free(pixels);
                break;
            }
        }
#endif
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // vsync (1 = once per screen refresh)

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Error] Failed to initialize GLAD\n";
            return -1;
        }

        // Closes the window LAST. Everything that owns OpenGL objects (the hotbar, fonts, sky,
        // and locals declared below such as the world renderer and particles) deletes them in
        // its destructor, which needs the context to still exist. Locals are destroyed in reverse
        // order, so this one - made before all of them - goes after them. (Closing the window
        // first made quitting the game crash in ~Hotbar.)
        struct GlShutdown {
            GLFWwindow* window;
            ~GlShutdown() {
                g_ModelInstances.clear();   // placed models free their meshes (OpenGL buffers) here
                delete g_Hotbar;         g_Hotbar = nullptr;
                delete g_FontRenderer;   g_FontRenderer = nullptr;
                delete g_ChatSystem;     g_ChatSystem = nullptr;
                delete g_LightingSystem; g_LightingSystem = nullptr;
                delete g_Skybox;         g_Skybox = nullptr;
                delete g_Clouds;         g_Clouds = nullptr;
                delete g_MainMenu;       g_MainMenu = nullptr;
                glfwDestroyWindow(window);
                glfwTerminate();
            }
        } glShutdown{ window };

        // Which graphics card we actually got (see NvOptimusEnablement at the top of this file)
        {
            const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            g_GpuName = renderer ? renderer : "unknown";
            std::cout << "[Graphics] GPU: " << g_GpuName << " (" << (vendor ? vendor : "?") << ", OpenGL "
                      << (version ? version : "?") << ")" << std::endl;
        }

        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        g_Hotbar = new Hotbar();
        g_FontRenderer = new FontRenderer(baseResolution);
        g_ChatSystem = new ChatSystem();
        g_LightingSystem = new LightingSystem();
        g_Skybox = new Skybox();
        g_Skybox->init();
        g_Clouds = new Clouds();
        g_Clouds->init();
        if (!g_Weather.init()) std::cerr << "[Weather] Couldn't set up weather effects" << std::endl;
        g_Audio.init();   // no sound card: the game runs silently
        for (const char* group : { "master", "music", "effects", "ambient" }) {
            const std::string id = std::string("volume_") + group;
            g_Audio.setVolume(group, currentVideoSetting(id) / 100.0f);
        }
        g_MainMenu = new MainMenu();

        // The game's font, else one that comes with the operating system. With neither, there is
        // no text at all (menus would be blank buttons), so say why - in the console and in the
        // window's title bar, where the player sees it - and carry on with text switched off. The
        // renderer stays (some screens draw through it unconditionally): without an atlas
        // (ready() is false) it draws nothing and measures every text as 0 wide.
        if (!g_FontRenderer->init("fonts/PixeloidSans.ttf")) {
            std::cerr << "[Font] fonts/PixeloidSans.ttf couldn't be used - trying the system's fonts" << std::endl;
            if (!g_FontRenderer->init(FontRenderer::systemFonts())) {
                std::cerr << "[Font Error] No font could be loaded: the game will show no text. Check that the "
                             "'fonts' folder is next to the game (with PixeloidSans.ttf in it)." << std::endl;
                glfwSetWindowTitle(window, (windowTitle + " - no font found: is the 'fonts' folder missing?").c_str());
            }
        }

        EngineContext ctx;
        glfwSetWindowUserPointer(window, &ctx);

        glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleCharInput(codepoint);
                return;
            }

            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));

            // Settings screen text fields (player name)
            if (settingsOpen()) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_settings_char", static_cast<int>(codepoint), ch);
                return;
            }

            // Creative inventory search box (inventory.lua decides if it wants the character)
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeChar(codepoint)) {
                return;
            }

            // Multiplayer screen text fields (player name, server address)
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_multiplayer_char", static_cast<int>(codepoint), ch);
                return;
            }

            // World select screen text fields (world name / seed)
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_world_select_char", static_cast<int>(codepoint), ch);
                return;
            }

            if (c && c->lua) {
                try {
                    sol::state* lua = c->lua;
                    sol::function charLua = (*lua)["on_char_input"];
                    if (charLua.valid()) charLua(codepoint);
                } catch (...) {}
            }
        });

        glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
            if (!c) return;

            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleKeyInput(key, scancode, action, mods, win);
                return;
            }

            // Settings (title screen or pause menu): Lua gets every key first, so it can
            // capture a key for rebinding. Otherwise Escape closes the settings.
            if (settingsOpen()) {
                if (callLuaUI(c->lua, "handle_settings_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) closeSettings();
                return;
            }

            // While typing in the creative search box, inventory.lua gets the keys first
            // (so 'E', 'T' or Escape don't close the inventory / open chat mid-typing).
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeKey(key, action, mods)) {
                return;
            }

            // World select: Lua gets the key first; otherwise Escape goes back to the title
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                if (callLuaUI(c->lua, "handle_world_select_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Multiplayer screen: same idea
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                if (callLuaUI(c->lua, "handle_multiplayer_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Mods screen: same idea
            if (g_CurrentGameState == GameState::MODS) {
                if (callLuaUI(c->lua, "handle_mods_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Texture packs screen
            if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                if (callLuaUI(c->lua, "handle_texture_packs_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            if (g_CurrentGameState == GameState::MENU && action == GLFW_PRESS) {
                if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, true);
                return;
            }
            // Stuck loading (the ground under the player can't be loaded): Esc leaves the world
            if (g_CurrentGameState == GameState::LOADING && g_SpawnChunkWaiting && action == GLFW_PRESS &&
                key == GLFW_KEY_ESCAPE) {
                g_SpawnChunkWaiting = false;
                sol::protected_function quit = (*c->lua)["engine"]["quit_to_menu"];
                if (quit.valid()) quit();
                return;
            }

            // Client mods see the keys pressed while playing (they can't take them away)
            if (g_CurrentGameState == GameState::PLAYING && g_ClientMods && !g_Paused &&
                !(g_ChatSystem && g_ChatSystem->isOpen) && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                g_ClientMods->key(key, action, mods);
            }

            if (g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS) {
                // While paused, only Escape does anything (it resumes the game)
                if (g_Paused) {
                    if (key == GLFW_KEY_ESCAPE) {
                        g_Paused = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                    return;
                }

                // Keys come from the Controls settings (see keyActions())
                if (key == keyForAction("chat") || key == keyForAction("command")) {
                    // The chat system expects T (plain chat) or Slash (starts with "/")
                    g_ChatSystem->openChat(win, key == keyForAction("command") ? GLFW_KEY_SLASH : GLFW_KEY_T);
                    return;
                }

                if (key == keyForAction("third_person")) {
                    g_ThirdPerson = !g_ThirdPerson;
                    return;
                }

                if (key == keyForAction("debug_info")) {
                    g_ShowDebugInfo = !g_ShowDebugInfo;
                    return;
                }

                if (key == keyForAction("inventory")) {
                    // Creative, with a mod's screen open (e.g. the survival inventory, opened from the
                    // creative one's Inventory tab): E just closes it
                    if (g_Creative && g_LuaInventoryOpen && !g_Inventory.isCreativeOpen()) {
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                        g_LuaInventoryOpen = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    // Survival: no creative menu (free blocks); a mod may show its own inventory
                    if (!g_Creative && !g_Inventory.isCreativeOpen()) {
                        // toggle_main_inventory() -> true if it's now open
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) {
                            auto r = toggleInv();
                            g_LuaInventoryOpen = r.valid() && r.get_type() == sol::type::boolean && r.get<bool>();
                            glfwSetInputMode(win, GLFW_CURSOR, g_LuaInventoryOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                        }
                        return;
                    }
                    // Use native C++ inventory toggle & cursor update
                    g_Inventory.toggleCreative();
                    int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                    if (currentMode == GLFW_CURSOR_DISABLED) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    else glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                    try {
                        sol::state* lua = c->lua;
                        sol::function toggleInv = (*lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                    } catch (...) {}
                    return;
                }

                if (key == GLFW_KEY_ESCAPE) {
                    // Escape closes the inventory first; otherwise it opens the pause menu
                    if (g_LuaInventoryOpen) {
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                        g_LuaInventoryOpen = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    if (g_Inventory.isCreativeOpen()) {
                        g_Inventory.setCreativeOpen(false);
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    g_Paused = true;
                    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        });

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        initTextureArray();

        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);
        // The player's client mods: their own Lua state, made after (so gone before) the game's
        ClientMods clientMods;
        struct ClientModsPointer {
            explicit ClientModsPointer(ClientMods* m) { g_ClientMods = m; }
            ~ClientModsPointer() { g_ClientMods = nullptr; }
        } clientModsPointer(&clientMods);

        auto compileShader = [](GLenum type, const char* src) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            return shader;
        };

        GLuint uiProgram = glCreateProgram();
        glAttachShader(uiProgram, compileShader(GL_VERTEX_SHADER, uiVertexShaderSource));
        glAttachShader(uiProgram, compileShader(GL_FRAGMENT_SHADER, uiFragmentShaderSource));
        glLinkProgram(uiProgram);

        GLuint texturedUIProgram = glCreateProgram();
        glAttachShader(texturedUIProgram, compileShader(GL_VERTEX_SHADER, texturedUIVertexShaderSource));
        glAttachShader(texturedUIProgram, compileShader(GL_FRAGMENT_SHADER, texturedUIFragmentShaderSource));
        glLinkProgram(texturedUIProgram);

        GLuint fontProgram = glCreateProgram();
        glAttachShader(fontProgram, compileShader(GL_VERTEX_SHADER, fontVertexShaderSource));
        glAttachShader(fontProgram, compileShader(GL_FRAGMENT_SHADER, fontFragmentShaderSource));
        glLinkProgram(fontProgram);

        GLuint modelShaderProgram = glCreateProgram();
        glAttachShader(modelShaderProgram, compileShader(GL_VERTEX_SHADER, modelVertexShaderSource));
        glAttachShader(modelShaderProgram, compileShader(GL_FRAGMENT_SHADER, modelFragmentShaderSource));
        glLinkProgram(modelShaderProgram);
        glUseProgram(modelShaderProgram);
        glUniform1f(glGetUniformLocation(modelShaderProgram, "u_WeatherLight"), 1.0f);   // full light until weather says otherwise

        // ------------------------------------------------------------------
        // ui.* table
        // ------------------------------------------------------------------
        auto uiTable = lua.create_named_table("ui");
        // ui.draw_image(path, x, y, w, h [, u0, v0, u1, v1]) - a picture (png) on screen, in pixels.
        //   u0..v1 pick part of the image (0..1), e.g. one icon from a sheet. Found in the
        //   mod's folder / textures/ like block textures. Loaded once, then cached.
        uiTable["draw_image"] = [texturedUIProgram](const std::string& path, float x, float y, float w, float h,
                                                     sol::optional<float> u0, sol::optional<float> v0,
                                                     sol::optional<float> u1, sol::optional<float> v1) {
            static std::unordered_map<std::string, GLuint> cache;
            static GLuint vao = 0, vbo = 0;
            auto it = cache.find(path);
            if (it == cache.end()) it = cache.emplace(path, loadStandardTexture(path)).first;
            if (it->second == 0 || w <= 0.0f || h <= 0.0f) return;
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const float sw = static_cast<float>(vp[2]), sh = static_cast<float>(vp[3]);
            if (sw <= 0 || sh <= 0) return;
            const float x0 = x / sw * 2.0f - 1.0f, x1 = (x + w) / sw * 2.0f - 1.0f;
            const float y0 = 1.0f - (y + h) / sh * 2.0f, y1 = 1.0f - y / sh * 2.0f;
            const float a = u0.value_or(0.0f), b = v0.value_or(0.0f), c = u1.value_or(1.0f), d = v1.value_or(1.0f);
            const float verts[24] = { x0, y0, a, d,  x1, y0, c, d,  x1, y1, c, b,
                                      x0, y0, a, d,  x1, y1, c, b,  x0, y1, a, b };
            if (vao == 0) {
                glGenVertexArrays(1, &vao);
                glGenBuffers(1, &vbo);
                glBindVertexArray(vao);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
                glEnableVertexAttribArray(1);
            }
            glUseProgram(texturedUIProgram);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, it->second);
            glUniform1i(glGetUniformLocation(texturedUIProgram, "u_Texture"), 0);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        };

        // ui.draw_button(x, y, w, h [, state [, danger]]) -> true if drawn: the game's button picture
        //   (menu/button.png, else mods/base/menu/button.png, else the engine's built-in one),
        //   9-slice, like the title screen. state: "normal", "hover", "disabled" or "selected".
        //   danger = true tints it red (delete buttons). Draw the label on top yourself.
        uiTable["draw_button"] = [texturedUIProgram](float x, float y, float w, float h,
                                                      sol::optional<std::string> state, sol::optional<bool> danger) {
            if (!g_MainMenu) return false;
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const std::string st = state.value_or("normal");
            const int code = st == "hover" ? 1 : st == "disabled" ? 2 : st == "selected" ? 3 : 0;
            return g_MainMenu->drawButton(texturedUIProgram, x, y, w, h, code, danger.value_or(false), vp[2], vp[3]);
        };
        // ui.get_button_text_color([hovered]) -> r, g, b: the label colour the game asked for in game.conf
        uiTable["get_button_text_color"] = [](sol::optional<bool> hovered) {
            const glm::vec4 c = g_MainMenu ? g_MainMenu->buttonTextColor(hovered.value_or(false))
                                           : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
            return std::make_tuple(c.r, c.g, c.b);
        };

        uiTable["get_mouse_pos"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int framebufferWidth, framebufferHeight;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            scaleCursorToFramebuffer(window, x, y, framebufferWidth, framebufferHeight);
            return std::make_tuple(x, y);
        };

        // Adds ui.draw_rect, ui.draw_block_icon, ui.get_all_blocks, ui.get_block_info,
        // ui.get_time, ui.keys, ui.actions (and a 0-based hotbar.* that gets replaced below).
        g_Inventory.registerLuaBindings(lua);

        // ui.draw_text - uses the real font shader. Supports both call styles:
        //   ui.draw_text(text, x, y, r, g, b)                    (old style, scale 1)
        //   ui.draw_text(text, x, y, scale, r, g, b [, a])       (new style)
        uiTable["draw_text"] = [&](const std::string& text, float x, float y, sol::variadic_args va) {
            if (!g_FontRenderer) return;

            std::vector<float> v;
            for (auto arg : va) v.push_back(arg.as<float>());

            float scale = 1.0f, r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            if (v.size() == 3) {
                r = v[0]; g = v[1]; b = v[2];
            } else if (v.size() >= 4) {
                scale = v[0]; r = v[1]; g = v[2]; b = v[3];
                if (v.size() >= 5) a = v[4];
            }

            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);

            resetUIRenderState();
            glUseProgram(fontProgram);
            const glm::vec2 s = uiTextScale(scale, windowSize);   // same size at any window size
            g_FontRenderer->renderText(fontProgram, text, x, y, s.x, s.y, glm::vec4(r, g, b, a), windowSize);
        };

        // ui.text_width(text, scale) -> exact width in pixels that ui.draw_text will use.
        // ui.text_height(scale)     -> height of capital letters above the baseline, in pixels.
        // Menus should measure with these instead of guessing letter sizes.
        uiTable["text_width"] = [window](const std::string& text, sol::optional<float> scale) -> float {
            if (!g_FontRenderer) return 0.0f;
            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
            return g_FontRenderer->textWidth(text, uiTextScale(scale.value_or(1.0f), windowSize).x, windowSize);
        };
        uiTable["text_height"] = [window](sol::optional<float> scale) -> float {
            if (!g_FontRenderer) return 0.0f;
            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
            return g_FontRenderer->capHeight(uiTextScale(scale.value_or(1.0f), windowSize).y, windowSize);
        };

        // ui.draw_server_icon(address, x, y, size) -> true if drawn, false if that server has no icon
        uiTable["draw_server_icon"] = [window, texturedUIProgram](const std::string& address,
                                                                  float x, float y, float size) {
            const ServerStatus* st = g_Pinger.status(address);
            if (!st || st->icon.empty()) return false;
            const GLuint tex = serverIconTexture(address, st->icon);
            if (!tex) return false;
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW <= 0 || fbH <= 0) return false;
            resetUIRenderState();
            drawUITexture(texturedUIProgram, tex, x, y, size, size, fbW, fbH);
            return true;
        };

        // ui.get_mouse() -> x, y, left_down, right_down   (framebuffer pixels, same as clicks)
        uiTable["get_mouse"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            scaleCursorToFramebuffer(window, x, y, fw, fh);
            const bool left  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            const bool right = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            return std::make_tuple(x, y, left, right);
        };

        lua.new_usertype<Bone>("Bone",
            "set_rotation", [](Bone& b, float x, float y, float z) { b.rotation = glm::vec3(x, y, z); },
            "get_rotation", [](Bone& b) { return std::make_tuple(b.rotation.x, b.rotation.y, b.rotation.z); },
            "set_position", [](Bone& b, float x, float y, float z) { b.position = glm::vec3(x, y, z); }
        );

        lua.new_usertype<AnimatedModel>("Model",
            "get_bone", &AnimatedModel::getBone,
            // model:reset_pose() - every bone back to the pose in the model file
            "reset_pose", &AnimatedModel::resetPose
        );

        // ---- Animations (Bedrock / Blockbench .animation.json) ----
        // local anims = engine.load_animations("player.animation.json")   (mod folder or its models/)
        // local walk = anims:get("animation.player.walk")   (or the short "walk", or nil/"" = the first)
        // walk:apply(model, time [, weight])   pose the model at `time` seconds; weight 0..1 blends
        // walk:get_length(), walk:is_looping(), walk:set_loop(bool), walk:get_name(), anims:names()
        lua.new_usertype<ModelAnimation>("Animation",
            "apply", [](ModelAnimation& a, std::shared_ptr<AnimatedModel> model, float time, sol::optional<float> weight) {
                if (model) a.apply(*model, time, weight.value_or(1.0f));
            },
            "get_length", [](ModelAnimation& a) { return a.length; },
            "is_looping", [](ModelAnimation& a) { return a.loop; },
            "set_loop", [](ModelAnimation& a, bool on) { a.loop = on; },
            "get_name", [](ModelAnimation& a) { return a.name; }
        );
        lua.new_usertype<AnimationSet>("AnimationSet",
            "get", [](AnimationSet& s, sol::optional<std::string> name) { return s.get(name.value_or("")); },
            "names", [](AnimationSet& s) { return sol::as_table(s.names()); }
        );

        // ------------------------------------------------------------------
        // hotbar.* table - ALL slot numbers are 1-based (Lua convention)
        // ------------------------------------------------------------------
        sol::table hotbarTable = lua.create_named_table("hotbar");
        hotbarTable["set_size"] = [](int size) { if (g_Hotbar) g_Hotbar->resize(size); };
        hotbarTable["get_size"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["get_slot_count"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["set_slot_size"] = [](float size) { if (g_Hotbar) g_Hotbar->slotSize = size; };
        hotbarTable["set_position"] = [](float x, float y) { if (g_Hotbar) { g_Hotbar->posX = x; g_Hotbar->posY = y; } };

        // hotbar.set_slot(slot, name [, count])  - name "" or "air" clears the slot
        hotbarTable["set_slot"] = [](int slotIndex, const std::string& blockName, sol::optional<int> count) {
            if (!g_Hotbar) return;
            int adj = slotIndex - 1;
            if (adj < 0 || adj >= g_Hotbar->slotCount) return;
            uint8_t blockId = (blockName.empty() || blockName == "air")
                ? 0 : BlockRegistry::getInstance().getBlockId(blockName);
            g_Hotbar->setSlot(adj, blockId, blockId == 0 ? 0 : count.value_or(64));
        };

        // hotbar.get_slot(slot) -> name, count   ("" and 0 when empty)
        hotbarTable["get_slot"] = [](int slotIndex) {
            if (!g_Hotbar) return std::make_tuple(std::string(), 0);
            HotbarSlot slot = g_Hotbar->getSlot(slotIndex - 1);
            if (slot.blockId == 0) return std::make_tuple(std::string(), 0);
            return std::make_tuple(BlockRegistry::getInstance().getBlock(slot.blockId).name, slot.count);
        };

        // hotbar.get_rect(W, H) -> x, y, w, h: where the bar is drawn (pixels, top-left), so HUD
        // mods can put hearts and hunger right above it
        hotbarTable["get_rect"] = [](float W, float H) {
            float x = 0, y = 0, w = 0, h = 0;
            if (g_Hotbar) hotbarScreenRect(*g_Hotbar, W, H, x, y, w, h);
            return std::make_tuple(x, y, w, h);
        };
        hotbarTable["get_active_slot"] = []() { return g_Hotbar ? g_Hotbar->activeSlot + 1 : 1; };
        hotbarTable["set_active_slot"] = [](int slotIndex) {
            if (g_Hotbar) {
                int adj = slotIndex - 1;
                if (adj >= 0 && adj < g_Hotbar->slotCount) g_Hotbar->activeSlot = adj;
            }
        };
        hotbarTable["is_creative_open"] = []() { return g_Inventory.isCreativeOpen(); };
        hotbarTable["toggle_creative"] = []() { g_Inventory.toggleCreative(); };
        // hotbar.set_creative_open(on): opens or closes the creative inventory, freeing the mouse
        // while it (or a mod's screen) is open. The survival inventory uses it to go back to it.
        hotbarTable["set_creative_open"] = [window](bool open) {
            g_Inventory.setCreativeOpen(open);
            glfwSetInputMode(window, GLFW_CURSOR, (open || g_LuaInventoryOpen) ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        };
        hotbarTable["get_slot_block"] = [](int slotIndex) -> std::string {
            if (!g_Hotbar) return "air";
            int adj = slotIndex - 1;
            HotbarSlot slot = g_Hotbar->getSlot(adj);
            if (slot.blockId == 0) return "air";
            return BlockRegistry::getInstance().getBlock(slot.blockId).name;
        };
        hotbarTable["set_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->customTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_selected_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->selectedTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_texture_scale"] = [](float s) {
            if (g_Hotbar) g_Hotbar->textureScale = glm::clamp(s, 0.25f, 4.0f);
        };

        auto skyTable = lua.create_named_table("sky");
        skyTable["set_time"] = [](float time) { if (g_LightingSystem) g_LightingSystem->customTimeOfDay = time; };
        skyTable["get_time"] = []() { return g_LightingSystem ? g_LightingSystem->getTimeOfDay() : 0.0f; };
        // Stars: sky.set_stars(density, brightness). density 0..1 (default 0.06), brightness 0 = off (default 0.9)
        skyTable["set_stars"] = [](float density, sol::optional<float> brightness) {
            if (!g_Skybox) return;
            g_Skybox->starDensity = std::max(0.0f, std::min(1.0f, density));
            if (brightness) g_Skybox->starBrightness = std::max(0.0f, *brightness);
        };
        skyTable["get_stars"] = []() -> std::tuple<float, float> {
            return g_Skybox ? std::make_tuple(g_Skybox->starDensity, g_Skybox->starBrightness) : std::make_tuple(0.0f, 0.0f);
        };
        skyTable["set_moon_phase"] = [](int phase) { if (g_LightingSystem) g_LightingSystem->forcedMoonPhase = phase; };
        skyTable["get_moon_phase"] = []() { return g_LightingSystem ? g_LightingSystem->getMoonPhase() : 0; };
        skyTable["set_moon_textures"] = [](sol::table paths) {
            if (!g_LightingSystem) return;
            std::vector<std::string> pList;
            for (int i = 0; i < paths.size(); ++i) pList.push_back(paths[i + 1]);
            g_LightingSystem->setMoonPhaseTextures(pList);
        };
        skyTable["set_rain_intensity"] = [](float intensity) {
            if (g_LightingSystem) g_LightingSystem->weatherRainIntensity = glm::clamp(intensity, 0.0f, 1.0f);
        };
        skyTable["get_rain_intensity"] = []() { return g_LightingSystem ? g_LightingSystem->weatherRainIntensity : 0.0f; };
        skyTable["set_sun_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setSunColor(r, g, b); };
        skyTable["set_ambient_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setAmbientColor(r, g, b); };
        skyTable["set_fog_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setFogColor(r, g, b); };
        skyTable["set_sun_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setSunIntensity(intensity); };
        skyTable["set_ambient_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setAmbientIntensity(intensity); };
        skyTable["reset"] = []() { if (g_LightingSystem) g_LightingSystem->resetToDefaults(); };
        // ---- Weather effects (drawn by WeatherEffects; DECIDED by Lua, see mods/weather) ----
        // sky.set_precipitation{ type = "rain"|"snow"|any, intensity = 0..1, fall_speed, size,
        //     length, sway, wind_x, wind_z, color = {r,g,b,a}, texture = "ash.png", radius,
        //     height, max_particles, streaks = bool }
        //   `type` starts from that preset; other fields change only what you give, so
        //   sky.set_precipitation{ intensity = 0.3 } just thins out the current rain.
        skyTable["set_precipitation"] = [](sol::table t) {
            static std::unordered_map<std::string, GLuint> textures;
            PrecipitationSettings p = g_Weather.precipitation();
            sol::optional<std::string> type = t["type"];
            if (type) {
                const float keep = p.intensity;
                if (*type != p.type) p = WeatherEffects::preset(*type);
                p.intensity = keep;
            }
            p.intensity = t.get_or("intensity", p.intensity);
            p.fallSpeed = t.get_or("fall_speed", p.fallSpeed);
            p.size = t.get_or("size", p.size);
            p.length = t.get_or("length", p.length);
            p.sway = t.get_or("sway", p.sway);
            p.wind.x = t.get_or("wind_x", p.wind.x);
            p.wind.y = t.get_or("wind_z", p.wind.y);
            p.radius = t.get_or("radius", p.radius);
            p.height = t.get_or("height", p.height);
            p.maxParticles = t.get_or("max_particles", p.maxParticles);
            p.streaks = t.get_or("streaks", p.streaks);
            sol::optional<sol::table> color = t["color"];
            if (color) {
                p.color = glm::vec4(color->get_or(1, p.color.r), color->get_or(2, p.color.g),
                                    color->get_or(3, p.color.b), color->get_or(4, p.color.a));
            }
            sol::optional<std::string> tex = t["texture"];
            if (tex) {
                if (tex->empty()) p.texture = 0;
                else {
                    auto it = textures.find(*tex);
                    if (it == textures.end()) it = textures.emplace(*tex, loadStandardTexture(*tex)).first;
                    p.texture = it->second;
                }
            }
            g_Weather.setPrecipitation(p);
        };
        // sky.get_precipitation() -> { type, intensity }
        skyTable["get_precipitation"] = [](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table t = lv.create_table();
            t["type"] = g_Weather.precipitation().type;
            t["intensity"] = g_Weather.precipitation().intensity;
            return t;
        };
        // sky.set_sky_tint(r, g, b, a): colour laid over the sky (a = 0 off, 1 = all that colour)
        skyTable["set_sky_tint"] = [](float r, float g, float b, float a) {
            g_Weather.setSkyTint(glm::vec4(r, g, b, glm::clamp(a, 0.0f, 1.0f)));
        };
        // sky.set_light(level): sunlight on the world, 1 = normal, 0.5 = stormy, 0 = black
        skyTable["set_light"] = [](float level) { g_Weather.setLight(level); };
        skyTable["get_light"] = []() { return g_Weather.light(); };
        // sky.flash(strength 0..1, seconds): a lightning flash
        skyTable["flash"] = [](float strength, sol::optional<float> seconds) {
            g_Weather.flash(strength, seconds.value_or(0.35f));
        };

        // Moving lights (not tied to a block): sky.add_light(x, y, z, r, g, b, radius, intensity) -> id,
        // sky.move_light(id, x, y, z [, r, g, b, radius, intensity]), sky.remove_light(id).
        // Up to 16 show at once. For lights that stay put, use a block with `light` instead.
        skyTable["add_light"] = [](float x, float y, float z, float r, float g, float b, float radius, float intensity) {
            return g_LightingSystem ? g_LightingSystem->addPointLight(glm::vec3(x, y, z), glm::vec3(r, g, b), radius, intensity) : -1;
        };
        skyTable["move_light"] = [](int id, float x, float y, float z, sol::optional<float> r, sol::optional<float> g,
                                    sol::optional<float> b, sol::optional<float> radius, sol::optional<float> intensity) {
            if (!g_LightingSystem) return;
            const auto& all = g_LightingSystem->getPointLights();
            if (id < 0 || id >= static_cast<int>(all.size()) || !all[id].active) return;
            const PointLight& cur = all[id];
            g_LightingSystem->updatePointLight(id, glm::vec3(x, y, z),
                glm::vec3(r.value_or(cur.color.x), g.value_or(cur.color.y), b.value_or(cur.color.z)),
                radius.value_or(cur.radius), intensity.value_or(cur.intensity));
        };
        skyTable["remove_light"] = [](int id) { if (g_LightingSystem) g_LightingSystem->removePointLight(id); };

        // ---- Block light look (Light.h) ----
        // sky.set_min_light(v): the darkest anything gets, 0..1 (default 0.02; 0 = pitch black caves)
        skyTable["set_min_light"] = [](float v) { g_LightSettings.minLight = std::max(0.0f, std::min(1.0f, v)); };
        // sky.set_brightness(v): 0 = moody, 1 = bright, like Minecraft's brightness slider (default 0.35)
        skyTable["set_brightness"] = [](float v) { g_LightSettings.brightness = std::max(0.0f, std::min(1.0f, v)); };
        // sky.set_block_light_strength(v): how strong torches and other light sources are (default 1)
        skyTable["set_block_light_strength"] = [](float v) { g_LightSettings.blockLightScale = std::max(0.0f, v); };
        // sky.set_held_light(on): holding a light source lights up around the player (default on)
        skyTable["set_held_light"] = [](bool on) { g_LightSettings.heldLight = on; };

        auto cloudTable = lua.create_named_table("clouds");
        cloudTable["set_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.color = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_night_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.nightColor = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_height"] = [](float y) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.height = y; g_Clouds->setSettings(s);
        };
        cloudTable["set_thickness"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.thickness = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_cell_size"] = [](float c) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.cellSize = c; g_Clouds->setSettings(s);
        };
        cloudTable["set_threshold"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.threshold = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_scroll_speed"] = [](float v) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.scrollSpeed = v; g_Clouds->setSettings(s);
        };
        cloudTable["set_texture"] = [](const std::string& path) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.texturePath = path; g_Clouds->setSettings(s);
        };
        cloudTable["set_weather"] = [](const std::string& name, float seconds) {
            if (g_Clouds) g_Clouds->setWeather(name, seconds);
        };

        auto fogTable = lua.create_named_table("fog");
        fogTable["set_enabled"]      = [](bool on) { g_Fog.enabled = on; };
        fogTable["set_exponential"]  = [](bool on) { g_Fog.exponential = on; };
        fogTable["set_range"]        = [](float s, float e) { g_Fog.start = s; g_Fog.end = e; };
        fogTable["set_density"]      = [](float d) { g_Fog.density = d; };
        fogTable["set_rain_scale"]   = [](float s) { g_Fog.rainScale = glm::clamp(s, 0.0f, 1.0f); };
        fogTable["set_day_color"]    = [](float r, float g, float b) { g_Fog.dayColor = glm::vec3(r, g, b); };
        fogTable["set_sunset_color"] = [](float r, float g, float b) { g_Fog.sunsetColor = glm::vec3(r, g, b); };
        fogTable["set_night_color"]  = [](float r, float g, float b) { g_Fog.nightColor = glm::vec3(r, g, b); };

        auto chatTable = lua.create_named_table("chat");
        chatTable["register_command"] = [](const std::string& name,
                                          const std::string& usage,
                                          const std::string& description) {
            if (g_ChatSystem) g_ChatSystem->registerCommand(name, usage, description);
        };
        chatTable["send"] = [](const std::string& text,
                               sol::optional<float> r,
                               sol::optional<float> g,
                               sol::optional<float> b) {
            if (!g_ChatSystem) return;
            if (r.has_value() && g.has_value() && b.has_value()) {
                g_ChatSystem->addMessage(text, glm::vec3(*r, *g, *b));
            } else {
                g_ChatSystem->addMessage(text);
            }
        };

        g_ChatSystem->setCommandHandler([&lua](const std::string& command) -> bool {
            // /netstats - frame rate, ping and how often other players' positions arrive
            if (command == "netstats") {
                const glm::vec3 info(0.6f, 0.9f, 1.0f);
                g_ChatSystem->addMessage("FPS: " + std::to_string(static_cast<int>(g_Fps)), info);
                std::vector<std::string> lines;
                if (g_Server.running()) lines = g_Server.statsLines();
                else lines = g_Client.statsLines();
                for (const auto& l : lines) g_ChatSystem->addMessage(l, info);
                return true;
            }
            // /name <new name> - built in, so it works in every game
            if (command == "name" || command.rfind("name ", 0) == 0) {
                std::string name = command.size() > 5 ? command.substr(5) : std::string();
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);
                if (name.empty()) {
                    g_ChatSystem->addMessage("Your name is " + playerName() + ". Use /name <new name> to change it.");
                    return true;
                }
                std::string error;
                if (changePlayerName(name, error)) g_ChatSystem->addMessage("Your name is now " + name);
                else g_ChatSystem->addMessage(error, glm::vec3(1.0f, 0.55f, 0.55f));
                return true;
            }
            // Hosting: /gamemode <mode> <player> sets a joined player's mode (you're the admin of
            // your own LAN game; they can't change it themselves)
            if (g_Server.running()) {
                std::istringstream gs(command);
                std::string w0, w1, w2;
                gs >> w0 >> w1 >> w2;
                if ((w0 == "gamemode" || w0 == "gm") && !w2.empty()) {
                    const std::string mode = (w1 == "creative" || w1 == "c" || w1 == "1") ? "creative"
                                           : (w1 == "survival" || w1 == "s" || w1 == "0") ? "survival" : "";
                    if (mode.empty()) {
                        g_ChatSystem->addMessage("Usage: /gamemode survival|creative [player]", glm::vec3(1.0f, 0.55f, 0.55f));
                    } else if (g_Server.setPlayerMode(w2, mode)) {
                        g_ChatSystem->addMessage(w2 + "'s game mode is now " + mode, glm::vec3(0.75f, 0.85f, 1.0f));
                        g_Server.messagePlayer(w2, "Your game mode is now " + mode + " (by the host)", glm::vec3(0.6f, 0.9f, 1.0f));
                    } else {
                        g_ChatSystem->addMessage(w2 + " isn't in your game", glm::vec3(1.0f, 0.55f, 0.55f));
                    }
                    return true;
                }
            }
            bool handledByLua = false;
            try {
                sol::protected_function fn = lua["on_chat_command"];
                if (fn.valid()) {
                    // on_chat_command(text, player, is_operator, reply) - same arguments as on a
                    // dedicated server, so one mod works in both places
                    std::function<void(const std::string&)> reply = [](const std::string& t) {
                        if (g_ChatSystem) g_ChatSystem->addMessage(t, glm::vec3(0.75f, 0.85f, 1.0f));
                    };
                    const bool isOp = g_Client.state() == GameClient::State::Joined ? g_Client.isOperator() : true;
                    sol::protected_function_result result = fn(command, playerName(), isOp, reply);
                    if (result.valid() && result.get_type() == sol::type::boolean) handledByLua = result.get<bool>();
                }
            } catch (...) {}
            if (handledByLua) return true;

            // Online: the server runs everything else (/list, /msg, and for operators /kick,
            // /ban, /save, /mods...). It answers in chat.
            if (g_Client.state() == GameClient::State::Joined) {
                const std::string word = command.substr(0, command.find(' '));
                if (word == "help") {
                    g_Client.sendCommand(command);   // the server's commands...
                    return false;                     // ...and this game's /help as well
                }
                if (word != "clear") {
                    g_Client.sendCommand(command);
                    return true;
                }
            }
            return false;
        });

        auto worldTable = lua.create_named_table("world");
        worldTable["set_render_distance"] = [](int chunks) { if (g_WorldInstance) g_WorldInstance->setRenderDistance(chunks); };
        worldTable["get_render_distance"] = []() { return g_WorldInstance ? g_WorldInstance->renderDistance() : 10; };
        worldTable["set_load_budget"]      = [](float ms) { if (g_WorldInstance) g_WorldInstance->setLoadBudgetMs(ms); };
        worldTable["set_mesh_budget"]      = [](float ms) { if (g_WorldRenderer) g_WorldRenderer->setMeshBudgetMs(ms); };
        worldTable["get_loaded_chunks"]   = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->loadedChunkCount()) : 0; };
        worldTable["get_pending_chunks"]  = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->pendingLoadCount()) : 0; };
        // world.set_caves_enabled(false) turns off the built-in caves (e.g. if your mapgen makes its own)
        worldTable["set_caves_enabled"]   = [](bool on) { CaveGenerator::setEnabled(on); };
        worldTable["get_caves_enabled"]   = []() { return CaveGenerator::isEnabled(); };
        // world.set_cave_settings({ tunnels, tunnel_frequency, ravines, caverns, noodles, lava_level... })
        worldTable["set_cave_settings"]   = [](sol::table t) { applyCaveSettings(t); };
        worldTable["get_cave_settings"]   = [](sol::this_state ts) { return caveSettingsTable(ts); };

        auto underwaterTable = lua.create_named_table("underwater");
        underwaterTable["set_shallow_color"] = [](float r, float g, float b) { g_Underwater.shallowColor = glm::vec3(r, g, b); g_Underwater.colorsFromLua = true; };
        underwaterTable["set_deep_color"]    = [](float r, float g, float b) { g_Underwater.deepColor = glm::vec3(r, g, b); g_Underwater.colorsFromLua = true; };
        // underwater.get_water_color() -> r, g, b (0..1): the water block's own colour (its texture
        // and tint), e.g. to build biome water colours from in get_water_color(x, z)
        underwaterTable["get_water_color"]   = []() {
            const glm::vec3 c = waterBlockColors().shallow;
            return std::make_tuple(c.r, c.g, c.b);
        };
        // underwater.use_water_color() - back to colours taken from the water block (the default)
        underwaterTable["use_water_color"]   = []() { g_Underwater.colorsFromLua = false; };
        underwaterTable["set_visibility"]    = [](float shallow, float deep) {
            g_Underwater.shallowVisibility = glm::max(shallow, 1.0f);
            g_Underwater.deepVisibility    = glm::max(deep, 1.0f);
        };
        underwaterTable["set_full_depth"]    = [](float d) { g_Underwater.fullDepth = glm::max(d, 1.0f); };
        underwaterTable["set_fade"]          = [](float inSeconds, float outSeconds) {
            g_Underwater.fadeInSeconds  = glm::max(inSeconds, 0.01f);
            g_Underwater.fadeOutSeconds = glm::max(outSeconds, 0.01f);
        };
        underwaterTable["set_tint_strength"] = [](float s) { g_Underwater.tintStrength = glm::clamp(s, 0.0f, 1.0f); };

        auto menuTable = lua.create_named_table("menu");
        menuTable["get_games"] = [&]() {
            auto games = getAvailableGames();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& g : games) {
                sol::table gt = lua.create_table();
                gt["folder"] = g.folderName;
                gt["title"] = g.title;
                gt["author"] = g.author;
                gt["description"] = g.description;
                t[idx++] = gt;
            }
            return t;
        };
        menuTable["get_active_game"] = []() { return g_ActiveGameFolder; };
        menuTable["set_active_game"] = [](std::string folderName) { g_ActiveGameFolder = folderName; };

        auto engineTable = lua.create_named_table("engine");
        engineTable["register_block"] = &registerBlockFromLua;
        engineTable["get_block_id"] = &getBlockIdLua;
        engineTable["set_fov"] = [](float fov) { g_FieldOfView = fov; };
        // engine.is_server() -> false here, true on BlockSmithServer (for mods that run on both)
        engineTable["is_server"] = []() { return false; };
        engineTable["set_sun_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setSunTexture(path); };
        engineTable["set_moon_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setMoonTexture(path); };

        // engine.load_model(path [, texture [, scale]])
        //   path    - .json (Bedrock geometry) or .b3d (Luanti mesh), found in the mod's folder or models/
        //   texture - image for the model; defaults to the one named inside a .b3d
        //   scale   - size multiplier; .b3d models default to 0.1 (10 units = 1 block)
        engineTable["load_model"] = [](std::string path, sol::optional<std::string> texture, sol::optional<float> scale) {
            auto model = std::make_shared<AnimatedModel>();
            const std::string resolved = resolveAssetPath(path, "models");
            if (resolved.empty()) {
                std::cerr << "[Model Error] Model file not found: " << path
                          << " (looked in the mod's folder and its models/ folder)" << std::endl;
                return model;
            }
            model->loadFromFile(resolved);

            const std::string tex = texture.value_or(model->textureHint);
            if (!tex.empty()) {
                const std::string texPath = resolveAssetPath(tex, "textures");
                model->texture = loadStandardTexture(texPath.empty() ? tex : texPath);
            }
            if (scale.has_value()) model->scale = scale.value();
            return model;
        };

        engineTable["load_animations"] = [](const std::string& path) -> std::shared_ptr<AnimationSet> {
            const std::string resolved = resolveAssetPath(path, "models");
            if (resolved.empty()) {
                std::cerr << "[Animation] File not found: " << path
                          << " (looked in the mod's folder and its models/ folder)" << std::endl;
                return nullptr;
            }
            auto set = std::make_shared<AnimationSet>();
            if (!set->loadFromFile(resolved)) return nullptr;
            return set;
        };

        // ---- Models in the world (mobs, decorations, ...) ----
        // engine.add_model(model, x, y, z [, yaw [, scale]]) -> id   (x,y,z = feet position, yaw in degrees)
        engineTable["add_model"] = [](std::shared_ptr<AnimatedModel> model, float x, float y, float z,
                                      sol::optional<float> yaw, sol::optional<float> scale) {
            ModelInstance inst;
            inst.model = model;
            inst.position = glm::vec3(x, y, z);
            inst.yaw = yaw.value_or(0.0f);
            inst.scale = scale.value_or(1.0f);
            const int id = g_NextModelInstance++;
            g_ModelInstances[id] = inst;
            return id;
        };
        // engine.move_model(id, x, y, z [, yaw])
        engineTable["move_model"] = [](int id, float x, float y, float z, sol::optional<float> yaw) {
            auto it = g_ModelInstances.find(id);
            if (it == g_ModelInstances.end()) return;
            it->second.position = glm::vec3(x, y, z);
            if (yaw.has_value()) it->second.yaw = yaw.value();
        };
        engineTable["set_model_visible"] = [](int id, bool visible) {
            auto it = g_ModelInstances.find(id);
            if (it != g_ModelInstances.end()) it->second.visible = visible;
        };
        engineTable["remove_model"] = [](int id) { g_ModelInstances.erase(id); };
        engineTable["is_third_person"] = []() { return g_ThirdPerson; };

        engineTable["set_block"] = [](int x, int y, int z, const std::string& blockName) {
            if (g_WorldInstance) {
                uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
                g_WorldInstance->setBlockAt(glm::ivec3(x, y, z), id);
            }
        };
        engineTable["get_block_name"] = [](int x, int y, int z) -> std::string {
            if (g_WorldInstance) {
                uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
                if (id == 0) return "air";
                return BlockRegistry::getInstance().getBlock(id).name;
            }
            return "air";
        };
        // engine.get_metadata(x, y, z) -> 0..255 (water: flow level in the low 3 bits, 8 = falling)
        // engine.get_light(x, y, z) -> sun, red, green, blue (0..15 each): the light at a block
        engineTable["get_light"] = [](int x, int y, int z) -> std::tuple<int, int, int, int> {
            const uint16_t l = g_WorldInstance ? g_WorldInstance->getLightAt(glm::ivec3(x, y, z)) : ChunkData::kFullSky;
            return std::make_tuple(light::sky(l), light::red(l), light::green(l), light::blue(l));
        };
        // engine.get_light_level(x, y, z) -> 0..15: how bright it is there right now (the brightest
        // of block light and sunlight, sunlight lower at night) - for mob spawning, plant growth...
        engineTable["get_light_level"] = [](int x, int y, int z) -> int {
            const uint16_t l = g_WorldInstance ? g_WorldInstance->getLightAt(glm::ivec3(x, y, z)) : ChunkData::kFullSky;
            float day = 1.0f;
            if (g_LightingSystem) day = std::max(0.0f, std::min(1.0f, g_LightingSystem->getSunDirection().y * 2.0f + 0.5f));
            const int skyNow = std::max(0, light::sky(l) - static_cast<int>(std::lround((1.0f - day) * 11.0f)));
            return std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l))));
        };
        // engine.set_smooth_lighting(on) / engine.set_ambient_occlusion(on): how chunks are shaded
        // (both on by default). Every chunk is redrawn.
        engineTable["set_smooth_lighting"] = [](bool on) {
            ChunkMesh::smoothLighting = on;
            if (g_WorldInstance) g_WorldInstance->markAllDirty();
        };
        engineTable["set_ambient_occlusion"] = [](bool on) {
            ChunkMesh::ambientOcclusion = on;
            if (g_WorldInstance) g_WorldInstance->markAllDirty();
        };
        engineTable["get_metadata"] = [](int x, int y, int z) -> int {
            return g_WorldInstance ? g_WorldInstance->getMetadataAt(glm::ivec3(x, y, z)) : 0;
        };
        // engine.set_block_meta(x, y, z, name, meta): block and metadata in one change
        engineTable["set_block_meta"] = [](int x, int y, int z, const std::string& blockName, int meta) {
            if (!g_WorldInstance) return;
            const uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
            g_WorldInstance->setBlockWithMetaAt(glm::ivec3(x, y, z), id, static_cast<uint8_t>(meta & 0xFF));
        };
        // engine.register_fluid, register_fluid_reaction, get_fluid, is_fluid_source,
        // list_fluids (Fluids.cpp)
        addFluidLuaApi(lua, engineTable, []() { return g_WorldInstance; });
        // engine.is_replaceable(x, y, z): air, a fluid or a plant - room to put something
        engineTable["is_replaceable"] = [](int x, int y, int z) {
            if (!g_WorldInstance) return false;
            const uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0) return true;
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            return FluidRegistry::get().forBlock(id) != nullptr || prop.pattern == "plantlike";
        };
        // engine.register_item_use(item, function(pointed) ... end)
        //   Right-clicking with `item` in the active hotbar slot calls fn instead of placing it.
        //   pointed = { item, slot, count,
        //               under = {x,y,z,name} (block looked at, or nil),
        //               above = {x,y,z}      (the space in front of it),
        //               fluid = {x,y,z,name,source} (water looked at first, or nil),
        //               player = {x,y,z} (feet) }
        //   Return true when the click was used (false lets the block be placed as normal).
        engineTable["register_item_use"] = [](const std::string& item, sol::protected_function fn) {
            g_ItemUseHandlers[item] = std::move(fn);
        };
        // ---- Shared values: decided by whoever runs the world, seen by every player ----
        // engine.is_authority() -> true in singleplayer and when hosting; false when joined to
        //   someone else's world/server (then their Lua decides, e.g. the weather)
        // engine.is_flying() / engine.set_flying(on): creative flying (double-tap Jump). Only
        // in creative; set_flying returns false otherwise. engine.set_fly_allowed(false) turns
        // double-tap flying off (e.g. for a parkour map), true turns it back on.
        engineTable["is_flying"] = []() { return g_Flying; };
        engineTable["set_flying"] = [](bool on) {
            if (on && !(g_Creative && g_FlyAllowed)) return false;
            g_Flying = on;
            return true;
        };
        engineTable["set_fly_allowed"] = [](bool on) { g_FlyAllowed = on; if (!on) g_Flying = false; };
        engineTable["is_authority"] = []() { return g_Client.state() != GameClient::State::Joined; };
        // engine.set_shared(key, value) -> true; false when joined (only the server sets them)
        engineTable["set_shared"] = [](const std::string& key, const std::string& value) {
            if (g_Client.state() == GameClient::State::Joined || key.empty()) return false;
            const auto it = g_SharedValues.find(key);
            if (it != g_SharedValues.end() && it->second == value) return true;
            g_SharedValues[key] = value;
            g_SharedChanged.push_back(key);
            if (g_Server.running()) g_Server.setShared(key, value);   // LAN players get it too
            return true;
        };
        // engine.get_shared(key) -> value, or "" if never set
        engineTable["get_shared"] = [](const std::string& key) -> std::string {
            if (g_Client.state() == GameClient::State::Joined) return g_Client.getShared(key);
            const auto it = g_SharedValues.find(key);
            return it == g_SharedValues.end() ? std::string() : it->second;
        };
        // engine.get_player_position() -> x, y, z (feet) of this game's player
        engineTable["get_player_position"] = []() {
            return std::make_tuple(g_PlayerPosition.x, g_PlayerPosition.y, g_PlayerPosition.z);
        };
        // ---- Survival building blocks (mods/survival uses these; any game can) ----
        // engine.set_creative(bool) / engine.is_creative()
        //   creative: blocks break instantly, placing is free, the creative menu opens
        //   survival: hold to dig (hardness, tools), placing uses up the stack, no creative menu
        //   On a server, the server decides (only its operators can change modes): switching to
        //   anything but the mode the server gave you is refused and returns false.
        // ---- First-person hand (FirstPerson.h) ----
        // engine.set_first_person_hand(on): show the held block / arm in first person (default on)
        engineTable["set_first_person_hand"] = [](bool on) { g_FirstPerson.enabled = on; };
        // engine.set_first_person_arm({ right = "bone", left = "bone", color = { r, g, b } }):
        // which bone of g_PlayerModel is the arm shown when the hand is empty (with everything
        // attached to it), and the colour of the plain arm used if the model has none
        engineTable["set_first_person_arm"] = [](sol::table t) {
            sol::optional<std::string> r = t["right"];
            sol::optional<std::string> l = t["left"];
            if (r) g_FirstPerson.rightArmBones.insert(g_FirstPerson.rightArmBones.begin(), *r);
            if (l) g_FirstPerson.leftArmBones.insert(g_FirstPerson.leftArmBones.begin(), *l);
            sol::optional<sol::table> c = t["color"];
            if (c) {
                glm::vec3 col((*c)[1].get_or(0.88f), (*c)[2].get_or(0.69f), (*c)[3].get_or(0.55f));
                if (col.r > 1.0f || col.g > 1.0f || col.b > 1.0f) col /= 255.0f;
                g_FirstPerson.skinColor = col;
            }
        };
        // engine.swing_hand(): play the hand swing (e.g. when a mod's item is used)
        engineTable["swing_hand"] = []() { g_FirstPerson.swing(); };
        // engine.get_swing() -> 0..1 how far through the current swing (0.3 s), or -1 when not
        // swinging. The player model uses it to swing its arm in third person.
        engineTable["get_swing"] = []() { return g_FirstPerson.swingProgress(); };
        // engine.is_left_handed(): the player's Main Hand setting
        engineTable["is_left_handed"] = []() { return settingInt("main_hand", 0) == 1; };
        // engine.set_place_repeat(seconds): holding right-click places / uses again this often
        // (Minecraft: 0.2). 0 = one per click.
        engineTable["set_place_repeat"] = [](float seconds) { g_PlaceRepeatSeconds = std::max(0.0f, seconds); };
        engineTable["set_creative"] = [](bool on) -> bool {
            if (g_Client.state() == GameClient::State::Joined && !g_Client.gameMode().empty() &&
                on != (g_Client.gameMode() == "creative")) {
                std::cout << "[Game mode] Refused: on this server only operators can change your game mode" << std::endl;
                return false;
            }
            g_Creative = on;
            g_Dig = DigState{};
            if (!on && g_Inventory.isCreativeOpen()) g_Inventory.setCreativeOpen(false);
            return true;
        };
        engineTable["is_creative"] = []() { return g_Creative; };
        // engine.get_server_game_mode() -> "survival" / "creative" (what the server gave you), or nil
        // when you're not on a server (then your own game decides)
        engineTable["get_server_game_mode"] = [](sol::this_state ts) -> sol::object {
            if (g_Client.state() != GameClient::State::Joined || g_Client.gameMode().empty()) return sol::make_object(ts, sol::lua_nil);
            return sol::make_object(ts, g_Client.gameMode());
        };
        // engine.is_operator() -> true in singleplayer and when hosting; on a server, whether you're one of its operators
        engineTable["is_operator"] = []() {
            return g_Client.state() == GameClient::State::Joined ? g_Client.isOperator() : true;
        };
        // engine.set_footstep_distance(blocks): how far you walk between footstep sounds (1.3)
        engineTable["set_footstep_distance"] = [](float blocks) { g_FootstepDistance = std::clamp(blocks, 0.3f, 10.0f); };

        // engine.get_player_state() -> { x, y, z, vx, vy, vz, yaw, pitch, on_ground, moving,
        //   sprinting, sneaking, jumping, feet_fluid, head_fluid }   (fluid names or nil)
        engineTable["get_player_state"] = [](sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            Player* p = g_PlayerInstance;
            if (!p) return sol::make_object(lv, sol::lua_nil);
            sol::table t = lv.create_table();
            t["x"] = p->position.x; t["y"] = p->position.y; t["z"] = p->position.z;
            t["vx"] = p->velocity.x; t["vy"] = p->velocity.y; t["vz"] = p->velocity.z;
            t["yaw"] = p->yaw; t["pitch"] = p->pitch;
            t["on_ground"] = p->isGrounded;
            const uint8_t k = g_LastMoveKeys;
            const bool moving = (k & (MoveInput::Forward | MoveInput::Back | MoveInput::Left | MoveInput::Right)) != 0;
            t["moving"] = moving;
            t["sprinting"] = moving && (k & MoveInput::Sprint) != 0;
            t["sneaking"] = (k & MoveInput::Sneak) != 0;
            t["jumping"] = (k & MoveInput::Jump) != 0;
            t["flying"] = g_Flying;
            if (g_WorldInstance) {
                auto fluidAt = [](const glm::vec3& v) -> const FluidDef* {
                    FluidRegistry::get().ensureFresh();
                    return FluidRegistry::get().forBlock(g_WorldInstance->getBlockAt(glm::ivec3(
                        static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z)))));
                };
                if (const FluidDef* f = fluidAt(p->position + glm::vec3(0.0f, 0.1f, 0.0f))) t["feet_fluid"] = f->name;
                if (const FluidDef* f = fluidAt(p->position + glm::vec3(0.0f, 1.62f, 0.0f))) t["head_fluid"] = f->name;
            }
            return t;
        };
        // engine.set_player_position(x, y, z) -> true; false when joined to a server
        engineTable["set_player_position"] = [](float x, float y, float z) {
            if (!g_PlayerInstance || g_Client.state() == GameClient::State::Joined) return false;
            g_PlayerInstance->position = glm::vec3(x, y, z);
            g_PlayerInstance->velocity = glm::vec3(0.0f);
            return true;
        };
        // engine.add_player_velocity(vx, vy, vz): a push (knockback). Singleplayer/host only.
        engineTable["add_player_velocity"] = [](float x, float y, float z) {
            if (!g_PlayerInstance || g_Client.state() == GameClient::State::Joined) return false;
            g_PlayerInstance->velocity += glm::vec3(x, y, z);
            return true;
        };
        // engine.respawn_player(x, y, z): back to a spawn point. When joined, the server
        // moves you to its spawn instead (the coordinates are ignored).
        engineTable["respawn_player"] = [](sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) {
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendCommand("respawn"); return true; }
            if (!g_PlayerInstance || !x || !y || !z) return false;
            g_PlayerInstance->position = glm::vec3(*x, *y, *z);
            g_PlayerInstance->velocity = glm::vec3(0.0f);
            return true;
        };
        // engine.set_player_controls{ move = bool, jump = bool, sprint = bool, sneak = bool }
        //   e.g. sprint = false when hungry, everything false while dead. Missing = unchanged.
        engineTable["set_player_controls"] = [](sol::table t) {
            auto flag = [&t](const char* name, uint8_t bits) {
                sol::optional<bool> on = t[name];
                if (!on) return;
                if (*on) g_MoveKeyMask |= bits; else g_MoveKeyMask &= static_cast<uint8_t>(~bits);
            };
            flag("move", MoveInput::Forward | MoveInput::Back | MoveInput::Left | MoveInput::Right);
            flag("jump", MoveInput::Jump);
            flag("sprint", MoveInput::Sprint);
            flag("sneak", MoveInput::Sneak);
        };

        // engine.get_storage(key) -> text or nil; engine.set_storage(key, text or nil)
        //   saved with the world (singleplayer and hosting; kept in memory when joined)
        engineTable["get_storage"] = [](const std::string& key, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            const auto it = g_ModStorage.find(key);
            if (it == g_ModStorage.end()) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, it->second);
        };
        engineTable["set_storage"] = [](const std::string& key, sol::optional<std::string> value) {
            if (value) g_ModStorage[key] = *value; else g_ModStorage.erase(key);
            g_ModStorageDirty = true;
        };

        // engine.set_dig_crack_texture(file, stages): the crack drawn on a block being dug, a
        //   vertical strip of `stages` square pictures (first stage at the top), like
        //   Minecraft's destroy stages. "" goes back to the plain darkening box.
        //   Leave out `stages` to count them from the picture (height / width), like Luanti's
        //   crack_anylength.png - which the engine also uses by itself if the game has one.
        engineTable["set_dig_crack_texture"] = [](const std::string& path, sol::optional<int> stages) {
            g_CrackTexture = path.empty() ? 0 : loadStandardTexture(path);
            int n = stages.value_or(0);
            if (g_CrackTexture != 0 && n <= 0) {
                GLint w = 0, h = 0;
                glBindTexture(GL_TEXTURE_2D, g_CrackTexture);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
                glBindTexture(GL_TEXTURE_2D, 0);
                n = (w > 0 && h >= w) ? h / w : 1;
            }
            g_CrackStages = std::clamp(n > 0 ? n : 10, 1, 64);
            return g_CrackTexture != 0 || path.empty();
        };
        // ---- Sound (Audio.h): sound.* ----
        auto soundTable = lua.create_named_table("sound");
        // sound.play(name [, { pos = {x,y,z}, gain, pitch, loop, group, max_distance }]) -> handle / nil
        //   name: "my_mod_click" plays my_mod_click.ogg/.wav (or a random .1/.2/... variant)
        //   from any mod's folder or sounds/ folder. With `pos` it's heard from that place.
        //   group: "effects" (default), "ambient", "music" or "ui" - each has its own volume.
        // sound.play(name [, opts]); the sandboxed copy is what client mods get (ClientMods.cpp):
        // it only plays files inside the sound folders (AudioSystem::findFiles)
        auto playSound = [](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts,
                            bool sandboxed) -> sol::object {
            sol::state_view lv(ts);
            SoundParams p;
            p.sandboxed = sandboxed;
            if (opts) {
                const sol::table& o = *opts;
                p.gain = o.get_or("gain", 1.0f);
                p.pitch = o.get_or("pitch", 1.0f);
                p.loop = o.get_or("loop", false);
                p.group = o.get_or("group", std::string("effects"));
                p.maxDistance = o.get_or("max_distance", 24.0f);
                p.refDistance = o.get_or("ref_distance", 2.0f);
                sol::optional<sol::table> pos = o["pos"];
                if (pos) {
                    p.positional = true;
                    p.position = glm::vec3(pos->get_or("x", 0.0f), pos->get_or("y", 0.0f), pos->get_or("z", 0.0f));
                }
            }
            const int h = g_Audio.play(name, p);
            if (h == 0) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, h);
        };
        soundTable["play"] = [playSound](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts) {
            return playSound(name, opts, ts, false);
        };
        soundTable["__play_sandboxed"] = [playSound](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts) {
            return playSound(name, opts, ts, true);
        };
        soundTable["stop"] = [](int h) { g_Audio.stop(h); };
        // sound.fade(handle, gain, seconds): to 0 stops it at the end
        soundTable["fade"] = [](int h, float gain, sol::optional<float> seconds) { g_Audio.fade(h, gain, seconds.value_or(1.0f)); };
        soundTable["set_gain"] = [](int h, float gain) { g_Audio.setGain(h, gain); };
        soundTable["set_pitch"] = [](int h, float pitch) { g_Audio.setPitch(h, pitch); };
        soundTable["set_position"] = [](int h, float x, float y, float z) { g_Audio.setPosition(h, glm::vec3(x, y, z)); };
        soundTable["is_playing"] = [](int h) { return g_Audio.isPlaying(h); };
        soundTable["exists"] = [](const std::string& name) { return g_Audio.soundExists(name); };
        soundTable["__exists_sandboxed"] = [](const std::string& name) { return g_Audio.soundExists(name, true); };
        soundTable["is_available"] = []() { return g_Audio.available(); };
        // sound.play_music(name [, { fade = 2, loop = false, gain = 1 }]) - streamed, crossfades
        soundTable["play_music"] = [](const std::string& name, sol::optional<sol::table> opts) {
            float fade = 2.0f, gain = 1.0f;
            bool loop = false;
            if (opts) { fade = opts->get_or("fade", fade); loop = opts->get_or("loop", loop); gain = opts->get_or("gain", gain); }
            return g_Audio.playMusic(name, fade, loop, gain) != 0;
        };
        soundTable["stop_music"] = [](sol::optional<float> fade) { g_Audio.stopMusic(fade.value_or(2.0f)); };
        soundTable["get_music"] = []() { return g_Audio.currentMusic(); };
        // sound.set_volume(group, 0..1) / sound.get_volume(group): "master", "music", "effects",
        //   "ambient", "ui" (the Settings sliders change the same values)
        soundTable["set_volume"] = [](const std::string& group, float v) { g_Audio.setVolume(group, v); };
        soundTable["get_volume"] = [](const std::string& group) { return g_Audio.volume(group); };

        // engine.find_texture(name) -> full path, or nil if no such picture (looks in the game's
        //   textures, the calling mod's folder and every loaded mod's folder)
        engineTable["find_texture"] = [](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            const std::string found = findTextureFile(name);
            if (found.empty()) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, found);
        };
        // engine.set_inventory_open(bool) / engine.is_inventory_open(): a mod's inventory screen
        //   (frees the mouse while open; clicks go to handle_inventory_click)
        engineTable["set_inventory_open"] = [window](bool open) {
            g_LuaInventoryOpen = open;
            glfwSetInputMode(window, GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        };
        engineTable["is_inventory_open"] = []() { return g_LuaInventoryOpen; };
        // engine.get_dig_progress() -> progress (0..1), x, y, z - or nil when not digging
        engineTable["get_dig_progress"] = []() -> std::tuple<sol::optional<float>, int, int, int> {
            if (!g_Dig.active) return { sol::nullopt, 0, 0, 0 };
            return { g_Dig.progress, g_Dig.pos.x, g_Dig.pos.y, g_Dig.pos.z };
        };
        // engine.override_block(name, { hardness, tool, needs_tool, drops, tool_type, tool_speed,
        //   in_creative }) - change a block another mod registered (like Luanti's override_item)
        engineTable["override_block"] = [](const std::string& name, sol::table props) {
            auto& reg = BlockRegistry::getInstance();
            const uint8_t id = reg.getBlockId(name);
            if (id == 0) return false;
            BlockProperty b = reg.getBlock(id);
            applyDigProperties(b, props);
            applySoundProperties(b, props);
            applyLightProperties(b, props);
            sol::optional<std::string> biome = props["biome_tint"];
            if (biome) b.biomeTint = (*biome == "grass") ? 0 : (*biome == "foliage") ? 1 : -1;
            b.in_creative = props.get_or("in_creative", b.in_creative);
            reg.registerBlock(b);
            return true;
        };
        // engine.list_blocks() -> { "stone", "dirt", ... } every registered block (and item)
        engineTable["list_blocks"] = [](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table t = lv.create_table();
            auto& reg = BlockRegistry::getInstance();
            int n = 1;
            for (int id = 1; id < 256; ++id) {
                const auto& b = reg.getBlock(static_cast<uint8_t>(id));
                if (!b.name.empty() && b.name != "air" && reg.getBlockId(b.name) == id) t[n++] = b.name;
            }
            return t;
        };
        // engine.get_block_def(name) -> { name, hardness (by hand, s), tool, needs_tool, drops,
        //   tool_type, tool_speed, solid, item } or nil
        engineTable["get_block_def"] = [](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            auto& reg = BlockRegistry::getInstance();
            const uint8_t id = reg.getBlockId(name);
            if (id == 0) return sol::make_object(lv, sol::lua_nil);
            const BlockProperty& b = reg.getBlock(id);
            sol::table t = lv.create_table();
            t["name"] = b.name;
            t["hardness"] = blockHardness(b);
            t["tool"] = blockTool(b);
            t["needs_tool"] = blockNeedsTool(b);
            t["drops"] = b.dropsSet ? b.drops : b.name;
            t["pick"] = b.pick.empty() ? b.name : b.pick;
            t["tool_type"] = b.toolType;
            t["tool_speed"] = b.toolSpeed;
            t["solid"] = b.isWalkable;
            t["item"] = !b.inventoryImage.empty();
            t["has_sounds"] = !b.soundFootstep.name.empty() || !b.soundDig.name.empty() ||
                              !b.soundDug.name.empty() || !b.soundPlace.name.empty();
            return t;
        };

        // engine.is_outdoors(x, y, z) -> true if nothing is above (rain reaches it)
        engineTable["is_outdoors"] = [](int x, int y, int z) {
            return g_WorldInstance && g_WorldInstance->hasSkyAbove(glm::ivec3(x, y, z));
        };
        // engine.give_item(name [, count]) -> true if it fit: into a slot holding the same item,
        // else the first empty slot
        engineTable["give_item"] = [](const std::string& name, sol::optional<int> count) {
            return giveHotbarItem(name, count.value_or(1));
        };

        engineTable["draw_text"] = [&](std::string text, float x, float y) {
            if (g_FontRenderer) {
                Dimensions2D windowSize;
                glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
                g_FontRenderer->renderText(fontProgram, text, x, y, 1.0f, 1.0f, glm::vec4(1.0f), windowSize);
            }
        };

        engineTable["get_worlds"] = [&]() {
            auto worlds = getAvailableWorlds();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : worlds) t[idx++] = w;
            return t;
        };

        engineTable["get_active_world"] = []() { return g_ActiveWorldName; };
        engineTable["set_active_world"] = [](std::string name) { g_ActiveWorldName = name; };
        engineTable["quit"] = [&]() { glfwSetWindowShouldClose(window, true); };

        // Pause menu controls (used by pause_menu.lua)
        engineTable["is_paused"] = []() { return g_Paused; };
        engineTable["pause"] = [window]() {
            if (g_CurrentGameState != GameState::PLAYING) return;
            g_Paused = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["resume"] = [window]() {
            g_Paused = false;
            g_SettingsInGame = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        };

        // ---- Settings (used by settings_menu.lua and pause_menu.lua) ----
        // engine.open_settings() - from the pause menu (the title screen's Settings button opens it too)
        engineTable["open_settings"] = [&lua]() {
            if (g_CurrentGameState == GameState::PLAYING) {
                g_Paused = true;
                g_SettingsInGame = true;
            } else {
                g_CurrentGameState = GameState::SETTINGS;
            }
            callLuaUI(&lua, "on_settings_opened");
        };
        engineTable["close_settings"] = []() { closeSettings(); };
        engineTable["is_in_game"] = []() { return g_CurrentGameState == GameState::PLAYING; };

        // engine.get_keybinds() -> { { action, label, key, key_name, default_key }, ... } in display order
        engineTable["get_keybinds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& a : keyActions()) {
                sol::table e = lua.create_table();
                const int key = keyForAction(a.id);
                e["action"] = std::string(a.id);
                e["label"] = std::string(a.label);
                e["key"] = key;
                e["key_name"] = keyName(key);
                e["default_key"] = a.defaultKey;
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_keybind(action, key) -> true, or false + reason
        engineTable["set_keybind"] = [](const std::string& action, int key) {
            std::string error;
            const bool ok = setKeybind(action, key, error);
            return std::make_tuple(ok, error);
        };
        engineTable["reset_keybinds"] = []() { resetKeybinds(); };
        engineTable["key_name"] = [](int key) { return keyName(key); };

        // engine.get_video_settings() -> { { id, value, min, max, step }, ... }
        engineTable["get_video_settings"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& s : kVideoSettings) {
                sol::table e = lua.create_table();
                e["id"] = std::string(s.id);
                e["value"] = currentVideoSetting(s.id);
                e["min"] = s.minV;
                e["max"] = s.maxV;
                e["step"] = s.step;
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_video_setting(id, value) -> true, or false + reason. Applies immediately.
        engineTable["set_video_setting"] = [](const std::string& id, int value) {
            std::string error;
            const bool ok = setVideoSetting(id, value, error);
            return std::make_tuple(ok, error);
        };

        engineTable["quit_to_menu"] = [window, &lua]() {
            g_SettingsInGame = false;
            // Save, then unload the world so the next one starts clean
            if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
            const bool wasOnline = g_Client.active();
            if (g_Client.active()) g_Client.disconnect();
            if (g_Server.running()) g_Server.stop();
            g_NetError.clear();
            if (g_WorldInstance) {
                g_WorldInstance->reset();
                g_WorldInstance->setSaveDirectory("");
            }
            g_ModelInstances.clear();   // models placed in the old world
            // The hotbar belongs to that world (saved just above): nothing of it stays behind
            if (g_Hotbar) for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
            g_ModStorage.clear();       // (so does mod storage - the next world loads its own)
            g_ThirdPerson = false;
            g_Paused = false;
            g_Inventory.setCreativeOpen(false);
            g_CurrentGameState = wasOnline ? GameState::MULTIPLAYER : GameState::MENU;
            if (wasOnline) callLuaUI(&lua, "on_multiplayer_opened");
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["start_game"] = [window]() { beginLoadingWorld(window); };
        // engine.take_world_error() -> why the last world was closed or couldn't be opened, once
        //   (e.g. its world generator failed), else nil
        engineTable["take_world_error"] = [](sol::this_state ts) -> sol::object {
            if (g_WorldError.empty()) return sol::make_object(ts, sol::lua_nil);
            std::string e;
            e.swap(g_WorldError);
            return sol::make_object(ts, e);
        };

        // ---- World saves (used by world_select.lua) ----
        // engine.list_worlds() -> { { name = "My World", seed = "123" }, ... }  newest first
        engineTable["list_worlds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : listSavedWorlds()) {
                sol::table e = lua.create_table();
                e["name"] = w.name;
                e["seed"] = w.seed;
                if (!w.gameMode.empty()) e["game_mode"] = w.gameMode;
                t[idx++] = e;
            }
            return t;
        };
        // engine.create_world(name, seedText [, options]) -> folder name actually used, or nil + error
        //   options = { game_mode = "survival", ... }: saved with the world, read by mods as
        //   engine.get_storage("world:game_mode")
        engineTable["create_world"] = [&lua](const std::string& name, sol::optional<std::string> seed,
                                             sol::optional<sol::table> options) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil),
                                       std::string("Restart the game to create worlds for this game."));
            }
            std::map<std::string, std::string> opts;
            if (options) {
                options->for_each([&opts](const sol::object& k, const sol::object& v) {
                    if (!k.is<std::string>()) return;
                    if (v.is<std::string>()) opts[k.as<std::string>()] = v.as<std::string>();
                    else if (v.is<bool>()) opts[k.as<std::string>()] = v.as<bool>() ? "true" : "false";
                    else if (v.is<double>()) opts[k.as<std::string>()] = std::to_string(v.as<double>());
                });
            }
            return std::make_tuple(sol::make_object(lua, createWorld(name, seed.value_or(""), opts)), std::string());
        };
        // engine.play_world(name) -> true, or false + error message
        engineTable["play_world"] = [window](const std::string& name) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(false, std::string("Restart the game to play worlds for this game."));
            }
            WorldMeta meta;
            std::string why;
            if (readWorldMeta(savesRoot() / name / "world.dat", meta) && !worldBelongsToLoadedGame(meta, &why)) {
                return std::make_tuple(false, why);
            }
            g_ActiveWorldName = name;
            beginLoadingWorld(window);
            return std::make_tuple(true, std::string());
        };
        // engine.delete_world(name) -> true, or false + error message
        engineTable["delete_world"] = [](const std::string& name) {
            std::string error;
            const bool ok = deleteWorld(name, error);
            return std::make_tuple(ok, error);
        };
        // engine.get_game_info() -> { selected, loaded, title, can_play }
        engineTable["get_game_info"] = [&lua]() {
            sol::table t = lua.create_table();
            std::string title = g_ActiveGameFolder;
            for (const auto& g : getAvailableGames()) {
                if (g.folderName == g_ActiveGameFolder) { title = g.title; break; }
            }
            t["selected"] = g_ActiveGameFolder;
            t["loaded"] = g_LoadedGameFolder;
            t["title"] = title;
            t["can_play"] = selectedGameIsLoaded();
            return t;
        };
        engineTable["save_world"] = []() {
            if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
        };
        engineTable["back_to_title"] = []() { g_CurrentGameState = GameState::MENU; };

        // ---- Multiplayer (used by multiplayer.lua and pause_menu.lua) ----
        // engine.list_servers() -> { { name = "Home", address = "192.168.1.20" }, ... }
        engineTable["list_servers"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& s : loadServers()) {
                sol::table e = lua.create_table();
                e["name"] = s.name;
                e["address"] = s.address;
                t[i++] = e;
            }
            return t;
        };
        // engine.save_servers(list)
        engineTable["save_servers"] = [](sol::table list) {
            std::vector<SavedServer> servers;
            for (size_t i = 1; i <= list.size(); ++i) {
                sol::optional<sol::table> e = list[i];
                if (!e) continue;
                servers.push_back({e->get_or<std::string>("name", "Server"),
                                   e->get_or<std::string>("address", "")});
            }
            saveServers(servers);
        };
        engineTable["get_player_name"] = []() { return playerName(); };
        // engine.set_player_name(name) -> true, or false + error
        // engine.set_player_password(password) - "" removes it. Only a hash is stored (settings.txt),
        // and it never leaves this PC: servers get a per-server challenge answer instead (Auth.h),
        // which protects your name there (Settings > Player) and is no use on any other server.
        engineTable["set_player_password"] = [](const std::string& password) {
            if (password.empty()) g_Settings.erase("player_password_token");
            else g_Settings["player_password_token"] = net::passwordToken(password);
            saveSettings();
            return true;
        };
        engineTable["has_player_password"] = []() {
            const auto it = g_Settings.find("player_password_token");
            return it != g_Settings.end() && !it->second.empty();
        };
        engineTable["set_player_name"] = [](const std::string& name) {
            std::string error;
            const bool ok = changePlayerName(name, error);
            return std::make_tuple(ok, error);
        };
        // engine.join_server(address) -> true, or false + error (connecting continues in the background)
        engineTable["join_server"] = [](const std::string& address) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(false, std::string("Restart the game with this game selected to join its servers."));
            }
            if (g_Server.running()) g_Server.stop();
            std::string error;
            g_NetError.clear();
            const int view = g_WorldInstance ? g_WorldInstance->renderDistance() : 8;
            const std::string token = g_Settings.count("player_password_token") ? g_Settings["player_password_token"] : "";
            // The identity this server had last time (settings.txt server_id.<host:port>): the
            // password is only used to answer the server we joined before (Auth.h)
            std::string knownId;
            {
                std::string host;
                uint16_t port = 0;
                if (net::parseAddress(address, host, port)) {
                    const auto it = g_Settings.find("server_id." + host + ":" + std::to_string(port));
                    if (it != g_Settings.end()) knownId = it->second;
                }
            }
            if (!g_Client.connect(*g_WorldInstance, address, playerName(), g_LoadedGameFolder, view, error, token, knownId)) {
                return std::make_tuple(false, error);
            }
            return std::make_tuple(true, std::string());
        };
        engineTable["cancel_join"] = []() {
            if (g_Client.active()) g_Client.disconnect();
            g_NetError.clear();
        };
        // engine.get_connection_status() -> { state = "idle"|"connecting"|"joined", address, error }
        engineTable["get_connection_status"] = [&lua]() {
            sol::table t = lua.create_table();
            const auto st = g_Client.state();
            t["state"] = st == GameClient::State::Joined ? "joined"
                       : st == GameClient::State::Connecting ? "connecting" : "idle";
            t["address"] = g_Client.address();
            if (!g_NetError.empty()) t["error"] = g_NetError;
            return t;
        };
        // engine.host_game([port]) -> true, or false + error   (opens the current world to others)
        engineTable["host_game"] = [](sol::optional<int> port) {
            if (!g_WorldInstance || g_CurrentGameState != GameState::PLAYING || g_Client.active()) {
                return std::make_tuple(false, std::string("Open a singleplayer world first"));
            }
            if (!net::validPlayerName(playerName())) {
                return std::make_tuple(false, std::string("Set your name in Settings > Player first"));
            }
            const int p = port.value_or(serverPort());
            if (p <= 0 || p > 65535) return std::make_tuple(false, std::string("Invalid port"));
            g_Server.setInfo(serverMotd(), serverMaxPlayers());
            g_Server.setIcon(loadServerIcon());
            for (const auto& g : getAvailableGames()) {   // "Plays <title>" in other players' lists
                if (g.folderName == g_LoadedGameFolder) g_Server.setGameTitle(g.title);
            }
            std::string error;
            // This game's identity as a host (made once): players' password answers are bound to it
            if (g_Settings["lan_server_id"].size() < 16) {
                g_Settings["lan_server_id"] = auth::randomHex(16);
                saveSettings();
            }
            g_Server.setServerId(g_Settings["lan_server_id"]);
            if (!g_Server.start(*g_WorldInstance, static_cast<uint16_t>(p), g_LoadedGameFolder, playerName(), error)) {
                return std::make_tuple(false, error);
            }
            for (const auto& [k, v] : g_SharedValues) g_Server.setShared(k, v);   // weather etc.
            g_Server.setShared(ClientModRules::kSharedKey, lanClientModRules().encode());   // players' client mods
            // Players who join get this world's game mode; only the host can change it
            {
                const auto it = g_ModStorage.find("world:game_mode");
                g_Server.setDefaultMode(it != g_ModStorage.end() && !it->second.empty() ? it->second : std::string("survival"));
            }
            g_Server.onCommand = [](const std::string& name, const std::string& command) {
                const std::string word = command.substr(0, command.find(' '));
                if (word == "gamemode" || word == "gm") {
                    if (command.find(' ') == std::string::npos) {
                        g_Server.messagePlayer(name, "Your game mode: " + g_Server.playerMode(name) + ". Only the host can change it.",
                                               glm::vec3(0.75f, 0.85f, 1.0f));
                    } else {
                        g_Server.messagePlayer(name, "Only the host can change game modes", glm::vec3(1.0f, 0.55f, 0.55f));
                    }
                    return;
                }
                g_Server.messagePlayer(name, "This game doesn't run commands (only /gamemode to see your mode)",
                                       glm::vec3(1.0f, 0.55f, 0.55f));
            };
            askFirewallOnce("firewall_ok_game", "", "BlockSmith");
            if (g_ChatSystem) {
                g_ChatSystem->addMessage("Hosting on port " + std::to_string(p) + ". Friends can join with your IP address.",
                                         glm::vec3(1.0f, 1.0f, 0.4f));
            }
            return std::make_tuple(true, std::string());
        };
        engineTable["stop_hosting"] = []() {
            if (!g_Server.running()) return;
            g_Server.stop();
            if (g_ChatSystem) g_ChatSystem->addMessage("Stopped hosting", glm::vec3(1.0f, 1.0f, 0.4f));
        };
        // engine.get_network_info() -> { hosting, joined, port, players, address }
        engineTable["get_network_info"] = [&lua]() {
            sol::table t = lua.create_table();
            t["hosting"] = g_Server.running();
            t["joined"] = g_Client.state() == GameClient::State::Joined;
            t["port"] = static_cast<int>(g_Server.port());
            t["players"] = g_Server.running() ? g_Server.playerCount()
                         : static_cast<int>(g_Client.players().size()) + 1;
            t["address"] = g_Client.address();
            return t;
        };
        // ---- Server settings (Settings > Server) ----
        // engine.get_server_settings() -> { motd, max_players, port }
        engineTable["get_server_settings"] = [&lua]() {
            sol::table t = lua.create_table();
            t["motd"] = serverMotd();
            t["max_players"] = serverMaxPlayers();
            t["port"] = serverPort();
            return t;
        };
        // engine.set_server_setting(id, value) -> true, or false + error. Applies straight away
        // while hosting (the port only next time you host).
        engineTable["set_server_setting"] = [](const std::string& id, const std::string& value) {
            if (id == "motd") {
                std::string motd;
                for (char ch : value) if (ch >= 32 && ch < 127) motd += ch;
                if (motd.size() > static_cast<size_t>(net::kMaxMotdLength)) motd.resize(net::kMaxMotdLength);
                g_Settings["server_motd"] = motd;
            } else if (id == "max_players" || id == "port") {
                int v = 0;
                try { v = std::stoi(value); } catch (...) { return std::make_tuple(false, std::string("Type a number")); }
                if (id == "max_players") {
                    if (v < 2 || v > 200) return std::make_tuple(false, std::string("Max players must be 2-200"));
                    g_Settings["server_max_players"] = std::to_string(v);
                } else {
                    if (v < 1024 || v > 65535) return std::make_tuple(false, std::string("Port must be 1024-65535"));
                    g_Settings["server_port"] = std::to_string(v);
                }
            } else {
                return std::make_tuple(false, std::string("Unknown setting"));
            }
            saveSettings();
            if (g_Server.running()) g_Server.setInfo(serverMotd(), serverMaxPlayers());
            return std::make_tuple(true, std::string());
        };

        // ---- Server list status (multiplayer.lua) ----
        // engine.ping_server(address)   starts checking a server (answer comes within ~5 s)
        engineTable["ping_server"] = [](const std::string& address) { g_Pinger.ping(address); };
        // engine.get_server_status(address) -> nil if never pinged, else
        //   { state = "pinging"|"online"|"offline", motd, players, max_players, ping, names = {...},
        //     version, game, compatible, error }
        engineTable["get_server_status"] = [&lua](const std::string& address) -> sol::object {
            const ServerStatus* st = g_Pinger.status(address);
            if (!st) return sol::make_object(lua, sol::lua_nil);
            sol::table t = lua.create_table();
            t["state"] = st->state == ServerStatus::State::Online ? "online"
                       : st->state == ServerStatus::State::Offline ? "offline" : "pinging";
            t["motd"] = st->motd;
            t["players"] = st->players;
            t["max_players"] = st->maxPlayers;
            t["ping"] = st->ping;
            t["version"] = st->version;
            t["game"] = st->game;
            t["game_title"] = st->gameTitle.empty() ? st->game : st->gameTitle;
            // Is that game installed here? (then the player only needs to select it)
            bool installed = false;
            for (const auto& g : getAvailableGames()) if (g.folderName == st->game) installed = true;
            t["game_installed"] = installed;
            t["same_game"] = st->game.empty() || st->game == g_LoadedGameFolder;
            t["compatible"] = st->compatible;
            t["error"] = st->error;
            sol::table names = lua.create_table();
            for (size_t i = 0; i < st->names.size(); ++i) names[i + 1] = st->names[i];
            t["names"] = names;
            return t;
        };

        // ---- My Servers (server_manager.lua): dedicated servers set up from the game ----
        // engine.list_games() -> { { folder, title, description }, ... }
        engineTable["list_games"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& g : getAvailableGames()) {
                sol::table e = lua.create_table();
                e["folder"] = g.folderName;
                e["title"] = g.title;
                e["description"] = g.description;
                t[i++] = e;
            }
            return t;
        };
        // engine.list_server_profiles() -> { "Survival", "Skyblock", ... }   (folders in servers/)
        engineTable["list_server_profiles"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& p : listServerProfiles()) t[i++] = p;
            return t;
        };
        // engine.get_server_profile(name) -> { name=..., motd=..., game=..., port=..., ... } (all strings)
        engineTable["get_server_profile"] = [&lua](const std::string& profile) {
            sol::table t = lua.create_table();
            for (const auto& [k, v] : readServerProfile(profile)) t[k] = v;
            return t;
        };
        // engine.save_server_profile(name, settings) -> true, or false + error
        engineTable["save_server_profile"] = [](const std::string& profile, sol::table settings) {
            std::map<std::string, std::string> values = readServerProfile(profile);
            for (const auto& s : serverSettingInfo()) {
                sol::object v = settings[s.key];
                if (v.is<std::string>()) values[s.key] = v.as<std::string>();
                else if (v.is<double>()) values[s.key] = std::to_string(static_cast<long long>(v.as<double>()));
                else if (v.is<bool>()) values[s.key] = v.as<bool>() ? "true" : "false";
            }
            std::string error;
            const bool ok = writeServerProfile(profile, values, error);
            return std::make_tuple(ok, error);
        };
        // engine.create_server_profile(displayName) -> folder name, or nil + error
        engineTable["create_server_profile"] = [&lua](const std::string& displayName) {
            const std::string folder = serverProfileFolder(displayName);
            if (folder.empty()) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), std::string("Type a name with letters or numbers"));
            }
            std::error_code ec;
            if (fs::exists(serverProfilesRoot() / folder, ec)) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), std::string("There's already a server called '" + folder + "'"));
            }
            // Defaults: this game, a world named after the server, the next free port
            std::map<std::string, std::string> values;
            values["name"] = displayName;
            values["game"] = g_LoadedGameFolder;
            values["world"] = folder;
            int port = net::kDefaultPort;
            for (const auto& other : listServerProfiles()) {
                port = std::max(port, settingIntFrom(readServerProfile(other), "port", net::kDefaultPort) + 1);
            }
            if (port == static_cast<int>(net::kLanPort)) ++port;   // the LAN list uses 30001
            values["port"] = std::to_string(port);
            std::string error;
            if (!writeServerProfile(folder, values, error)) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), error);
            }
            return std::make_tuple(sol::make_object(lua, folder), std::string());
        };
        // engine.delete_server_profile(name) -> true, or false + error (its world in saves/ is kept)
        engineTable["delete_server_profile"] = [](const std::string& profile) {
            if (serverProfileFolder(profile) != profile || profile.empty()) {
                return std::make_tuple(false, std::string("Invalid server name"));
            }
            std::error_code ec;
            fs::remove_all(serverProfilesRoot() / profile, ec);
            if (ec) return std::make_tuple(false, "Couldn't delete it: " + ec.message());
            return std::make_tuple(true, std::string());
        };
        // engine.list_mods_for_game(game) -> { { id, title, description, source = "Game"|"Global", required }, ... }
        engineTable["list_mods_for_game"] = [&lua](const std::string& game) {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& m : listModsForGame(game)) {
                sol::table e = lua.create_table();
                e["id"] = m.id;
                e["title"] = m.title;
                e["description"] = m.description;
                e["source"] = m.source;
                e["required"] = m.required;
                t[i++] = e;
            }
            return t;
        };
        // engine.start_server_profile(name) -> true, or false + error (opens the server's window)
        engineTable["start_server_profile"] = [](const std::string& profile) {
            const fs::path serverExe = findServerExe();
            if (!serverExe.empty()) askFirewallOnce("firewall_ok_server", serverExe.string(), "BlockSmith Server");
            std::string error;
            const bool ok = startServerProfile(profile, error);
            return std::make_tuple(ok, error);
        };
        engineTable["stop_server_profile"] = [](const std::string& profile) { stopServerProfile(profile); };
        engineTable["open_server_profile_folder"] = [](const std::string& profile) {
            openFolderInExplorer(serverProfilesRoot() / profile);
        };
        engineTable["has_server_build"] = []() { return !findServerExe().empty(); };
        // engine.allow_firewall() -> asks Windows again to let this game accept connections
        //   (for a "Fix: friends can't join" button). Returns ok, status: true, "allowed" (always,
        //   off Windows) / false, "failed" / false, "pending" (Windows is still applying it; the
        //   game records the result itself when it finishes - asking again meanwhile starts nothing)
        engineTable["allow_firewall"] = []() {
            const FirewallStatus st = allowThroughFirewall("", "BlockSmith");
            if (st == FirewallStatus::Allowed) { g_Settings["firewall_ok_game"] = "1"; saveSettings(); }
            if (st == FirewallStatus::Pending) g_FirewallWatch["BlockSmith"] = "firewall_ok_game";
            return std::make_tuple(st == FirewallStatus::Allowed,
                                   std::string(st == FirewallStatus::Allowed ? "allowed"
                                               : st == FirewallStatus::Pending ? "pending" : "failed"));
        };

        // engine.get_lan_games() -> { { address, host, motd, players, max_players }, ... }
        engineTable["get_lan_games"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& g : g_Lan.games()) {
                sol::table e = lua.create_table();
                e["address"] = g.address;
                e["host"] = g.hostName;
                e["motd"] = g.motd;
                e["players"] = g.players;
                e["max_players"] = g.maxPlayers;
                t[i++] = e;
            }
            return t;
        };
        // engine.is_scanning_lan() -> true while listening for LAN games
        engineTable["is_scanning_lan"] = []() { return g_Lan.running(); };
        // engine.has_server_icon() -> true if server_icon.png exists (Settings > Server)
        engineTable["has_server_icon"] = []() {
            std::error_code ec;
            return fs::exists(serverIconFile(), ec);
        };

        // engine.get_player_list() -> { title = "...", players = { { name, ping, host, you }, ... } }
        //   ping is in ms (-1 = not known yet). Used by player_list.lua (hold Tab).
        engineTable["get_player_list"] = [&lua]() {
            sol::table t = lua.create_table();
            sol::table list = lua.create_table();
            int i = 1;
            auto add = [&](const std::string& name, int ping, bool host, bool you) {
                sol::table e = lua.create_table();
                e["name"] = name;
                e["ping"] = ping;
                e["host"] = host;
                e["you"] = you;
                list[i++] = e;
            };
            if (g_Server.running()) {
                t["title"] = "Hosting on port " + std::to_string(g_Server.port());
                add(playerName(), 0, true, true);
                for (const auto& [id, v] : g_Server.players()) add(v.name, v.ping, false, false);
            } else if (g_Client.state() == GameClient::State::Joined) {
                t["title"] = g_Client.address();
                for (const auto& [id, v] : g_Client.players()) add(v.name, v.ping, id == 0, false);
                add(playerName(), g_Client.ping(), false, true);
            } else {
                t["title"] = "Singleplayer";
                add(playerName(), -1, false, true);
            }
            t["players"] = list;
            return t;
        };
        // engine.send_chat(text) -> sends a chat message to everyone (multiplayer only). True if sent.
        engineTable["send_chat"] = [](const std::string& text) {
            if (g_Server.running()) { g_Server.sendChat(text); return true; }
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendChat(text); return true; }
            return false;
        };

        // ---- Mods screen (used by mods_menu.lua) ----
        // engine.list_mods() -> { { id, title, author, description, version, depends,
        //                           source, path, status, error, lua_files, is_folder }, ... }
        // status is "loaded", "error" or "not_loaded"
        engineTable["list_mods"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& m : listInstalledMods()) {
                sol::table e = lua.create_table();
                e["id"] = m.id;
                e["title"] = m.title;
                e["author"] = m.author;
                e["description"] = m.description;
                e["version"] = m.version;
                e["depends"] = m.depends;
                e["source"] = m.source;
                e["path"] = m.path;
                e["status"] = m.status;
                e["error"] = m.error;
                e["lua_files"] = m.luaFiles;
                e["is_folder"] = m.isFolder;
                e["enabled"] = m.enabled;
                e["required"] = m.required;
                e["restart_needed"] = m.restartNeeded;
                t[idx++] = e;
            }
            // The player's client mods (clientmods/), after the others. Off until switched on.
            if (g_ClientMods) {
                for (const auto& m : g_ClientMods->list()) {
                    sol::table e = lua.create_table();
                    e["id"] = "client:" + m.name;
                    e["title"] = m.title;
                    e["author"] = m.author;
                    e["description"] = m.description;
                    e["version"] = m.version;
                    e["depends"] = m.depends;
                    e["source"] = std::string("Client mod");
                    e["path"] = m.path;
                    e["status"] = m.status;
                    e["error"] = m.error;
                    e["lua_files"] = m.luaFiles;
                    e["is_folder"] = true;
                    e["enabled"] = m.enabled;
                    e["required"] = false;
                    e["restart_needed"] = m.restartNeeded;
                    e["client"] = true;
                    t[idx++] = e;
                }
            }
            return t;
        };
        // engine.get_client_mods_folder() -> the full path of the clientmods folder in use
        engineTable["get_client_mods_folder"] = []() -> std::string {
            if (!g_ClientMods) return "";
            std::error_code ec;
            return fs::absolute(g_ClientMods->folder(), ec).string();
        };
        // engine.open_mods_folder(["client"]): the mods folder, or "client" = the client mods folder
        engineTable["open_mods_folder"] = [](sol::optional<std::string> which) {
            if (which && *which == "client" && g_ClientMods) {
                std::error_code ec;
                const fs::path dir = g_ClientMods->folder();
                fs::create_directories(dir, ec);
                std::string abs = fs::absolute(dir, ec).string();
                abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
                const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
                const std::string cmd = "open \"" + abs + "\"";
#else
                const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
                std::cout << "[Mods] Opening " << abs << std::endl;
                (void)std::system(cmd.c_str());
                return;
            }
            openModsFolder();
        };

        // ---- Texture packs (used by texture_packs.lua) ----
        // engine.list_texture_packs() -> { { id, title, author, description, path, images, replaces, active }, ... }
        //   The first entry is always the default textures (id = "").
        engineTable["list_texture_packs"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& p : listTexturePacks()) {
                sol::table e = lua.create_table();
                e["id"] = p.id;
                e["title"] = p.title;
                e["author"] = p.author;
                e["description"] = p.description;
                e["path"] = p.path;
                e["images"] = p.images;
                e["replaces"] = p.replaces;
                e["total"] = static_cast<int>(g_LayerSources.size() + g_StandardTextureSources.size());
                e["active"] = (p.id == g_ActiveTexturePack);
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_texture_pack(id) -> true, or false + reason. "" = default. Applies immediately.
        engineTable["set_texture_pack"] = [](const std::string& id) {
            std::string error;
            const bool ok = setTexturePack(id, error);
            return std::make_tuple(ok, error);
        };
        engineTable["get_texture_pack"] = []() { return g_ActiveTexturePack; };
        engineTable["open_texture_packs_folder"] = []() { openTexturePacksFolder(); };
        // engine.set_mod_enabled(id, on) -> true, or false + reason. Applies on the next start.
        engineTable["set_mod_enabled"] = [](const std::string& id, bool on) {
            std::string error;
            if (id.rfind("client:", 0) == 0) {   // a client mod (clientmods/mods.conf)
                const bool ok = g_ClientMods && g_ClientMods->setEnabled(id.substr(7), on, error);
                if (!g_ClientMods && error.empty()) error = "Client mods aren't available.";
                return std::make_tuple(ok, error);
            }
            const bool ok = setModEnabled(id, on, error);
            return std::make_tuple(ok, error);
        };

        World world;
        g_WorldInstance = &world;
        world.setLighting(true);   // block light (Light.h); the dedicated server doesn't need it
        WorldRenderer worldRenderer;          // meshes and draws `world` (client only)
        g_WorldRenderer = &worldRenderer;

        // Worlds are tied to the game whose mods are loaded now (see openActiveWorld)
        g_LoadedGameFolder = g_ActiveGameFolder;

        // Player settings (key bindings, FOV, ...) from settings.txt
        loadSettings();

        // Every player always has a name (used in chat and multiplayer)
        g_ChatSystem->setPlayerName(ensurePlayerName());
        g_ChatSystem->registerCommand("name", "[new name]", "Show or change your player name.");
        g_ChatSystem->registerCommand("netstats", "", "Show FPS, ping and multiplayer update rates.");
        // Typed chat goes to everyone when playing online (returns false offline = show locally)
        g_ChatSystem->setMessageSender([](const std::string& text) -> bool {
            if (g_Server.running()) { g_Server.sendChat(text); return true; }
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendChat(text); return true; }
            return false;
        });

        // The chosen texture pack must be known before mods load their textures
        g_ActiveTexturePack = readActiveTexturePack();
        if (!g_ActiveTexturePack.empty()) {
            std::cout << "[Texture] Using texture pack '" << g_ActiveTexturePack << "'" << std::endl;
        }

        std::vector<std::string> modSearchPaths = { "mods", "../mods", "../../mods", "games/" + g_ActiveGameFolder + "/mods" };
        buildTextureIndex();   // pictures anywhere in the game / mods can be used by name
        std::vector<fs::path> modFiles;
        std::set<std::string> initFolders;

        std::set<std::string> seenModRoots;   // the same folder reached by two paths is only loaded once
        for (const auto& searchPath : modSearchPaths) {
            std::string resolvedPath = searchPath;
            if (!fs::exists(resolvedPath) && fs::exists("../../" + searchPath)) resolvedPath = "../../" + searchPath;
            if (!fs::exists(resolvedPath)) continue;

            const std::string rootKey = modPathKey(resolvedPath);
            if (!seenModRoots.insert(rootKey).second) {
                std::cout << "[Mod] Search path '" << searchPath << "' leads to " << rootKey
                          << ", which is already loaded - skipping it" << std::endl;
                continue;
            }
            std::cout << "[Mod] Mod folder: " << rootKey << " (search path '" << searchPath << "')" << std::endl;

            // Mods in the mods/ folder can be switched off on the Mods screen; game mods always load.
            const bool isGlobalFolder = searchPath.rfind("games/", 0) != 0;
            std::set<std::string> reportedDisabled;
            const size_t rootStart = modFiles.size();

            for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;

                if (isGlobalFolder) {
                    std::error_code relEc;
                    const fs::path rel = fs::relative(entry.path(), resolvedPath, relEc);
                    const std::string modId = (relEc || rel.empty()) ? "" : rel.begin()->string();
                    if (modId == "clientmods") continue;   // mods/clientmods/: the player's client mods, not game mods
                    if (!modId.empty()) {
                        const bool on = modIsEnabled(resolvedPath, modId);
                        g_ModEnabledAtStartup[modId] = on;
                        if (!on) {
                            if (reportedDisabled.insert(modId).second) {
                                std::cout << "[Mod] Skipping disabled mod: " << modId << std::endl;
                            }
                            continue;
                        }
                    }
                }

                if (entry.path().filename() == "init.lua") {
                    initFolders.insert(entry.path().parent_path().string());
                }
                modFiles.push_back(entry.path());
            }
            modorder::sortRoot(modFiles, rootStart);   // alphabetical, whatever the file system's order
        }
        modorder::applyDepends(modFiles);              // mod.conf depends = ... loads after those mods

        auto isInsideInitFolder = [&initFolders](const fs::path& file) {
            for (fs::path p = file.parent_path(); ; p = p.parent_path()) {
                if (initFolders.count(p.string())) return true;
                if (p.empty() || p == p.parent_path()) return false;
            }
        };

        auto runModFile = [&](const fs::path& file) {
            g_CurrentModPath = file.parent_path().string();
            if (std::find(g_ModFolders.begin(), g_ModFolders.end(), g_CurrentModPath) == g_ModFolders.end()) {
                g_ModFolders.push_back(g_CurrentModPath);
            }
            lua["g_CurrentModPath"] = g_CurrentModPath;
            std::cout << "[Mod] Running " << modPathKey(file) << std::endl;
            try {
                lua.script_file(file.string());
                g_ModFileStatus[modPathKey(file)] = "";              // ran OK (shown on the Mods screen)
            } catch (const std::exception& e) {
                std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
                g_ModFileStatus[modPathKey(file)] = e.what();
            }
        };

        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua") runModFile(file);
        }
        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua" || isInsideInitFolder(file)) continue;
            runModFile(file);
        }

        // The player's own client mods (clientmods/), after the game's mods so they can use
        // its ui.* and sound.*. settings.txt: client_mods = false switches them all off.
        // Where client mods are: a clientmods/ folder next to mods/ or inside it, or next to the
        // game (the first one that exists; made next to mods/ if there's none)
        const fs::path clientModsFolder = [] {
            const fs::path modsDir = globalModsFolder();
            const fs::path beside = (modsDir.has_parent_path() ? modsDir.parent_path() : fs::path(".")) / "clientmods";
            const std::vector<fs::path> places = { beside, modsDir / "clientmods", "clientmods", "../clientmods",
                                                   "../../clientmods", g_ExeDir / "clientmods" };
            std::error_code ec;
            for (const auto& p : places) if (!p.empty() && fs::is_directory(p, ec)) return p;
            return beside;
        }();
        {
            std::error_code ec;
            std::cout << "[Client mods] Folder: " << fs::absolute(clientModsFolder, ec).string() << std::endl;
        }
        clientMods.setFolder(clientModsFolder.string());
        // Sounds are looked for in every mod's folder (and its sounds/), then the game's. Installed
        // as the client mods' folders become known (before their init.lua runs) and again after
        // loading; Audio's sandboxed lookups check every file against these same roots each time.
        auto installSoundFolders = [&clientMods] {
            std::vector<std::string> soundFolders = g_ModFolders;
            for (const auto& f : clientMods.modFolders()) soundFolders.push_back(f);   // client mods' sounds, last
            soundFolders.push_back("games/" + g_ActiveGameFolder);
            soundFolders.push_back("../../games/" + g_ActiveGameFolder);
            g_Audio.setSearchFolders(soundFolders);
        };
        clientMods.onModFoldersReady = installSoundFolders;
        if (settingInt("client_mods", 1) != 0 && g_Settings["client_mods"] != "false") {
            const fs::path folder = clientModsFolder;
            clientMods.showChat = [](const std::string& text, float r, float g, float b) {
                if (g_ChatSystem) g_ChatSystem->addMessage(text, glm::vec3(r, g, b));
            };
            clientMods.init(lua, folder.string());
            addClientModWorldApi(clientMods);
            clientMods.load();
            // The HUD is drawn through the game's state, like render_hud
            lua["__client_hud"] = [](float W, float H) { if (g_ClientMods) g_ClientMods->hud(W, H); };
            g_ChatSystem->setLocalCommandHandler([](const std::string& text) {
                return g_ClientMods && g_ClientMods->command(text);
            });
            g_ChatSystem->setMessageListener([](const std::string& text) {
                if (g_ClientMods) g_ClientMods->chatMessage(text);
            });
        }

        // Saved video settings win over values set by mods during loading
        applyStoredVideoSettings();

        g_Lua = &lua;
        installSoundFolders();   // (also with client mods off)
        // Other players building (and, when hosting, players editing our world): their
        // break/place sounds where it happened. Fluids flowing make no sound.
        {
            auto editSound = [](const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
                FluidRegistry::get().ensureFresh();
                if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
                const glm::vec3 at = glm::vec3(pos) + glm::vec3(0.5f);
                if (newId == 0) playBlockSound(oldId, "dug", at);
                else playBlockSound(newId, "place", at);
            };
            g_Client.onBlockChanged = editSound;
            // Hosting: other players' edits also reach on_block_broken / on_block_placed here,
            // since the host decides the world (leaf decay and the like)
            g_Server.onPlayerEdit = [editSound, &lua](const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
                editSound(pos, oldId, newId);
                if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
                auto& reg = BlockRegistry::getInstance();
                const char* hookName = newId == 0 ? "on_block_broken" : "on_block_placed";
                sol::protected_function hook = lua[hookName];
                if (!hook.valid()) return;
                auto r = newId == 0 ? hook(pos.x, pos.y, pos.z, reg.getBlock(oldId).name, 0)
                                    : hook(pos.x, pos.y, pos.z, reg.getBlock(newId).name);
                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] " << hookName << ": " << err.what() << std::endl; }
            };
        }
        // Luanti's crack picture, if the game has one and no mod chose another
        if (g_CrackTexture == 0) {
            const std::vector<std::string> places = { "textures/crack_anylength.png", "../../textures/crack_anylength.png",
                "games/" + g_ActiveGameFolder + "/textures/crack_anylength.png",
                "../../games/" + g_ActiveGameFolder + "/textures/crack_anylength.png" };
            for (const auto& f : places) {
                std::error_code fec;
                if (fs::exists(f, fec)) {
                    sol::protected_function setCrack = lua["engine"]["set_dig_crack_texture"];
                    if (setCrack.valid()) setCrack("crack_anylength.png");
                    std::cout << "[Dig] Using crack_anylength.png (" << g_CrackStages << " stages)" << std::endl;
                    break;
                }
            }
        }

        // lua_api.txt: everything modders can use in this build (see writeApiReference)
        writeApiReference(lua);
        engineTable["write_api_reference"] = [&lua]() { return writeApiReference(lua); };

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

        // New chunks are generated on background threads (singleplayer and LAN hosting alike),
        // so exploring never makes the game - or the players connected to it - stutter.
        // Two cores stay free: this thread and the graphics driver.
        CaveGenerator::prepare();
        std::vector<fs::path> genModFiles;
        for (const auto& f : modFiles) if (f.filename() == "init.lua") genModFiles.push_back(f);
        for (const auto& f : modFiles) if (f.filename() != "init.lua" && !isInsideInitFolder(f)) genModFiles.push_back(f);
        ChunkGenerator chunkGenerator(0, [genModFiles]() { return makeWorldGenLua(genModFiles); }, world.seed(), 2);
        world.setGenerator(&chunkGenerator);
        std::cout << "[World] Generating chunks on " << chunkGenerator.threadCount() << " background thread"
                  << (chunkGenerator.threadCount() == 1 ? "" : "s") << std::endl;

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, vertexShaderSource));
        glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource));
        glLinkProgram(shaderProgram);
        glUseProgram(shaderProgram);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_WeatherLight"), 1.0f);
        glUniform4f(glGetUniformLocation(shaderProgram, "u_FallbackLight"), 1.0f, 0.0f, 0.0f, 0.0f);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_MinLight"), g_LightSettings.minLight);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_Brightness"), g_LightSettings.brightness);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_BlockLightScale"), g_LightSettings.blockLightScale);

        GLuint sunProgram = glCreateProgram();
        glAttachShader(sunProgram, compileShader(GL_VERTEX_SHADER, sunVertexShaderSource));
        glAttachShader(sunProgram, compileShader(GL_FRAGMENT_SHADER, sunFragmentShaderSource));
        glLinkProgram(sunProgram);

        GLuint wireframeProgram = glCreateProgram();
        glAttachShader(wireframeProgram, compileShader(GL_VERTEX_SHADER, wireframeVertexShaderSource));
        glAttachShader(wireframeProgram, compileShader(GL_FRAGMENT_SHADER, wireframeFragmentShaderSource));
        glLinkProgram(wireframeProgram);

        float sunVertices[] = { -12.0f, 12.0f, 0.0f, 0.0f, 1.0f, -12.0f, -12.0f, 0.0f, 0.0f, 0.0f, 12.0f, -12.0f, 0.0f, 1.0f, 0.0f, 12.0f, 12.0f, 0.0f, 1.0f, 1.0f };
        unsigned int sunIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO); glGenBuffers(1, &sunVBO); glGenBuffers(1, &sunEBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        float crosshairVertices[] = { -0.015f, 0.0f, 1,1,1,0.8f, 0.015f, 0.0f, 1,1,1,0.8f, 0.0f, -0.025f, 1,1,1,0.8f, 0.0f, 0.025f, 1,1,1,0.8f };
        GLuint crosshairVAO, crosshairVBO;
        glGenVertexArrays(1, &crosshairVAO); glGenBuffers(1, &crosshairVBO);
        glBindVertexArray(crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

        Player player;
        ParticleSystem particles;   // block-break particles (needs the GL context, so created here)
        PlayerAvatars playerAvatars;   // other players in multiplayer
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);

        ctx.player = &player; ctx.lua = &lua;
        g_PlayerInstance = &player;

        glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
            if (g_CurrentGameState == GameState::PLAYING) {
                if (g_ChatSystem && g_ChatSystem->isOpen) return;
                EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (c && c->player && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                    c->player->handleMouseLook(xpos, ypos);
                }
            }
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
            // Settings > Controls waiting for a new key: any mouse button can be the answer.
            // handle_settings_mouse(code) -> true = used (code = 1000 + button, as keybinds store it)
            if (action == GLFW_PRESS && settingsOpen()) {
                EngineContext* sctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (sctx && sctx->lua) {
                    sol::protected_function f = (*sctx->lua)["handle_settings_mouse"];
                    if (f.valid()) {
                        auto r = f(kMouseKeyBase + button);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] handle_settings_mouse: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) return;
                    }
                }
            }
            // A mod's inventory screen: handle_inventory_click(mx, my, W, H, "left"|"right", shift)
            if (g_LuaInventoryOpen && g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS &&
                (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;
                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, sw, sh);
                callLuaUI(ctx->lua, "handle_inventory_click", mx, my, sw, sh,
                          std::string(button == GLFW_MOUSE_BUTTON_LEFT ? "left" : "right"),
                          (mods & GLFW_MOD_SHIFT) != 0);
                return;
            }
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;

                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, sw, sh);

                if (g_CurrentGameState == GameState::MENU && g_MainMenu) {
                    std::string actionResult = g_MainMenu->handleMouseClick(mx, my, sw, sh);
                    if (actionResult == "play") {
                        // Singleplayer -> world selection screen (world_select.lua)
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(ctx->lua, "on_world_select_opened");
                    } else if (actionResult == "multiplayer") {
                        // Multiplayer -> server list (multiplayer.lua)
                        g_CurrentGameState = GameState::MULTIPLAYER;
                        callLuaUI(ctx->lua, "on_multiplayer_opened");
                    } else if (actionResult == "mods") {
                        // Mods -> installed mods screen (mods_menu.lua)
                        g_CurrentGameState = GameState::MODS;
                        callLuaUI(ctx->lua, "on_mods_opened");
                    } else if (actionResult == "texturepacks") {
                        // Textures -> texture pack picker (texture_packs.lua)
                        g_CurrentGameState = GameState::TEXTURE_PACKS;
                        callLuaUI(ctx->lua, "on_texture_packs_opened");
                    } else if (actionResult == "settings") {
                        // Settings -> key bindings and video settings (settings_menu.lua)
                        g_CurrentGameState = GameState::SETTINGS;
                        callLuaUI(ctx->lua, "on_settings_opened");
                    } else if (actionResult == "quit") {
                        glfwSetWindowShouldClose(win, true);
                    }
                } else if (settingsOpen()) {
                    callLuaUI(ctx->lua, "handle_settings_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                    callLuaUI(ctx->lua, "handle_world_select_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::MULTIPLAYER) {
                    callLuaUI(ctx->lua, "handle_multiplayer_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::MODS) {
                    callLuaUI(ctx->lua, "handle_mods_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                    callLuaUI(ctx->lua, "handle_texture_packs_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::PLAYING && g_Paused) {
                    // Forwarded to handle_pause_click in pause_menu.lua
                    try {
                        sol::protected_function clickFn = (*ctx->lua)["handle_pause_click"];
                        if (clickFn.valid()) {
                            sol::protected_function_result res = clickFn(mx, my, sw, sh);
                            if (!res.valid()) {
                                sol::error err = res;
                                std::cerr << "[Pause Lua] handle_pause_click: " << err.what() << std::endl;
                            }
                        }
                    } catch (...) {}
                } else if (g_CurrentGameState == GameState::PLAYING && g_Inventory.isCreativeOpen()) {
                    // Forwarded to handle_creative_click in inventory.lua
                    g_Inventory.handleCreativeClick(mx, my, static_cast<float>(sw), static_cast<float>(sh), "left",
                                                    (mods & GLFW_MOD_SHIFT) != 0);   // shift-click: to the trash

                    try {
                        sol::state* lua = ctx->lua;
                        sol::function handleInvClick = (*lua)["handle_inventory_click"];
                        if (handleInvClick.valid()) {
                            handleInvClick(mx, my, sw, sh);
                        }
                    } catch (...) {}
                }
            }
        });

        float lastFrameTime = static_cast<float>(glfwGetTime());
        bool leftMousePressedLast = false;
        bool rightMousePressedLast = false;

        // The world's generator can't run (World::fatalError): save what there is, leave the world
        // and show why on the world list - instead of loading forever or playing in a void
        auto leaveBrokenWorld = [&]() {
            g_WorldError = world.fatalError();
            std::cerr << "[World] Closing the world: " << g_WorldError << std::endl;
            sol::protected_function quit = lua["engine"]["quit_to_menu"];
            if (quit.valid()) quit();
            g_CurrentGameState = GameState::WORLD_SELECT;
            callLuaUI(&lua, "on_world_select_opened");
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);   // (read again each frame)
        };

        while (!glfwWindowShouldClose(window)) {
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;
            {
                static int frames = 0;
                static float fpsTimer = 0.0f;
                ++frames;
                fpsTimer += deltaTime;
                if (fpsTimer >= 0.5f) { g_Fps = frames / fpsTimer; frames = 0; fpsTimer = 0.0f; }
            }

            glfwPollEvents();

            g_PlayerPosition = player.position;
            {
                const glm::vec3 eye = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
                const glm::vec3 fwd(std::cos(glm::radians(player.yaw)) * std::cos(glm::radians(player.pitch)),
                                    std::sin(glm::radians(player.pitch)),
                                    std::sin(glm::radians(player.yaw)) * std::cos(glm::radians(player.pitch)));
                g_Audio.setListener(eye, fwd, glm::vec3(0.0f, 1.0f, 0.0f));
                g_Audio.update(deltaTime);
                pollFirewallUpdates(deltaTime);
            }
            g_LastMoveKeys = readMoveInput(window, player.yaw, player.pitch).keys;

            // Shared values that changed (set here, or sent by the server): on_shared_changed(key, value)
            {
                std::vector<std::string> changed;
                changed.swap(g_SharedChanged);
                if (g_Client.state() == GameClient::State::Joined) {
                    for (auto& k : g_Client.takeSharedChanges()) changed.push_back(std::move(k));
                }
                if (!changed.empty()) {
                    sol::protected_function fn = lua["on_shared_changed"];
                    for (const auto& k : changed) {
                        if (!fn.valid()) break;
                        const std::string value = g_Client.state() == GameClient::State::Joined
                            ? g_Client.getShared(k) : g_SharedValues[k];
                        auto r = fn(k, value);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_shared_changed: " << err.what() << std::endl; }
                    }
                }
            }

            // Game mode from the server (or back to our own game after leaving): on_game_mode(mode, is_operator)
            if (g_Client.takeModeChange()) {
                const std::string mode = g_Client.gameMode();
                if (!mode.empty()) {
                    g_Creative = mode == "creative";
                    g_Dig = DigState{};
                    if (!g_Creative && g_Inventory.isCreativeOpen()) g_Inventory.setCreativeOpen(false);
                }
                sol::protected_function fn = lua["on_game_mode"];
                if (fn.valid()) {
                    auto r = mode.empty() ? fn(sol::lua_nil, true) : fn(mode, g_Client.isOperator());
                    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_game_mode: " << err.what() << std::endl; }
                }
            }

            // Client mods: what the server allows, joining / leaving, and their update(dt)
            if (g_ClientMods && g_ClientMods->ready()) {
                g_ClientMods->setRules(currentClientModRules());
                const bool inWorld = g_CurrentGameState == GameState::PLAYING && g_WorldInstance;
                const std::string where = !inWorld ? "" : g_Server.running() ? "lan_host"
                                        : g_Client.state() == GameClient::State::Joined ? "server" : "singleplayer";
                g_ClientMods->update(deltaTime, where, where == "server" ? g_Client.address() : g_ActiveWorldName);
            }

            // on_update(dt): errors are printed (once until the message changes), never hidden
            {
                static std::string lastUpdateError;
                sol::protected_function onUpdate = lua["on_update"];
                if (onUpdate.valid()) {
                    sol::protected_function_result r = onUpdate(deltaTime);
                    if (!r.valid()) {
                        sol::error err = r;
                        if (lastUpdateError != err.what()) {
                            lastUpdateError = err.what();
                            std::cerr << "[Lua] on_update: " << lastUpdateError << std::endl;
                        }
                    }
                }
            }

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0) height = 1;

            bool isCursorDisabled = (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

            // ---- Multiplayer networking (every frame, also while paused or loading) ----
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                g_Pinger.update();
                if (!g_Lan.running()) g_Lan.start();   // "Scanning for games on your local network"
                g_Lan.update();
            } else if (g_Lan.running()) {
                g_Lan.stop();
            }
            if (g_Server.running()) {
                g_Server.update(player.position, player.yaw, player.pitch, deltaTime);
                for (const auto& line : g_Server.takeChat()) {
                    if (g_ChatSystem) g_ChatSystem->addMessage(line.text, line.color);
                }
            }
            if (g_Client.active()) {
                g_Client.update(MoveState{player.position, player.velocity, player.isGrounded},
                                player.yaw, player.pitch, deltaTime);
                // The server disagreed with where we moved ourselves: take its answer
                // (with our newer inputs already replayed on top)
                MoveState corrected;
                if (g_Client.takeCorrection(corrected)) {
                    player.position = corrected.position;
                    player.velocity = corrected.velocity;
                    player.isGrounded = corrected.grounded;
                }
                for (const auto& line : g_Client.takeChat()) {
                    if (g_ChatSystem) g_ChatSystem->addMessage(line.text, line.color);
                }
                GameClient::Welcome welcome;
                if (g_Client.takeWelcome(welcome)) {
                    // Remember this server's identity (first visit): from now on the password is
                    // only used to answer this same server at this address (Auth.h)
                    if (!g_Client.serverId().empty()) {
                        std::string& known = g_Settings["server_id." + g_Client.address()];
                        if (known.empty()) { known = g_Client.serverId(); saveSettings(); }
                    }
                    // Joined: the client already reset the world into remote mode
                    player.position = welcome.spawn;
                    g_SpawnPlaced = true;
                    if (g_Hotbar) for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
                    g_ModelInstances.clear();
                    g_ActiveWorldName.clear();
                    g_CurrentGameState = GameState::LOADING;
                    g_PendingWorldOpen = false;
                    g_LoadStartTime = glfwGetTime();
                    g_LoadingProgress = 0.0f;
                    g_Paused = false;
                }
            }
            {
                std::string reason;
                if (g_Client.takeDisconnect(reason)) {
                    g_NetError = reason;
                    if (g_CurrentGameState == GameState::LOADING || g_CurrentGameState == GameState::PLAYING) {
                        // Kicked or lost connection while in the world: back to the server list
                        world.reset();
                        g_ModelInstances.clear();
                        g_ThirdPerson = false;
                        g_Paused = false;
                        g_SettingsInGame = false;
                        g_Inventory.setCreativeOpen(false);
                        g_CurrentGameState = GameState::MULTIPLAYER;
                        callLuaUI(&lua, "on_multiplayer_opened");
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        isCursorDisabled = false;
                    }
                }
            }

            if (g_CurrentGameState == GameState::LOADING) {
                if (g_PendingWorldOpen) {
                    g_PendingWorldOpen = false;
                    if (!openActiveWorld(world, player)) {
                        // Wrong game for this world - back to the world list
                        world.setSaveDirectory("");
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(&lua, "on_world_select_opened");
                        continue;
                    }
                }

                world.setLoadBudgetMs(40.0f);
                worldRenderer.setMeshBudgetMs(30.0f);
                worldRenderer.setUploadBudgetMs(30.0f);
                world.update(player.position, lua);
                if (!world.fatalError().empty()) { leaveBrokenWorld(); continue; }
                worldRenderer.update(world, player.position);

                const int sx = static_cast<int>(std::floor(player.position.x));
                const int sz = static_cast<int>(std::floor(player.position.z));
                const bool timedOut = (glfwGetTime() - g_LoadStartTime) > kLoadTimeoutSec;

                // The chunk the player stands in must be there before they can move: the timeout
                // may skip waiting for meshes, never for the ground (a missing chunk isn't air).
                // Until it loads, the saved position is kept as it is.
                const bool spawnChunkReady = world.isLoaded(glm::ivec3(sx, 0, sz));
                if (!g_SpawnPlaced) {
                    const int ground = world.groundHeightAt(sx, sz);
                    if (ground >= 0) {
                        player.position.y = static_cast<float>(ground);
                        g_SpawnPlaced = true;
                    } else if (timedOut && spawnChunkReady) {   // loaded, with nothing to stand on
                        player.position.y = 100.0f;
                        g_SpawnPlaced = true;
                    }
                }

                g_LoadingProgress = worldRenderer.areaReadyProgress(world, player.position, kSpawnReadyRadius);
                g_SpawnChunkWaiting = timedOut && !spawnChunkReady;   // Esc = back to the menu

                if (g_SpawnPlaced && spawnChunkReady && (g_LoadingProgress >= 1.0f || timedOut)) {
                    g_SpawnChunkWaiting = false;
                    world.setLoadBudgetMs(4.0f);
                    worldRenderer.setMeshBudgetMs(4.0f);
                    worldRenderer.setUploadBudgetMs(3.0f);
                    g_CurrentGameState = GameState::PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }

            if (g_CurrentGameState == GameState::PLAYING) {
                // Autosave
                if (glfwGetTime() - g_LastAutosave >= kAutosaveSeconds) {
                    saveCurrentWorld();
                    g_LastAutosave = glfwGetTime();
                }

                // Chunks keep loading even while paused / in menus (other players may need them)
                world.update(player.position, lua);
                if (!world.fatalError().empty()) { leaveBrokenWorld(); continue; }

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    // Hotbar slot keys (rebindable in Settings > Controls)
                    for (int slot = 0; slot < 9; ++slot) {
                        if (isActionDown(window, "hotbar" + std::to_string(slot + 1)) && slot < g_Hotbar->slotCount) {
                            g_Hotbar->activeSlot = slot;
                        }
                    }
                    const glm::vec3 stepFrom = player.position;
                    const bool wasGrounded = player.isGrounded;
                    const float fallSpeed = -player.velocity.y;
                    // Creative flying: double-tap Jump (only while the game has the keyboard)
                    updateFlight(window, g_Creative && g_FlyAllowed, player.isGrounded);
                    player.update(deltaTime, world, window, lua);
                    // Footsteps: every g_FootstepDistance blocks walked on the ground (so faster
                    // walking gives faster steps), quieter when sneaking;
                    // a landing makes one too
                    {
                        static float stepDistance = 0.0f;
                        const glm::ivec3 under(static_cast<int>(std::floor(player.position.x)),
                                               static_cast<int>(std::floor(player.position.y - 0.2f)),
                                               static_cast<int>(std::floor(player.position.z)));
                        const uint8_t ground = world.getBlockAt(under);
                        if (player.isGrounded && ground != 0) {
                            const glm::vec2 moved(player.position.x - stepFrom.x, player.position.z - stepFrom.z);
                            stepDistance += glm::length(moved);
                            const bool sneaking = (g_LastMoveKeys & MoveInput::Sneak) != 0;
                            if (!wasGrounded && fallSpeed > 4.0f) {
                                playBlockSound(ground, "footstep", player.position, 0.8f);
                                stepDistance = 0.0f;
                            } else if (stepDistance > g_FootstepDistance && glm::length(moved) < 2.0f) {
                                playBlockSound(ground, "footstep", player.position, sneaking ? 0.3f : 0.6f);
                                stepDistance = 0.0f;
                            }
                        }
                    }
                    // Online: tell the server exactly what we just did, so it can do the same
                    // (client-side prediction - see Net.h)
                    if (g_Client.state() == GameClient::State::Joined) {
                        g_Client.recordInput(readMoveInput(window, player.yaw, player.pitch), deltaTime);
                    }

                    {
                        // update_player_animation(dt, is_moving, pitch) - errors are printed, not hidden
                        static std::string lastAnimError;
                        sol::protected_function updateAnimFunc = lua["update_player_animation"];
                        if (updateAnimFunc.valid()) {
                            bool isMoving = isActionDown(window, "forward") || isActionDown(window, "back") ||
                                            isActionDown(window, "left")    || isActionDown(window, "right");
                            sol::protected_function_result r = updateAnimFunc(deltaTime, isMoving, player.pitch);
                            if (!r.valid()) {
                                sol::error err = r;
                                if (lastAnimError != err.what()) {
                                    lastAnimError = err.what();
                                    std::cerr << "[Lua] update_player_animation: " << lastAnimError << std::endl;
                                }
                            }
                        }
                    }
                }

                // Water flows while you play, chat or browse the inventory. Pausing stops it in
                // singleplayer, but not while hosting (the other players are still playing).
                // Each fluid keeps its own step timer (water 0.25 s, lava 1.5 s...), see Fluids.h
                if (!g_Paused || g_Server.running()) {
                    world.updateFluids(deltaTime);
                }
            }

            glm::vec3 rayOrigin = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            glm::vec3 rayDir;
            rayDir.x = cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir.y = sin(glm::radians(player.pitch));
            rayDir.z = sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir = glm::normalize(rayDir);

            RaycastResult rayResult = raycastBlock(rayOrigin, rayDir, 6.0f, world);

            bool leftPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (g_CurrentGameState == GameState::PLAYING && isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                uint8_t activeBlockId = g_Hotbar->getActiveBlockId();
                std::string activeItemName = (activeBlockId != 0) ? BlockRegistry::getInstance().getBlock(activeBlockId).name : "air";

                // Mods get clicks first (e.g. the survival death screen):
                // on_player_click("left"|"right") -> true = used, nothing else happens
                bool clickUsed = false;
                const bool leftClick = leftPress && !leftMousePressedLast;
                const bool rightClick = rightPress && !rightMousePressedLast;
                // Holding right-click keeps placing / using, like Minecraft (every 4 ticks = 0.2 s,
                // engine.set_place_repeat). rightUse = this frame places or uses something.
                static float rightRepeatTimer = 0.0f;
                bool rightRepeat = false;
                if (rightClick) {
                    rightRepeatTimer = g_PlaceRepeatSeconds;
                } else if (rightPress && g_PlaceRepeatSeconds > 0.0f) {
                    rightRepeatTimer -= deltaTime;
                    if (rightRepeatTimer <= 0.0f) {
                        rightRepeat = true;
                        rightRepeatTimer += g_PlaceRepeatSeconds;
                        if (rightRepeatTimer < 0.0f) rightRepeatTimer = g_PlaceRepeatSeconds;
                    }
                }
                const bool rightUse = rightClick || rightRepeat;
                if (leftClick && !clickUsed) g_FirstPerson.swing();   // every left click swings, even at nothing (like Minecraft)
                const bool sneaking = isActionDown(window, "sneak");
                if (leftClick || rightClick) {
                    sol::protected_function onClick = lua["on_player_click"];
                    if (onClick.valid()) {
                        auto r = onClick(leftClick ? "left" : "right");
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_player_click: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) clickUsed = true;
                    }
                }
                if (g_MoveKeyMask == 0) clickUsed = true;   // frozen (e.g. dead): no digging or placing

                // Pick block (middle mouse by default, Settings > Controls): the block you're looking
                // at goes into your hand. on_pick_block(x, y, z, block, meta) -> true = a mod did it
                // (the survival mod takes it from your inventory); otherwise, in creative, the engine
                // selects it in the hotbar or puts it there, like Minecraft.
                {
                    static bool pickWasDown = false;
                    const bool pickDown = isActionDown(window, "pick_block");
                    if (pickDown && !pickWasDown && rayResult.hit && g_MoveKeyMask != 0) {
                        pickBlock(lua, world, rayResult.blockPos);
                    }
                    pickWasDown = pickDown;
                }

                auto& reg = BlockRegistry::getInstance();
                auto spawnBreakParticles = [&](const glm::ivec3& pos, uint8_t id) {
                    const auto& prop = reg.getBlock(id);
                    int particleLayer = -1;   // the block's side texture (or any face it has)
                    for (int f : { 4, 0, 2, 1, 5, 3 }) {
                        if (prop.faces[f].baseLayer >= 0) { particleLayer = prop.faces[f].baseLayer; break; }
                    }
                    particles.spawnBreakParticles(glm::vec3(pos), id, static_cast<float>(particleLayer));
                };
                const uint8_t heldId = g_Hotbar->getActiveBlockId();
                const BlockProperty* held = heldId != 0 ? &reg.getBlock(heldId) : nullptr;
                auto rightTool = [&](const BlockProperty& block) {
                    const std::string want = blockTool(block);
                    return held && !want.empty() && held->toolType == want;
                };
                // Seconds to dig `block` with what's in hand (-1 = can't). get_dig_time(block, item)
                // in Lua may decide instead.
                auto digSeconds = [&](const BlockProperty& block) -> float {
                    sol::protected_function hook = lua["get_dig_time"];
                    if (hook.valid()) {
                        auto r = hook(block.name, activeItemName);
                        if (r.valid() && r.get_type() == sol::type::number) return r.get<float>();
                    }
                    float t = blockHardness(block);
                    if (t <= 0.0f) return t;
                    if (rightTool(block)) t /= std::max(held->toolSpeed, 0.01f);
                    else if (blockNeedsTool(block)) t *= 3.33f;   // the wrong tool: much slower
                    return t;
                };
                // A block dug in survival: on_dig_block(x, y, z, block, item) may handle the drops
                // (return true); otherwise it drops itself (or `drops`) into the hotbar.
                // on_block_broken(x, y, z, block, meta): a player broke a block (creative or
                    // survival), after it's gone - e.g. the doors mod removes the other half
                auto blockBroken = [&](const glm::ivec3& pos, const std::string& name, uint8_t meta) {
                    if (g_ClientMods) g_ClientMods->blockDug(pos.x, pos.y, pos.z, name);
                    sol::protected_function hook = lua["on_block_broken"];
                    if (!hook.valid()) return;
                    auto r = hook(pos.x, pos.y, pos.z, name, static_cast<int>(meta));
                    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_block_broken: " << err.what() << std::endl; }
                };
                auto survivalDig = [&](const glm::ivec3& pos, uint8_t id) {
                    const BlockProperty block = reg.getBlock(id);
                    const uint8_t brokenMeta = world.getMetadataAt(pos);
                    bool handled = false;
                    sol::protected_function hook = lua["on_dig_block"];
                    if (hook.valid()) {
                        auto r = hook(pos.x, pos.y, pos.z, block.name, activeItemName);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_dig_block: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) handled = true;
                    }
                    spawnBreakParticles(pos, id);
                    playBlockSound(id, "dug", glm::vec3(pos) + glm::vec3(0.5f));
                    world.setBlockAt(pos, 0);
                    blockBroken(pos, block.name, brokenMeta);
                    if (handled) return;
                    if (blockNeedsTool(block) && !rightTool(block)) return;   // stone by hand: nothing
                    const std::string drop = block.dropsSet ? block.drops : block.name;
                    if (!drop.empty()) giveHotbarItem(drop, 1);
                };

                g_Dig.cooldown = std::max(0.0f, g_Dig.cooldown - deltaTime);
                bool digging = false;
                if (leftPress && !clickUsed && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    const uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = reg.getBlock(targetBlockId);
                        if (targetProp.isSelectable) {
                            if (activeItemName.find("scythe") != std::string::npos) {
                                if (leftClick) {
                                    sol::protected_function scytheFunc = lua["on_use_scythe"];
                                    if (scytheFunc.valid()) scytheFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                                }
                            } else if (g_Creative) {
                                if (leftClick) {   // creative: one click, gone
                                    const uint8_t brokenMeta = world.getMetadataAt(hitPos);
                                    spawnBreakParticles(hitPos, targetBlockId);
                                    playBlockSound(targetBlockId, "dug", glm::vec3(hitPos) + glm::vec3(0.5f));
                                    world.setBlockAt(hitPos, 0);
                                    blockBroken(hitPos, targetProp.name, brokenMeta);
                                }
                            } else {
                                // survival: hold until the block breaks
                                digging = true;
                                if (!g_Dig.active || g_Dig.pos != hitPos) {
                                    g_Dig = DigState{true, hitPos, 0.0f, digSeconds(targetProp), g_Dig.cooldown, 0.0f};
                                }
                                if (!g_FirstPerson.swinging()) g_FirstPerson.swing();   // keeps swinging while digging
                                if (g_Dig.seconds >= 0.0f && g_Dig.cooldown <= 0.0f) {
                                    g_Dig.progress = g_Dig.seconds <= 0.0f ? 1.0f
                                        : g_Dig.progress + deltaTime / g_Dig.seconds;
                                    g_Dig.particleTimer -= deltaTime;
                                    if (g_Dig.particleTimer <= 0.0f && g_Dig.progress < 1.0f) {
                                        g_Dig.particleTimer = 0.25f;
                                        spawnBreakParticles(hitPos, targetBlockId);
                                        playBlockSound(targetBlockId, "dig", glm::vec3(hitPos) + glm::vec3(0.5f), 0.5f);
                                    }
                                    if (g_Dig.progress >= 1.0f) {
                                        survivalDig(hitPos, targetBlockId);
                                        g_Dig.active = false;
                                        g_Dig.progress = 0.0f;
                                        g_Dig.cooldown = 0.25f;   // Minecraft's short pause between blocks
                                    }
                                }
                            }
                        }
                    }
                }
                if (!digging) { g_Dig.active = false; g_Dig.progress = 0.0f; }

                // Items with a use (engine.register_item_use) get the right-click first, even when
                // pointing at water or nothing at all
                bool itemUsed = false;
                if (rightUse && !clickUsed) {
                    const auto handler = g_ItemUseHandlers.find(activeItemName);
                    if (handler != g_ItemUseHandlers.end() && handler->second.valid()) {
                        sol::table pointed = lua.create_table();
                        const HotbarSlot held = g_Hotbar->getSlot(g_Hotbar->activeSlot);
                        pointed["item"] = activeItemName;
                        pointed["slot"] = g_Hotbar->activeSlot + 1;
                        pointed["count"] = held.count;
                        pointed["sneaking"] = sneaking;
                        pointed["repeat"] = rightRepeat;   // from holding the button, not a new click
                        pointed["repeat_click"] = rightRepeat;   // the same, usable as pointed.repeat_click ("repeat" is a Lua keyword)
                        pointed["player"] = lua.create_table_with("x", player.position.x, "y", player.position.y, "z", player.position.z);
                        if (rayResult.hit) {
                            const glm::ivec3 u(rayResult.blockPos);
                            const glm::ivec3 a = u + glm::ivec3(glm::round(glm::vec3(rayResult.faceNormal)));
                            const uint8_t uid = world.getBlockAt(u);
                            pointed["under"] = lua.create_table_with("x", u.x, "y", u.y, "z", u.z,
                                "name", uid ? BlockRegistry::getInstance().getBlock(uid).name : std::string("air"));
                            pointed["above"] = lua.create_table_with("x", a.x, "y", a.y, "z", a.z);
                        }
                        const FluidRayHit fluid = raycastFluid(rayOrigin, rayDir, 6.0f, world);
                        const bool fluidFirst = fluid.hit && (!rayResult.hit ||
                            glm::distance(glm::vec3(fluid.pos) + 0.5f, rayOrigin) <
                            glm::distance(glm::vec3(rayResult.blockPos) + 0.5f, rayOrigin));
                        if (fluidFirst) {
                            const uint8_t fid = world.getBlockAt(fluid.pos);
                            const FluidCell fc = FluidRegistry::get().cell(fid, world.getMetadataAt(fluid.pos));
                            if (fc) {
                                pointed["fluid"] = lua.create_table_with("x", fluid.pos.x, "y", fluid.pos.y, "z", fluid.pos.z,
                                    "name", fc.fluid->name,                                   // "water", "lava"...
                                    "block", BlockRegistry::getInstance().getBlock(fid).name,
                                    "source", fc.source, "falling", fc.falling, "level", fc.level);
                            }
                        }
                        auto res = handler->second(pointed);
                        if (!res.valid()) {
                            sol::error err = res;
                            std::cerr << "[Mod] Item use for '" << activeItemName << "' failed: " << err.what() << std::endl;
                            itemUsed = true;
                        } else {
                            itemUsed = res.get_type() == sol::type::boolean && res.get<bool>();
                        }
                        if (itemUsed) g_FirstPerson.swing();
                    }
                }

                if (rightUse && !clickUsed && rayResult.hit && !itemUsed) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);
                        if (activeItemName.find("hoe") != std::string::npos) {
                            sol::protected_function hoeFunc = lua["on_use_hoe"];
                            if (hoeFunc.valid()) {
                                hoeFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                            }
                        } else {
                            if (g_ClientMods && !rightRepeat) g_ClientMods->rightClickBlock(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                            sol::protected_function clickFunc = lua["on_right_click_block"];
                            bool handled = false;
                            if (clickFunc.valid()) {
                                // (x, y, z, block, held_item, sneaking, repeat): sneaking = place against it
                                // instead of using it; repeat = from holding the button
                                sol::protected_function_result res = clickFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name,
                                                                               activeItemName, sneaking, rightRepeat);
                                if (res.valid()) handled = res.get<bool>();
                            }
                            if (handled) g_FirstPerson.swing();   // opened a door, a workbench...

                            if (!handled) {
                                uint8_t selectedBlock = g_Hotbar->getActiveBlockId();
                                // Items (anything with an inventory picture: sticks, tools, buckets)
                                // are never put down as blocks
                                if (selectedBlock != 0 && !BlockRegistry::getInstance().getBlock(selectedBlock).inventoryImage.empty()) {
                                    selectedBlock = 0;
                                }
                                if (selectedBlock != 0) {
                                    bool replaceTarget = false;
                                    if (targetProp.name == "short_grass" || targetProp.name == "flower_red" || targetProp.pattern == "plantlike" || targetProp.name.find("farmland") != std::string::npos) {
                                        replaceTarget = true;
                                    }
                                    // Work in whole-block (integer) coordinates for both cases so the
                                    // ternary isn't mixing glm::ivec3 and glm::vec3.
                                    const glm::ivec3 hitCell(rayResult.blockPos);
                                    const int farmlandLift = (targetProp.name.find("farmland") != std::string::npos) ? 1 : 0;
                                    const glm::ivec3 cell = replaceTarget
                                        ? hitCell + glm::ivec3(0, farmlandLift, 0)
                                        : hitCell + glm::ivec3(glm::round(glm::vec3(rayResult.faceNormal)));

                                    // Don't place a solid block where the player is standing (or jumping).
                                    // Player box: 0.6 wide, 1.8 tall, feet at player.position.
                                    constexpr float kHalfWidth = 0.3f;
                                    constexpr float kHeight    = 1.8f;
                                    constexpr float kEps       = 0.001f;   // touching is fine, overlapping isn't
                                    const glm::vec3 pMin(player.position.x - kHalfWidth, player.position.y, player.position.z - kHalfWidth);
                                    const glm::vec3 pMax(player.position.x + kHalfWidth, player.position.y + kHeight, player.position.z + kHalfWidth);
                                    const bool overlapsPlayer =
                                        pMin.x < cell.x + 1 - kEps && pMax.x > cell.x + kEps &&
                                        pMin.y < cell.y + 1 - kEps && pMax.y > cell.y + kEps &&
                                        pMin.z < cell.z + 1 - kEps && pMax.z > cell.z + kEps;
                                    const bool solid = BlockRegistry::getInstance().getBlock(selectedBlock).isWalkable;
                                    // Only into a free space: air, a fluid or a plant (never over a
                                    // door you reached past, or any other block)
                                    bool cellFree = replaceTarget;
                                    if (!cellFree) {
                                        const uint8_t existing = world.getBlockAt(cell);
                                        const auto& ep = BlockRegistry::getInstance().getBlock(existing);
                                        cellFree = existing == 0 || FluidRegistry::get().forBlock(existing) != nullptr
                                                || ep.pattern == "plantlike";
                                    }

                                    if (cellFree && !(solid && overlapsPlayer)) {
                                        world.setBlockAt(cell, selectedBlock);
                                        g_FirstPerson.swing();
                                        // on_block_placed(x, y, z, block): a player put a block down
                                        {
                                            sol::protected_function placedHook = lua["on_block_placed"];
                                            if (placedHook.valid()) {
                                                auto r = placedHook(cell.x, cell.y, cell.z,
                                                                    BlockRegistry::getInstance().getBlock(selectedBlock).name);
                                                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_block_placed: " << err.what() << std::endl; }
                                            }
                                        }
                                        playBlockSound(selectedBlock, "place", glm::vec3(cell) + glm::vec3(0.5f));
                                        // Survival: placing uses one from the stack
                                        if (!g_Creative) {
                                            const int slot = g_Hotbar->activeSlot;
                                            const HotbarSlot held = g_Hotbar->getSlot(slot);
                                            const int left = held.count - 1;
                                            g_Hotbar->setSlot(slot, left > 0 ? held.blockId : 0, left > 0 ? left : 0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            leftMousePressedLast = leftPress;
            rightMousePressedLast = rightPress;

            float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            glm::mat4 proj = glm::perspective(glm::radians(g_FieldOfView), aspectRatio, 0.1f, 500.0f);
            glm::mat4 view = player.getViewMatrix();

            // Third-person camera (F5): behind the player, pulled in so it never goes through blocks
            if (g_ThirdPerson && g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 eye = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
                const glm::vec3 front = glm::normalize(glm::vec3(
                    cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch)),
                    sin(glm::radians(player.pitch)),
                    sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch))));

                float dist = 4.0f;
                for (float d = 0.25f; d <= 4.0f; d += 0.25f) {
                    const glm::vec3 p = eye - front * d;
                    const uint8_t id = world.getBlockAt(glm::ivec3(static_cast<int>(std::floor(p.x)),
                                                                   static_cast<int>(std::floor(p.y)),
                                                                   static_cast<int>(std::floor(p.z))));
                    if (id != 0 && BlockRegistry::getInstance().getBlock(id).isWalkable) {
                        dist = std::max(0.3f, d - 0.3f);
                        break;
                    }
                }
                view = glm::lookAt(eye - front * dist, eye, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            glm::vec3 sunDir = g_LightingSystem->getSunDirection();
            int moonPhase = g_LightingSystem->getMoonPhase();

            const glm::vec3 eyePos = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            const float waterDepth = (g_CurrentGameState == GameState::PLAYING)
                ? underwaterDepth(world, eyePos)
                : -1.0f;
            const bool submerged = waterDepth >= 0.0f;

            {
                const float target   = submerged ? 1.0f : 0.0f;
                const float seconds = submerged ? g_Underwater.fadeInSeconds : g_Underwater.fadeOutSeconds;
                const float step    = deltaTime / glm::max(seconds, 0.01f);
                if (g_UnderwaterBlend < target) g_UnderwaterBlend = glm::min(g_UnderwaterBlend + step, target);
                else                            g_UnderwaterBlend = glm::max(g_UnderwaterBlend - step, target);
            }
            const float blend      = g_UnderwaterBlend;
            const bool  skyHidden = blend > 0.5f;

            const WaterColors water = (blend > 0.0f)
                ? queryWaterColors(lua, eyePos)
                : (g_Underwater.colorsFromLua ? WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor } : waterBlockColors());
            const float depthT = submerged
                ? glm::clamp(waterDepth / g_Underwater.fullDepth, 0.0f, 1.0f)
                : 0.0f;
            const glm::vec3 underwaterFog = glm::mix(water.shallow, water.deep, depthT);
            const float underwaterVis = glm::mix(g_Underwater.shallowVisibility, g_Underwater.deepVisibility, depthT);

            // A gentle cool tint (white mixed with a little fog colour): colours stay readable
            const glm::vec3 underwaterTint = glm::mix(
                glm::vec3(0.85f, 0.95f, 1.0f),
                glm::normalize(underwaterFog),
                0.25f);
            const float tintAmount = blend * g_Underwater.tintStrength;

            const glm::vec3 skyClear(0.5f, 0.8f, 0.95f);
            const glm::vec3 clearCol = glm::mix(skyClear, underwaterFog, blend);
            glClearColor(clearCol.r, clearCol.g, clearCol.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 aboveFog   = fogColorForSun(g_Fog, sunDir.y);
                const float loadedEdge     = g_WorldInstance->renderDistanceBlocks();
                const float aboveEnd       = glm::min(effectiveFogEnd(g_Fog, g_LightingSystem->weatherRainIntensity), loadedEdge - 8.0f);
                const float aboveStart     = effectiveFogStart(g_Fog, aboveEnd);

                const glm::vec3 fogCol     = glm::mix(aboveFog, underwaterFog, blend);
                const float     fogStart   = glm::mix(aboveStart, underwaterVis * g_Underwater.fogStartFraction, blend);
                const float     fogEnd     = glm::mix(aboveEnd, underwaterVis, blend);
                const float     fogDensity = g_Fog.density;
                const bool fogExp = g_Fog.exponential && blend <= 0.0f;
                const bool fogOn  = g_Fog.enabled || blend > 0.0f;

                if (!skyHidden) {
                    g_Skybox->render(view, proj, sunDir.y);

                    glDepthMask(GL_FALSE);
                    glUseProgram(sunProgram);

                    glm::vec3 sunWorldPos = player.position + (sunDir * 80.0f);
                    glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunWorldPos);
                    sunModel[0][0] = view[0][0]; sunModel[0][1] = view[1][0]; sunModel[0][2] = view[2][0];
                    sunModel[1][0] = view[0][1]; sunModel[1][1] = view[1][1]; sunModel[1][2] = view[2][1];
                    sunModel[2][0] = view[0][2]; sunModel[2][1] = view[1][2]; sunModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(sunModel));

                    if (g_LightingSystem->sunTextureID != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, g_LightingSystem->sunTextureID);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }

                    glBindVertexArray(sunVAO);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glm::vec3 moonDir = -sunDir;
                    glm::vec3 moonWorldPos = player.position + (moonDir * 80.0f);
                    glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonWorldPos);
                    moonModel[0][0] = view[0][0]; moonModel[0][1] = view[1][0]; moonModel[0][2] = view[2][0];
                    moonModel[1][0] = view[0][1]; moonModel[1][1] = view[1][1]; moonModel[1][2] = view[2][1];
                    moonModel[2][0] = view[0][2]; moonModel[2][1] = view[1][2]; moonModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(moonModel));

                    GLuint activeMoonTex = g_LightingSystem->moonTextureIDs[moonPhase] != 0
                        ? g_LightingSystem->moonTextureIDs[moonPhase]
                        : g_LightingSystem->moonTextureIDs[0];
                    if (activeMoonTex != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, activeMoonTex);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glBindVertexArray(0);
                    glDepthMask(GL_TRUE);

                    // Weather: greys/darkens the sky, sun and moon (sky.set_sky_tint)
                    g_Weather.drawSkyTint();
                }

                glUseProgram(shaderProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), static_cast<float>(glfwGetTime()));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogEnabled"), fogOn ? 1 : 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogExp"), fogExp ? 1 : 0);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogStart"), fogStart);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogEnd"), fogEnd);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogDensity"), fogDensity);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_FogColor"), fogCol.r, fogCol.g, fogCol.b);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_UnderwaterAmount"), tintAmount);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_WeatherLight"), g_Weather.worldLight());

                // Moving lights: the item in the player's hand, then lights added by mods (sky.add_light)
                g_HeldLight.update(world);
                std::vector<PointLight> pointLights;
                for (const PointLight& pl : g_LightingSystem->getPointLights()) {
                    if (pl.active && pointLights.size() < 16) pointLights.push_back(pl);
                }
                glUniform1i(glGetUniformLocation(shaderProgram, "u_NumPointLights"), static_cast<int>(pointLights.size()));
                for (size_t i = 0; i < pointLights.size() && i < 16; ++i) {
                    std::string base = "u_PointLights[" + std::to_string(i) + "].";
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "position").c_str()), pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "color").c_str()), pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "radius").c_str()), pointLights[i].radius);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "intensity").c_str()), pointLights[i].intensity);
                }

                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

                worldRenderer.update(world, player.position);   // pick up changed chunks, mesh, upload
                // Block light: chunks use the light in their vertices; everything else drawn with
                // this shader afterwards (players, particles) uses the light where the camera is
                {
                    const uint16_t here = world.getLightAt(glm::ivec3(glm::floor(eyePos)));
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_FallbackLight"), light::sky(here) / 15.0f,
                                light::red(here) / 15.0f, light::green(here) / 15.0f, light::blue(here) / 15.0f);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_MinLight"), g_LightSettings.minLight);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_Brightness"), g_LightSettings.brightness);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_BlockLightScale"), g_LightSettings.blockLightScale);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 1);
                }
                worldRenderer.render(shaderProgram, proj * view, eyePos);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 0);

                // Other players (multiplayer)
                const net::PlayerMap& otherPlayers = g_Server.running() ? g_Server.players() : g_Client.players();
                if (!otherPlayers.empty()) playerAvatars.render(otherPlayers, shaderProgram, view, proj);

                // Block-break particles (same shader as the world, so they get fog and lighting)
                if (!g_Paused) particles.update(deltaTime);
                particles.render(view, proj, shaderProgram, g_TextureArrayID);

                g_Clouds->update(deltaTime);
                if (!skyHidden) {
                    g_Clouds->render(view, proj, player.position,
                                       static_cast<float>(glfwGetTime()),
                                       sunDir.y,
                                       fogCol);
                }

                glUseProgram(modelShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LightDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterAmount"), tintAmount);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_WeatherLight"), g_Weather.worldLight());

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), player.position);
                modelMatrix = glm::rotate(modelMatrix, glm::radians(-player.yaw + 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

                // Player model: only in third person (in first person the camera is inside its head)
                if (g_ThirdPerson) {
                    try {
                        sol::object modelObj = lua["g_PlayerModel"];
                        if (modelObj.is<std::shared_ptr<AnimatedModel>>()) {
                            auto model = modelObj.as<std::shared_ptr<AnimatedModel>>();
                            if (model) {
                                model->draw(modelShaderProgram, modelMatrix);
                            }
                        }
                    } catch (...) {}
                }

                // Models placed from Lua with engine.add_model (mobs etc.)
                for (const auto& [id, inst] : g_ModelInstances) {
                    if (!inst.visible || !inst.model) continue;
                    glm::mat4 m = glm::translate(glm::mat4(1.0f), inst.position);
                    m = glm::rotate(m, glm::radians(inst.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
                    m = glm::scale(m, glm::vec3(inst.scale));
                    inst.model->draw(modelShaderProgram, m);
                }

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen) && rayResult.hit) {
                    glUseProgram(wireframeProgram);
                    // The outline fits the block's shape: a full cube, or a door's panel (DoorShape.h)
                    glm::vec3 selMin(0.0f), selMax(1.0f);
                    {
                        const uint8_t selId = world.getBlockAt(rayResult.blockPos);
                        if (selId != 0 && BlockRegistry::getInstance().getBlock(selId).pattern == "door") {
                            door::box(world.getMetadataAt(rayResult.blockPos), selMin, selMax);
                        }
                    }
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(rayResult.blockPos) + (selMin + selMax) * 0.5f);
                    model = glm::scale(model, (selMax - selMin) + glm::vec3(0.002f));   // a hair bigger: no z-fighting
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    glDisable(GL_CULL_FACE);
                    glLineWidth(3.0f);

                    static GLuint boxVAO = 0, boxVBO = 0;
                    if (boxVAO == 0) {
                        float vertices[] = {
                            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
                             0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,

                            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
                             0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,

                            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f
                        };
                        glGenVertexArrays(1, &boxVAO);
                        glGenBuffers(1, &boxVBO);
                        glBindVertexArray(boxVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                        glEnableVertexAttribArray(0);
                        glBindVertexArray(0);
                    }

                    glUniform4f(glGetUniformLocation(wireframeProgram, "u_Color"), 0.0f, 0.0f, 0.0f, 0.9f);
                    glBindVertexArray(boxVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);

                    // Survival digging: the crack picture (engine.set_dig_crack_texture) grows stage by
                    // stage over the block; without one, the block darkens in steps
                    if (g_Dig.active && g_Dig.progress > 0.0f && g_Dig.pos == glm::ivec3(rayResult.blockPos)) {
                        const float p = glm::clamp(g_Dig.progress, 0.0f, 0.999f);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glDepthMask(GL_FALSE);
                        if (g_CrackTexture != 0) {
                            static GLuint crackProg = 0, texVAO = 0, texVBO = 0;
                            if (crackProg == 0) {
                                const char* vs = "#version 330 core\n"
                                    "layout (location = 0) in vec3 aPos; layout (location = 1) in vec2 aUV;\n"
                                    "uniform mat4 u_MVP; uniform float u_Stage; uniform float u_Stages; out vec2 vUV;\n"
                                    "void main() { vUV = vec2(aUV.x, (u_Stage + aUV.y) / u_Stages); gl_Position = u_MVP * vec4(aPos, 1.0); }\n";
                                const char* fs = "#version 330 core\n"
                                    "in vec2 vUV; out vec4 FragColor; uniform sampler2D u_Tex;\n"
                                    "void main() { vec4 c = texture(u_Tex, vUV); if (c.a < 0.02) discard; FragColor = c; }\n";
                                crackProg = glCreateProgram();
                                glAttachShader(crackProg, compileShader(GL_VERTEX_SHADER, vs));
                                glAttachShader(crackProg, compileShader(GL_FRAGMENT_SHADER, fs));
                                glLinkProgram(crackProg);
                                // 6 faces x 2 triangles, each face with its own 0..1 picture
                                const float h = 0.503f;
                                const float faces[6][4][3] = {
                                    {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
                                    {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}},
                                    {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}} };
                                const float uv[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };
                                const int order[6] = { 0, 1, 2, 0, 2, 3 };
                                float v[6 * 6 * 5];
                                int n = 0;
                                for (int f = 0; f < 6; ++f) for (int i : order) {
                                    v[n++] = faces[f][i][0]; v[n++] = faces[f][i][1]; v[n++] = faces[f][i][2];
                                    v[n++] = uv[i][0]; v[n++] = uv[i][1];
                                }
                                glGenVertexArrays(1, &texVAO);
                                glGenBuffers(1, &texVBO);
                                glBindVertexArray(texVAO);
                                glBindBuffer(GL_ARRAY_BUFFER, texVBO);
                                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
                                glEnableVertexAttribArray(0);
                                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
                                glEnableVertexAttribArray(1);
                            }
                            const glm::mat4 mvp = proj * view * model;
                            glUseProgram(crackProg);
                            glUniformMatrix4fv(glGetUniformLocation(crackProg, "u_MVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                            glUniform1f(glGetUniformLocation(crackProg, "u_Stage"), std::floor(p * g_CrackStages));
                            glUniform1f(glGetUniformLocation(crackProg, "u_Stages"), static_cast<float>(g_CrackStages));
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, g_CrackTexture);
                            glUniform1i(glGetUniformLocation(crackProg, "u_Tex"), 0);
                            glBindVertexArray(texVAO);
                            glDrawArrays(GL_TRIANGLES, 0, 36);
                            glBindVertexArray(0);
                        } else {
                            static GLuint crackVAO = 0, crackVBO = 0;
                            if (crackVAO == 0) {
                                // 36 vertices: the 6 faces of a cube, a hair bigger than the block
                                const float h = 0.502f;
                                const float c[8][3] = { {-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h} };
                                const int f[36] = { 0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,  3,2,6, 3,6,7,  0,3,7, 0,7,4,  1,5,6, 1,6,2 };
                                float v[108];
                                for (int i = 0; i < 36; ++i) for (int k = 0; k < 3; ++k) v[i * 3 + k] = c[f[i]][k];
                                glGenVertexArrays(1, &crackVAO);
                                glGenBuffers(1, &crackVBO);
                                glBindVertexArray(crackVAO);
                                glBindBuffer(GL_ARRAY_BUFFER, crackVBO);
                                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                                glEnableVertexAttribArray(0);
                            }
                            const float stage = std::floor(p * 10.0f) / 10.0f;   // like Minecraft's 10 stages
                            glUniform4f(glGetUniformLocation(wireframeProgram, "u_Color"), 0.05f, 0.04f, 0.03f, 0.08f + stage * 0.55f);
                            glBindVertexArray(crackVAO);
                            glDrawArrays(GL_TRIANGLES, 0, 36);
                            glBindVertexArray(0);
                        }
                        glDepthMask(GL_TRUE);
                    }

                    // Restore the state the rest of the frame expects (culling was off before)
                    glDisable(GL_CULL_FACE);
                    glEnable(GL_BLEND);
                }

                // Weather: rain/snow around the camera (not underwater), then lightning flashes
                g_Weather.update(g_Paused ? 0.0f : deltaTime, eyePos, world);
                if (blend < 0.5f) {
                    g_Weather.drawPrecipitation(view, proj, eyePos, glm::clamp(sunDir.y * 2.0f + 0.5f, 0.0f, 1.0f));
                }
                g_Weather.drawFlash();

                // ---------------- First-person hand (FirstPerson.h) ----------------
                {
                    // Walking speed for the bob; "on the ground" by vertical speed, as the grounded
                    // flag flickers frame to frame while standing. Updated in third person too, so
                    // the swing keeps time for the player model (engine.get_swing).
                    const float walkSpeed = g_Paused ? 0.0f : glm::length(glm::vec2(player.velocity.x, player.velocity.z));
                    // (flying counts as off the ground: no bob, like Minecraft - flying level would
                    // otherwise look like walking at flying speed)
                    const bool onGround = !g_Flying && std::fabs(player.velocity.y) < 0.6f;
                    g_FirstPerson.update(deltaTime, g_Hotbar ? g_Hotbar->getActiveBlockId() : 0,
                                         g_Hotbar ? g_Hotbar->activeSlot : 0, walkSpeed, onGround,
                                         player.yaw, player.pitch);
                    std::shared_ptr<AnimatedModel> fpModel;
                    sol::object modelObj = lua["g_PlayerModel"];
                    if (modelObj.is<std::shared_ptr<AnimatedModel>>()) fpModel = modelObj.as<std::shared_ptr<AnimatedModel>>();
                    const uint16_t here = world.getLightAt(glm::ivec3(glm::floor(eyePos)));
                    const glm::vec4 handLight(light::sky(here) / 15.0f, light::red(here) / 15.0f,
                                              light::green(here) / 15.0f, light::blue(here) / 15.0f);
                    if (!g_ThirdPerson) {
                        g_FirstPerson.render(shaderProgram, modelShaderProgram, g_TextureArrayID, width, height,
                                             handLight, fpModel, settingInt("main_hand", 0) == 1);
                    }
                }

                // ---------------- HUD ----------------
                glDisable(GL_DEPTH_TEST);
                glUseProgram(uiProgram);
                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    glBindVertexArray(crosshairVAO);
                    glLineWidth(2.0f);
                    glDrawArrays(GL_LINES, 0, 4);
                    glBindVertexArray(0);
                }

                // 1. Hotbar (background + 3D icons) - drawn BEFORE the inventory so the
                //    creative inventory always sits on top of it.
                g_Hotbar->updateBuffers(width, height);
                g_Hotbar->renderSlots(uiProgram, texturedUIProgram);

                glEnable(GL_DEPTH_TEST);
                glUseProgram(shaderProgram);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

                g_Hotbar->render3DIcons(shaderProgram, g_TextureArrayID);

                // Stack counts (more than 1) at the bottom right of each slot, white with a shadow,
                // like Minecraft
                if (g_FontRenderer) {
                    const Dimensions2D screen{width, height};
                    const Dimensions2D base = g_FontRenderer->baseSize();
                    const float rawCap = std::max(g_FontRenderer->capHeight(1.0f, base), 0.001f);
                    glDisable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glUseProgram(fontProgram);
                    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
                        const HotbarSlot slot = g_Hotbar->getSlot(i);
                        if (slot.blockId == 0 || slot.count <= 1) continue;
                        float sx, sy, size;
                        hotbarSlotRect(*g_Hotbar, i, static_cast<float>(width), static_cast<float>(height), sx, sy, size);
                        const std::string text = std::to_string(slot.count);
                        const float capPx = std::max(7.0f, size * 0.3f);
                        const float f = capPx / rawCap;
                        const float scX = f * base.width / std::max(1, width), scY = f * base.height / std::max(1, height);
                        const float tw = g_FontRenderer->textWidth(text, scX, screen);
                        const float tx = std::round(sx + size - tw - size * 0.04f);
                        const float ty = std::round(sy + size - size * 0.04f);           // baseline
                        const float sh = std::max(1.0f, std::round(capPx / 7.0f));      // shadow offset
                        g_FontRenderer->renderText(fontProgram, text, tx + sh, ty + sh, scX, scY, glm::vec4(0.15f, 0.15f, 0.15f, 1.0f), screen);
                        g_FontRenderer->renderText(fontProgram, text, tx, ty, scX, scY, glm::vec4(1.0f), screen);
                    }
                }

                glDisable(GL_DEPTH_TEST);

                // HUD drawn by mods (hearts, hunger, air... see mods/survival): render_hud(W, H)
                {
                    sol::object hud = lua["render_hud"];
                    if (hud.valid() && hud.get_type() == sol::type::function) {
                        g_Inventory.renderLuaScreen(window, lua, "render_hud", static_cast<float>(width),
                                                    static_cast<float>(height), uiProgram, texturedUIProgram);
                        glDisable(GL_DEPTH_TEST);
                    }
                }

                // The player's client mods' HUDs (minimap...), drawn on top of the game's
                if (g_ClientMods && g_ClientMods->active()) {
                    g_Inventory.renderLuaScreen(window, lua, "__client_hud", static_cast<float>(width),
                                                static_cast<float>(height), uiProgram, texturedUIProgram);
                    glDisable(GL_DEPTH_TEST);
                }

                // 2. Creative inventory (drawn by inventory.lua)
                g_Inventory.renderCreativeInventory(window, lua, static_cast<float>(width), static_cast<float>(height), uiProgram, texturedUIProgram);
                glDisable(GL_DEPTH_TEST);

                // 3. A mod's inventory screen (render_inventory), with ui.* drawing available
                {
                    sol::object inv = lua["render_inventory"];
                    if (inv.valid() && inv.get_type() == sol::type::function) {
                        g_Inventory.renderLuaScreen(window, lua, "render_inventory", static_cast<float>(width),
                                                    static_cast<float>(height), uiProgram, texturedUIProgram);
                        glDisable(GL_DEPTH_TEST);
                    }
                }

                // 4. Pause menu (pause_menu.lua), or Settings if opened from it (settings_menu.lua)
                if (g_Paused) {
                    g_Inventory.renderLuaScreen(window, lua, g_SettingsInGame ? "render_settings" : "render_pause_menu",
                                                static_cast<float>(width), static_cast<float>(height),
                                                uiProgram, texturedUIProgram);
                    glDisable(GL_DEPTH_TEST);
                }

                // Name tags above other players (multiplayer)
                {
                    const net::PlayerMap& others = g_Server.running() ? g_Server.players() : g_Client.players();
                    if (!others.empty() && g_FontRenderer) {
                        resetUIRenderState();
                        glUseProgram(fontProgram);
                        const Dimensions2D screen{width, height};
                        const glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
                        for (const auto& tag : PlayerAvatars::nameTags(others, view, proj, camPos, width, height)) {
                            const float scale = 0.5f;
                            const float x = tag.x - tag.text.size() * 13.0f * scale * 0.5f;
                            g_FontRenderer->renderText(fontProgram, tag.text, x + 2.0f, tag.y + 2.0f, scale, scale,
                                                       glm::vec4(0.0f, 0.0f, 0.0f, 0.8f), screen);
                            g_FontRenderer->renderText(fontProgram, tag.text, x, tag.y, scale, scale,
                                                       glm::vec4(1.0f), screen);
                        }
                    }
                }

                // Player list while the Player List key (Tab) is held (drawn by player_list.lua)
                if (!g_Paused && (!g_ChatSystem || !g_ChatSystem->isOpen) && !g_Inventory.isCreativeOpen() &&
                    isActionDown(window, "player_list")) {
                    resetUIRenderState();
                    g_Inventory.renderLuaScreen(window, lua, "render_player_list",
                                                static_cast<float>(width), static_cast<float>(height),
                                                uiProgram, texturedUIProgram);
                }

                // F3 info: stays on screen even when this window isn't focused, so you can see
                // whether a background window is being slowed down
                if (g_ShowDebugInfo && g_FontRenderer) {
                    resetUIRenderState();
                    glUseProgram(fontProgram);
                    const Dimensions2D screen{width, height};
                    std::vector<std::string> lines;
                    lines.push_back("FPS: " + std::to_string(static_cast<int>(g_Fps)) +
                                    (glfwGetWindowAttrib(window, GLFW_FOCUSED) ? "" : "  (window not focused)"));
                    lines.push_back("GPU: " + g_GpuName);
                    if (g_Server.running()) lines.push_back("Hosting");
                    for (const auto& l : g_Server.running() ? g_Server.statsLines() : g_Client.statsLines()) {
                        if (g_Server.running() || g_Client.active()) lines.push_back(l);
                    }
                    char pos[96];
                    std::snprintf(pos, sizeof(pos), "XYZ: %.1f %.1f %.1f", player.position.x,
                                  player.position.y, player.position.z);
                    lines.push_back(pos);

                    // What the crosshair is on, straight from the world's block data
                    if (rayResult.hit) {
                        const glm::ivec3 b(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                        const uint8_t id = world.getBlockAt(b);
                        const uint8_t meta = world.getMetadataAt(b);
                        const BlockProperty& prop = BlockRegistry::getInstance().getBlock(id);
                        char buf[200];

                        lines.push_back("");
                        std::snprintf(buf, sizeof(buf), "Looking at: %s (id %d)", prop.name.c_str(), static_cast<int>(id));
                        lines.push_back(buf);

                        const glm::ivec3 n(rayResult.faceNormal);
                        const char* face = n.y > 0 ? "top" : n.y < 0 ? "bottom" : n.x > 0 ? "east (+X)"
                                         : n.x < 0 ? "west (-X)" : n.z > 0 ? "south (+Z)" : n.z < 0 ? "north (-Z)" : "?";
                        const glm::vec3 eye = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
                        const float dist = glm::distance(eye, glm::vec3(b) + glm::vec3(0.5f));
                        std::snprintf(buf, sizeof(buf), "  Block %d %d %d, %s face, %.1f blocks away",
                                      b.x, b.y, b.z, face, dist);
                        lines.push_back(buf);

                        auto floorDiv16 = [](int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); };
                        const int cx = floorDiv16(b.x), cz = floorDiv16(b.z);
                        std::snprintf(buf, sizeof(buf), "  Chunk %d, %d (inside it: %d %d %d)  metadata %d",
                                      cx, cz, b.x - cx * 16, b.y, b.z - cz * 16, static_cast<int>(meta));
                        lines.push_back(buf);

                        std::string flags;
                        auto flag = [&flags](bool on, const char* name) {
                            if (on) flags += (flags.empty() ? "" : ", ") + std::string(name);
                        };
                        flag(prop.isWalkable, "solid");
                        flag(!prop.isWalkable, "walk-through");
                        flag(prop.isTransparent, "transparent");
                        flag(prop.isCutout, "cutout");
                        flag(prop.hasGravity, "falls");
                        flag(!prop.isSelectable, "not selectable");
                        flag(!prop.in_creative, "hidden from creative");
                        lines.push_back("  " + flags + "  (shape: " + (prop.pattern.empty() ? "cube" : prop.pattern) + ")");
                    } else {
                        lines.push_back("");
                        lines.push_back("Looking at: nothing in reach");
                    }
                    // The first block of any kind on the crosshair ray (even ones that can't be
                    // selected), to tell what an odd-looking block really is
                    {
                        glm::vec3 pnt = rayOrigin;
                        for (float t = 0.0f; t <= 6.0f; t += 0.05f, pnt = rayOrigin + rayDir * t) {
                            const glm::ivec3 c(static_cast<int>(std::floor(pnt.x)), static_cast<int>(std::floor(pnt.y)),
                                               static_cast<int>(std::floor(pnt.z)));
                            const uint8_t rid = world.getBlockAt(c);
                            if (rid == 0) continue;
                            const auto& rp = BlockRegistry::getInstance().getBlock(rid);
                            char rbuf[200];
                            std::snprintf(rbuf, sizeof(rbuf), "Ray hits: %s (id %d) at %d %d %d, metadata %d, water id %d",
                                          rp.name.empty() ? "(no block with this id!)" : rp.name.c_str(),
                                          static_cast<int>(rid), c.x, c.y, c.z, static_cast<int>(world.getMetadataAt(c)),
                                          static_cast<int>(BlockRegistry::getInstance().getBlockId("water")));
                            lines.push_back(rbuf);
                            break;
                        }
                    }
                    // Fluid in front of the block (fluids can't be selected, so they're listed apart)
                    {
                        const FluidRayHit fl = raycastFluid(rayOrigin, rayDir, 6.0f, world);
                        if (fl.hit) {
                            const uint8_t fid = world.getBlockAt(fl.pos);
                            const uint8_t fmeta = world.getMetadataAt(fl.pos);
                            const FluidCell fc = FluidRegistry::get().cell(fid, fmeta);
                            const std::string state = !fc ? "?" : fc.source ? "source" : fc.falling ? "falling"
                                : "flow level " + std::to_string(fc.level) + " of " + std::to_string(fc.fluid->spread);
                            char fbuf[200];
                            std::snprintf(fbuf, sizeof(fbuf), "Fluid: %s (block %s, id %d) at %d %d %d, %s, metadata %d",
                                          fc ? fc.fluid->name.c_str() : "?",
                                          BlockRegistry::getInstance().getBlock(fid).name.c_str(), static_cast<int>(fid),
                                          fl.pos.x, fl.pos.y, fl.pos.z, state.c_str(), static_cast<int>(fmeta));
                            lines.push_back(fbuf);
                            std::snprintf(fbuf, sizeof(fbuf), "  Fluids flowing: %zu cells", world.fluids().activeCount());
                            lines.push_back(fbuf);
                        }
                    }
                    const glm::vec2 sc = uiTextScale(0.8f, screen);
                    float y = 24.0f;
                    for (const auto& l : lines) {
                        g_FontRenderer->renderText(fontProgram, l, 11.0f, y + 1.0f, sc.x, sc.y, glm::vec4(0, 0, 0, 0.8f), screen);
                        g_FontRenderer->renderText(fontProgram, l, 10.0f, y, sc.x, sc.y, glm::vec4(1.0f), screen);
                        y += 20.0f;
                    }
                }

                // 5. Chat always on top
                resetUIRenderState();
                if (g_ChatSystem) g_ChatSystem->renderChatOverlay(g_FontRenderer, fontProgram, width, height);

                glEnable(GL_DEPTH_TEST);
            } else if (g_CurrentGameState == GameState::LOADING) {
                resetUIRenderState();
                drawLoadingScreen(uiProgram, fontProgram, g_LoadingProgress, width, height);
            } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_world_select",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::MULTIPLAYER) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_multiplayer",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::MODS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_mods",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_texture_packs",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::SETTINGS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_settings",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else {
                // Gameplay leaves face culling / depth state behind, which hides the
                // font when coming back to the title screen. Reset it every frame.
                resetUIRenderState();

                int sw, sh;
                glfwGetFramebufferSize(window, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);
                scaleCursorToFramebuffer(window, mx, my, sw, sh);

                if (g_MainMenu) {
                    g_MainMenu->updateAndBuildBuffers(mx, my, sw, sh);
                    if (g_FontRenderer) g_MainMenu->render(uiProgram, texturedUIProgram, fontProgram, *g_FontRenderer, sw, sh);
                }
            }

            glfwSwapBuffers(window);
        }

        // Closing the window (or Quit Game) while playing saves the world first
        if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
        g_Client.disconnect();
        g_Server.stop("The host closed the game");
        world.setGenerator(nullptr);   // the generator's threads stop at the end of this block
        g_ItemUseHandlers.clear();     // Lua functions: must go before the Lua state does
        g_Pinger.clear();
        g_Lan.stop();
        for (auto& [addr, t] : g_ServerIcons) if (t.tex) glDeleteTextures(1, &t.tex);
        g_ServerIcons.clear();

        g_Audio.shutdown();
        // (the window and the objects drawing with OpenGL go at the end of this block: GlShutdown)
    } catch (const std::exception& e) {
        std::cerr << "\n[CRITICAL EXCEPTION]: " << e.what() << std::endl;
        system("pause");
        return -1;
    }
    return 0;
}
